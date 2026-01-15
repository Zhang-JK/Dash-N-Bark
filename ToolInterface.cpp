//
// Created by laojk on 2026-01-15.
//

#include "ToolInterface.h"
#include <spdlog/spdlog.h>

ToolInterface::ToolInterface(const std::string &base) : base_path_(base)
{
    fetch_manager_ = std::make_shared<StreamFetch::FetchManager>(base_path_);
    audio_mixer_ = std::make_shared<AudioMixer::AudioMixer>();
    sound_pad_manager_ = std::make_shared<AudioMixer::SoundPadManager>();
    sound_pad_manager_->initialize(base_path_ + "/soundpad.db", base_path_ + "/sounds");
}

ToolInterface::~ToolInterface()
{
    // Cleanup if necessary
}

int ToolInterface::fetchAndEnqueuePlaylist(const std::string& url)
{
    auto res = fetch_manager_->fetchFromURL(url);
    if (!res.isValid()) {
        spdlog::error("Failed to fetch playlist from URL: {}, error code: {}, reason: {}",
                            url, res.error_code, res.error_msg);
        return -1;
    }
    auto audio_path = res.path.value();
    AudioMixer::AudioClip clip(audio_path, AudioMixer::AudioBuffer::PCM_16BIT_STEREO_48K);
    audio_mixer_->registerAudio(clip);
    return 0;
}

std::optional<AudioMixer::AudioClip> ToolInterface::stepAudioMixer() const
{
    return audio_mixer_->step();
}

