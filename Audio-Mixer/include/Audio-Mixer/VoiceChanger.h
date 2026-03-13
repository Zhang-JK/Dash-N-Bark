//
// Created by laojk on 2026-03-12.
//

#ifndef DASH_N_BARK_VOICECHANGER_H
#define DASH_N_BARK_VOICECHANGER_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <queue>
#include <mutex>

#include <bungee/Stream.h>

namespace AudioMixer {

    class VoiceChanger {
    public:
        enum class VoicePreset {
            None,
            Baby,
            LittleGirl,
            Chipmunk,
            DeepVoice,
            Robot,
            Custom
        };

        static constexpr int kSampleRate = 48000;
        static constexpr int kChannels = 2;
        static constexpr int kChunkFrames = 8192;

        VoiceChanger();
        ~VoiceChanger();

        void setPitch(double pitchMultiplier);
        void setPreset(VoicePreset preset);
        void setPresetByName(const std::string& presetName);

        std::optional<std::vector<float>> process(const uint8_t* inputData, size_t inputSize);
        std::optional<std::vector<float>> flush();

        static std::unordered_map<std::string, VoicePreset> getPresetMap();
        static std::vector<std::string> getPresetNames();

    private:
        double pitch_;
        bool enabled_;

        Bungee::Stretcher<Bungee::Basic> stretcher_;
        std::unique_ptr<Bungee::Stream<Bungee::Basic>> stream_;

        std::vector<float> inputAccumulator_;
        std::mutex mutex_;

        static double semitoneToPitch(int semitones);

        bool processChunk(const float* input, int frameCount, float* output, int& outputFrameCount);
    };

}

#endif //DASH_N_BARK_VOICECHANGER_H
