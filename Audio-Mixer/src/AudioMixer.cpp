//
// Created by laojk on 2025-11-22.
//

#include "Audio-Mixer/AudioMixer.h"

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
        // auto* buffer_data = reinterpret_cast<uint16_t*>(output_buffer->getDataWritable());
        auto* buffer_data = output_buffer->getDataWritable();

        for (auto it = audio.begin(); it != audio.end(); ) {
            RegisteredAudio &reg_audio = *it;
            const uint8_t* data = reg_audio.clip.getData();
            size_t clip_size = reg_audio.clip.getSize();

            size_t bytes_to_process = std::min(step_size, clip_size - reg_audio.position);
            std::cout << "step_size: " << step_size
                      << ", clip_size: " << clip_size
                      << ", position: " << reg_audio.position
                      << ", bytes_to_process: " << bytes_to_process << std::endl;
            for (size_t i = 0; i < bytes_to_process; ++i) {
                // buffer_data[i] = static_cast<uint16_t>(
                //     std::min(
                //         static_cast<uint32_t>(buffer_data[i]) +
                //         static_cast<uint32_t>(
                //             reinterpret_cast<const uint16_t *>(data)[reg_audio.position / 2 + i] * reg_audio.volume
                //         ),
                //         static_cast<uint32_t>(std::numeric_limits<uint16_t>::max() - 10)
                //     )
                // );
                auto mixed_sample = static_cast<uint32_t>(buffer_data[i]) +
                                   static_cast<uint32_t>(static_cast<float>(data[reg_audio.position + i]));
                buffer_data[i] = static_cast<uint8_t>(
                    std::min(
                        mixed_sample,
                        static_cast<uint32_t>(std::numeric_limits<uint8_t>::max() - 5)
                    )
                );
            }

            reg_audio.position += bytes_to_process;
            if (reg_audio.position >= clip_size) {
                it = audio.erase(it);
            } else {
                ++it;
            }
        }

        return AudioClip(output_buffer);
    }

} // namespace AudioMixer