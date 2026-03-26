//
// Created by laojk on 2026-02-05.
//

#include "../include/Audio-Mixer/RecorderSession.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

#include "Audio-Mixer/AudioMixer.h"
#include "spdlog/spdlog.h"

namespace AudioMixer {

    RecorderSession::RecorderSession(std::string user_id, int duration_seconds, bool keep_data, const std::string& base_path)
        : user_id_(std::move(user_id)),
          target_duration_ms_(std::max(0, duration_seconds) * 1000),
          keep_data_(keep_data),
          target_delay_ms_(300),
          processing_audio_pointer_(0),
          is_shutting_down_(false),
          headBufferPointer_(0) {
        start_time_ = std::chrono::steady_clock::now();
        if (keep_data_) {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#if defined(_WIN32)
            ::localtime_s(&tm, &t);
#else
            ::localtime_r(&t, &tm);
#endif
            std::string user_dir = base_path + "/user_rec/" + user_id_;
            std::filesystem::create_directories(user_dir);
            std::ostringstream oss;
            oss << user_dir << "/" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".pcm";
            file_to_dump_ = oss.str();
            dump_file_ = std::ofstream(file_to_dump_, std::ios::binary | std::ios::app);
        }
    }

    RecorderSession::~RecorderSession() {
        std::lock_guard lock(mutex_);
        if (!is_shutting_down_) {
            shutdown();
        }
        // todo
    }

    void RecorderSession::recordAudio(const uint8_t* audio_data, size_t data_size) {
        if (is_shutting_down_) {
            return;
        }
        std::lock_guard lock(mutex_);
        if (!audio_data || data_size == 0) {
            return;
        }
        if (target_duration_ms_ > 0) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time_).count();
            if (elapsed_ms >= target_duration_ms_) {
                return;
            }
        }
        auto now = std::chrono::steady_clock::now();
        auto buffer = std::make_shared<AudioBuffer>(audio_data, data_size);
        audio_queue_.push(std::make_tuple(std::move(now), std::move(buffer)));

        if (keep_data_ && dump_file_.is_open()) {
            dump_file_.write(reinterpret_cast<const char*>(audio_data), data_size);
            if (!dump_file_) {
                try { dump_file_.close(); } catch (...) {}
            }
            dump_file_.flush();
        }
    }

    std::optional<AudioClip> RecorderSession::streamAudio(int len_ms) {
        if (is_shutting_down_) {
            return std::nullopt;
        }
        std::lock_guard lock(mutex_);
        if (audio_queue_.empty()) {
            return std::nullopt;
        }

        if (headBufferPointer_ == 0) {
            auto& timestamp = std::get<0>(audio_queue_.front());
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp).count();
            if (elapsed_ms < target_delay_ms_) {
                return std::nullopt;
            }
        }

        size_t total_bytes_needed = static_cast<size_t>(len_ms) * BYTES_PER_SEC_DEFAULT / 1000;
        auto combined_buffer = std::make_shared<AudioBuffer>(total_bytes_needed);
        size_t bytes_written = 0;

        while (bytes_written < total_bytes_needed && !audio_queue_.empty()) {
            auto& buffer = std::get<1>(audio_queue_.front());
            size_t available = buffer->getSize() - headBufferPointer_;
            size_t to_copy = std::min(available, total_bytes_needed - bytes_written);

            std::copy(buffer->getData() + headBufferPointer_,
                      buffer->getData() + headBufferPointer_ + to_copy,
                      combined_buffer->getData() + bytes_written);
            bytes_written += to_copy;
            headBufferPointer_ += to_copy;

            if (static_cast<size_t>(headBufferPointer_) >= buffer->getSize()) {
                audio_queue_.pop();
                headBufferPointer_ = 0;
            }
        }

        if (bytes_written == 0) {
            return std::nullopt;
        }

        if (voice_changer_) {
            auto processed = voice_changer_->process(combined_buffer->getData(), bytes_written);
            if (processed && !processed->empty()) {
                size_t processed_sample_count = processed->size();
                size_t processed_size = processed_sample_count * sizeof(int16_t);
                auto output_buffer = std::make_shared<AudioBuffer>(processed_size);
                auto* output_data = reinterpret_cast<int16_t*>(output_buffer->getData());
                for (size_t i = 0; i < processed_sample_count; ++i) {
                    float sample = (*processed)[i];
                    sample = std::clamp(sample, -1.0f, 1.0f);
                    output_data[i] = static_cast<int16_t>(sample * 32767.0f);
                }
                return AudioClip(output_buffer, 0, processed_size);
            }
        }

        return AudioClip(combined_buffer, 0, bytes_written);
    }

    // this is a hack function
    // sometimes the stream coroutine does not run
    // have to use this for checking if timeout happens
    bool RecorderSession::isTimeOut() {
        if (is_shutting_down_) {
            return true;
        }
        std::lock_guard lock(mutex_);
        return
            std::chrono::duration_cast<std::chrono::milliseconds>
                (std::chrono::steady_clock::now() - start_time_).count()
                >= target_duration_ms_ + 5000;      // timeout is set to 5000ms
    }

    void RecorderSession::shutdown() {
        std::lock_guard lock(mutex_);
        if (is_shutting_down_) {
            return;
        }
        is_shutting_down_ = true;
        while (!audio_queue_.empty()) {
            audio_queue_.pop();
        }
        if (dump_file_.is_open()) {
            try { dump_file_.close(); } catch (...) {}
        }
    }

    std::string RecorderSession::getUserId() const {
        return user_id_;
    }

    bool RecorderSession::checkUserId(const std::string& user_id) {
        std::lock_guard lock(mutex_);
        return user_id == user_id_;
    }

    bool RecorderSession::isShuttingDown() {
        std::lock_guard lock(mutex_);
        return is_shutting_down_;
    }

    void RecorderSession::setVoiceChangerEnabled(bool enabled) {
        if (!voice_changer_ && enabled) {
            voice_changer_ = std::make_shared<VoiceChanger>();
        }
    }

    void RecorderSession::setVoiceChangerPreset(VoiceChanger::VoicePreset preset) {
        if (!voice_changer_) {
            voice_changer_ = std::make_shared<VoiceChanger>();
        }
        voice_changer_->setPreset(preset);
    }

    void RecorderSession::setVoiceChangerPitch(double pitch) {
        if (!voice_changer_) {
            voice_changer_ = std::make_shared<VoiceChanger>();
        }
        voice_changer_->setPitch(pitch);
    }

} // namespace AudioMixer
