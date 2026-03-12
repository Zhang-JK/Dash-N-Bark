//
// Created by laojk on 2026-03-12.
//

#include "../include/Audio-Mixer/VoiceChanger.h"
#include "spdlog/spdlog.h"

#include <cstring>
#include <algorithm>

namespace AudioMixer {

    VoiceChanger::VoiceChanger()
        : pitch_(1.0)
        , enabled_(false)
        , stretcher_(Bungee::SampleRates{kSampleRate, kSampleRate}, kChannels, 0)
        , stream_(std::make_unique<Bungee::Stream<Bungee::Basic>>(stretcher_, kChunkFrames, kChannels))
    {
    }

    VoiceChanger::~VoiceChanger() = default;

    double VoiceChanger::semitoneToPitch(int semitones) {
        return std::pow(2.0, static_cast<double>(semitones) / 12.0);
    }

    void VoiceChanger::setPitch(double pitchMultiplier) {
        pitch_ = pitchMultiplier;
        enabled_ = (pitch_ != 1.0);
    }

    void VoiceChanger::setPreset(VoicePreset preset) {
        switch (preset) {
            case VoicePreset::None:
                pitch_ = 1.0;
                enabled_ = false;
                break;
            case VoicePreset::Baby:
                pitch_ = semitoneToPitch(4);
                enabled_ = true;
                break;
            case VoicePreset::LittleGirl:
                pitch_ = semitoneToPitch(6);
                enabled_ = true;
                break;
            case VoicePreset::Chipmunk:
                pitch_ = semitoneToPitch(8);
                enabled_ = true;
                break;
            case VoicePreset::DeepVoice:
                pitch_ = semitoneToPitch(-4);
                enabled_ = true;
                break;
            case VoicePreset::Robot:
                pitch_ = semitoneToPitch(-2);
                enabled_ = true;
                break;
            case VoicePreset::Custom:
                enabled_ = (pitch_ != 1.0);
                break;
        }
    }

    void VoiceChanger::setPresetByName(const std::string& presetName) {
        auto it = getPresetMap().find(presetName);
        if (it != getPresetMap().end()) {
            setPreset(it->second);
        } else {
            spdlog::warn("Unknown voice preset: {}", presetName);
        }
    }

    std::unordered_map<std::string, VoiceChanger::VoicePreset> VoiceChanger::getPresetMap() {
        return {
            {"none", VoicePreset::None},
            {"baby", VoicePreset::Baby},
            {"little_girl", VoicePreset::LittleGirl},
            {"littlegirl", VoicePreset::LittleGirl},
            {"chipmunk", VoicePreset::Chipmunk},
            {"deep_voice", VoicePreset::DeepVoice},
            {"deepvoice", VoicePreset::DeepVoice},
            {"robot", VoicePreset::Robot},
            {"custom", VoicePreset::Custom}
        };
    }

    std::vector<std::string> VoiceChanger::getPresetNames() {
        return {
            "none",
            "baby",
            "little_girl",
            "chipmunk",
            "deep_voice",
            "robot",
            "custom"
        };
    }

    bool VoiceChanger::processChunk(const float* input, int frameCount, float* output, int& outputFrameCount) {
        const float* inputPointers[kChannels];
        float* outputPointers[kChannels];

        for (int c = 0; c < kChannels; ++c) {
            inputPointers[c] = input + c * frameCount;
            outputPointers[c] = output + c * frameCount;
        }

        double outputFrameCountIdeal = frameCount;
        
        outputFrameCount = stream_->process(
            inputPointers,
            outputPointers,
            frameCount,
            outputFrameCountIdeal,
            pitch_
        );

        return outputFrameCount > 0;
    }

    std::optional<std::vector<float>> VoiceChanger::process(const uint8_t* inputData, size_t inputSize) {
        if (!enabled_) {
            return std::nullopt;
        }

        int inputFrameCount = static_cast<int>(inputSize) / (kChannels * sizeof(int16_t));
        if (inputFrameCount == 0) {
            return std::nullopt;
        }

        const int16_t* inputSamples = reinterpret_cast<const int16_t*>(inputData);

        std::vector<float> floatInput(inputFrameCount * kChannels);
        for (int frame = 0; frame < inputFrameCount; ++frame) {
            for (int c = 0; c < kChannels; ++c) {
                floatInput[c * inputFrameCount + frame] = static_cast<float>(inputSamples[frame * kChannels + c]) / 32768.0f;
                floatInput[c * inputFrameCount + frame] = std::clamp(floatInput[c * inputFrameCount + frame], -1.0f, 1.0f);
            }
        }

        std::vector<float> floatOutput(inputFrameCount * kChannels);
        int outputFrameCount = 0;
        
        bool success = processChunk(floatInput.data(), inputFrameCount, floatOutput.data(), outputFrameCount);
        
        if (!success || outputFrameCount == 0) {
            return std::nullopt;
        }

        std::vector<float> result(outputFrameCount * kChannels);
        for (int frame = 0; frame < outputFrameCount; ++frame) {
            for (int c = 0; c < kChannels; ++c) {
                result[frame * kChannels + c] = floatOutput[c * outputFrameCount + frame];
            }
        }

        return result;
    }

    std::optional<std::vector<float>> VoiceChanger::flush() {
        if (!enabled_) {
            return std::nullopt;
        }

        std::vector<float> zeroInput(kChunkFrames * kChannels, 0.0f);
        std::vector<float> floatOutput(kChunkFrames * kChannels);
        int outputFrameCount = 0;

        const float* inputPointers[kChannels];
        float* outputPointers[kChannels];
        for (int c = 0; c < kChannels; ++c) {
            inputPointers[c] = zeroInput.data() + c * kChunkFrames;
            outputPointers[c] = floatOutput.data() + c * kChunkFrames;
        }

        stream_->process(
            inputPointers,
            outputPointers,
            0,
            0,
            pitch_
        );

        if (outputFrameCount > 0) {
            std::vector<float> result(outputFrameCount * kChannels);
            std::memcpy(result.data(), floatOutput.data(), result.size() * sizeof(float));
            return result;
        }

        return std::nullopt;
    }

}
