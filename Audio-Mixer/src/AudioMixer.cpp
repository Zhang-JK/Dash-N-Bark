//
// Created by laojk on 2025-11-22.
//

#include "Audio-Mixer/AudioMixer.h"
#include  "spdlog/spdlog.h"

#include <utility>
#include <vector>
#include <limits>
#include <iostream>

namespace AudioMixer {

    constexpr float SONG_VOLUME_REMIX = 0.5f;
    constexpr float AUDIO_FX_VOLUME_REMIX = 0.8f;
    constexpr float AUDIO_FX_VOLUME_REDUCTION_RATE = 0.85f;
    constexpr float SOLO_NORMAL_VOLUME = 1.0f;

    void AudioMixer::registerAudio(const AudioClip& clip, SoundType type, float volume) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (type == SoundType::SONG) {
            song_track_.push_back(RegisteredAudio{clip, 0, volume, false});
        } else {
            audio_track_.push_back(RegisteredAudio{clip, 0, volume, false});
        }
    }

    std::optional<std::vector<std::tuple<std::string, int, int>>> AudioMixer::getSongTrackQueueInfo()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (song_track_.empty()) {
            return std::nullopt;
        }

        std::vector<std::tuple<std::string, int, int>> track_info;
        for (const auto& reg_song : song_track_) {
            std::string name = reg_song.clip.getName();
            int total_seconds = static_cast<int>(reg_song.clip.getSize() / BYTES_PER_SEC_DEFAULT);
            int elapsed_seconds = static_cast<int>(reg_song.position / BYTES_PER_SEC_DEFAULT);
            track_info.emplace_back(name, total_seconds, elapsed_seconds);
        }

        return track_info;
    }

    std::optional<std::tuple<std::string, int, int>> AudioMixer::getCurrentPlayingSong()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (song_track_.empty()) {
            return std::nullopt;
        }

        const auto& reg_song = song_track_.front();
        std::string name = reg_song.clip.getName();
        int total_seconds = static_cast<int>(reg_song.clip.getSize() / BYTES_PER_SEC_DEFAULT);
        int elapsed_seconds = static_cast<int>(reg_song.position / BYTES_PER_SEC_DEFAULT);
        return std::make_tuple(name, total_seconds, elapsed_seconds);
    }

    bool AudioMixer::skipCurrentSong()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!song_track_.empty()) {
            song_track_.erase(song_track_.begin());
            spdlog::info("Skipped current song.");
            return true;
        }
        return false;
    }

    std::optional<AudioClip> AudioMixer::step(size_t step_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (song_track_.empty() && audio_track_.empty()) {
            return std::nullopt;
        }

        AudioBufferPtr output_buffer = std::make_shared<AudioBuffer>(step_size);
        auto* buffer_data = reinterpret_cast<int16_t*>(output_buffer->getDataWritable());

        // process song track, only one song can be played at a time
        if (!song_track_.empty()) {
            auto track_volume = audio_track_.empty() ? SOLO_NORMAL_VOLUME : SONG_VOLUME_REMIX;
            RegisteredAudio &reg_song = song_track_.front();
            auto data = reinterpret_cast<const int16_t *>(reg_song.clip.getData());
            size_t clip_size = reg_song.clip.getSize();

            size_t bytes_to_process = std::min(step_size, clip_size - reg_song.position);
            size_t samples_to_process = bytes_to_process / 2;
            for (size_t i = 0; i < samples_to_process; ++i) {
                auto value = static_cast<float>(data[reg_song.position / 2 + i]) *
                             reg_song.volume * track_volume;
                buffer_data[i] = static_cast<int16_t>(
                    std::clamp(static_cast<int32_t>(value),
                        static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                        static_cast<int32_t>(std::numeric_limits<int16_t>::max()))
                );
            }

            reg_song.position += bytes_to_process;
            if (reg_song.position >= clip_size) {
                song_track_.erase(song_track_.begin());
            }
        }

        // process audio fx track, which all sounds are mixed together
        std::vector audio_count(step_size / 2, song_track_.empty() ? SOLO_NORMAL_VOLUME : AUDIO_FX_VOLUME_REMIX);
        for (auto &reg_audio : audio_track_) {
            size_t bytes_to_process = std::min(step_size, reg_audio.clip.getSize() - reg_audio.position);
            size_t samples_to_process = bytes_to_process / 2;

            for (size_t i = 0; i < samples_to_process; ++i) {
                audio_count[i] *= AUDIO_FX_VOLUME_REDUCTION_RATE;
            }
        }

        for (auto it = audio_track_.begin(); it != audio_track_.end(); ) {
            RegisteredAudio &reg_audio = *it;
            auto data = reinterpret_cast<const int16_t *>(reg_audio.clip.getData());
            size_t clip_size = reg_audio.clip.getSize();

            size_t bytes_to_process = std::min(step_size, clip_size - reg_audio.position);
            size_t samples_to_process = bytes_to_process / 2;
            for (size_t i = 0; i < samples_to_process; ++i) {
                auto value = static_cast<float>(buffer_data[i]) +
                            static_cast<float>(data[reg_audio.position / 2 + i]) *
                            reg_audio.volume / audio_count[i];
                buffer_data[i] = static_cast<int16_t>(
                    std::clamp(static_cast<int32_t>(value),
                        static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                        static_cast<int32_t>(std::numeric_limits<int16_t>::max()))
                );
            }

            reg_audio.position += bytes_to_process;
            if (reg_audio.position >= clip_size) {
                it = audio_track_.erase(it);
            } else {
                ++it;
            }
        }

        return AudioClip(output_buffer);
    }

    void AudioMixer::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_track_.clear();
        song_track_.clear();
        spdlog::info("Cleared all registered audio clips.");
    }

} // namespace AudioMixer