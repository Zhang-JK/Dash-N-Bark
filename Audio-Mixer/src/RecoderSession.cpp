//
// Created by laojk on 2026-02-05.
//

#include "../include/Audio-Mixer/RecoderSession.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

#include "Audio-Mixer/AudioMixer.h"

RecoderSession::RecoderSession(std::string user_id, int duration_seconds, bool keep_data, const std::string& base_path)
    : user_id_(std::move(user_id)),
      target_duration_ms_(std::max(0, duration_seconds) * 1000),
      keep_data_(keep_data),
      target_delay_ms_(1000),
      processing_audio_pointer_(0) {
    start_time_ = std::chrono::steady_clock::now();
    if (target_duration_ms_ < 0) {
        target_duration_ms_ = 60 * 1000; // default to 1 minute
    }
    if (keep_data_) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&t);
        std::string user_dir = base_path + "/user_rec/" + user_id_;
        std::filesystem::create_directories(user_dir);
        std::ostringstream oss;
        oss << user_dir << "/" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".pcm";
        file_to_dump_ = oss.str();
        dump_file_ = std::ofstream(file_to_dump_, std::ios::binary | std::ios::app);
    }
}

RecoderSession::~RecoderSession() {
    // todo
}

void RecoderSession::recordAudio(const uint8_t* audio_data, size_t data_size) {
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
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto buffer = std::make_shared<AudioMixer::AudioBuffer>(audio_data, data_size);
    audio_queue_.push(std::make_tuple(std::move(now), std::move(buffer)));

    if (keep_data_ && dump_file_.is_open()) {
        dump_file_.write(reinterpret_cast<const char*>(audio_data), data_size);
        if (!dump_file_) {
            try { dump_file_.close(); } catch (...) {}
        }
        dump_file_.flush();
    }
}

std::optional<AudioMixer::AudioClip> RecoderSession::streamAudio(int len_ms) {
    // todo: merge two audio if to short
    std::lock_guard lock(mutex_);
    if (audio_queue_.empty()) {
        return std::nullopt;
    }
    auto top = audio_queue_.front();
    auto& buffer = std::get<1>(top);

    if (processing_audio_pointer_ == 0) {
        auto& timestamp = std::get<0>(top);
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp).count();
        if (elapsed_ms < target_delay_ms_) {
            return std::nullopt;
        }
    }

    auto buffer_len = len_ms * AudioMixer::BYTES_PER_SEC_DEFAULT / 1000;
    auto audio_ending = std::min(processing_audio_pointer_ + buffer_len, buffer->getSize());
    AudioMixer::AudioClip clip(buffer, processing_audio_pointer_, audio_ending - processing_audio_pointer_);
    processing_audio_pointer_ = audio_ending;
    if (processing_audio_pointer_ >= buffer->getSize()) {
        audio_queue_.pop();
        processing_audio_pointer_ = 0;
    }
    return clip;
}

bool RecoderSession::isDone() const {
    return
        std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::steady_clock::now() - start_time_).count() >= target_duration_ms_
        && audio_queue_.empty();
}
