//
// Created by laojk on 2025-11-22.
//

#ifndef DASH_N_BARK_AUDIOMIXER_H
#define DASH_N_BARK_AUDIOMIXER_H

#include <optional>
#include <list>

#include "AudioBuffer.h"

namespace AudioMixer {

    constexpr int SAMPLE_RATE_48K = 48000;
    constexpr int CHANNELS_STEREO = 2;
    constexpr int BYTES_PER_SAMPLE_16 = 2;
    constexpr int BYTES_PER_SEC_DEFAULT = SAMPLE_RATE_48K * CHANNELS_STEREO * BYTES_PER_SAMPLE_16;

    class AudioMixer {
    public:
        enum SoundType {
            SONG,
            AUDIO_EFFECT
        };

        void registerAudio(const AudioClip& clip, SoundType type, float volume=1.0f);
        std::optional<AudioClip> step(size_t step_size = BYTES_PER_SEC_DEFAULT); // 1000ms per step
        void clear();

    private:
        struct RegisteredAudio {
            AudioClip clip;
            size_t position{0};
            float volume{1.0f};
            bool finished{false};
        };

        std::list<RegisteredAudio> song_track_;
        std::list<RegisteredAudio> audio_track_;
        std::mutex mutex_;
    };

} // namespace AudioMixer


#endif //DASH_N_BARK_AUDIOMIXER_H