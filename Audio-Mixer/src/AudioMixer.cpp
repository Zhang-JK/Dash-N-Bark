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

    void AudioMixer::registerAudio(AudioClip clip, float volume) {
        std::lock_guard<std::mutex> lock(mutex);
        audio.push_back(RegisteredAudio{std::move(clip), 0, volume, false});
    }

    std::optional<AudioClip> AudioMixer::step(size_t step_size) {
        std::lock_guard<std::mutex> lock(mutex);
        if (audio.empty()) {
            return std::nullopt;
        }

        AudioBufferPtr output_buffer = std::make_shared<AudioBuffer>(step_size);
        auto* buffer_data = reinterpret_cast<int16_t*>(output_buffer->getDataWritable());
        std::vector audio_count(step_size / 2, 0.0f);
        for (auto &reg_audio : audio) {
            size_t bytes_to_process = std::min(step_size, reg_audio.clip.getSize() - reg_audio.position);
            size_t samples_to_process = bytes_to_process / 2;

            for (size_t i = 0; i < samples_to_process; ++i) {
                audio_count[i] += 1.0f;
            }
        }

        for (auto it = audio.begin(); it != audio.end(); ) {
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
                it = audio.erase(it);
            } else {
                ++it;
            }
        }

        if (audio.empty()) {
            spdlog::info("All audio clips finished.");
        }

        return AudioClip(output_buffer);
    }

    void AudioMixer::clear() {
        std::lock_guard<std::mutex> lock(mutex);
        spdlog::info("Clearing all registered audio clips.");
        audio.clear();
    }

} // namespace AudioMixer