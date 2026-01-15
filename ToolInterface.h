//
// Created by laojk on 2026-01-15.
//

#ifndef DASH_N_BARK_TOOLINTERFACE_H
#define DASH_N_BARK_TOOLINTERFACE_H
#include <memory>

#include "Audio-Mixer/SoundPadManager.h"
#include "Audio-Mixer/AudioMixer.h"
#include "Stream-Fetch/FetchManager.h"


class ToolInterface {
public:
    ToolInterface() = delete;
    ToolInterface(const std::string& base);
    ~ToolInterface();

    int fetchAndEnqueuePlaylist(const std::string& url);

    std::optional<AudioMixer::AudioClip> stepAudioMixer() const;

private:
    std::string base_path_;
    std::shared_ptr<StreamFetch::FetchManager> fetch_manager_;
    std::shared_ptr<AudioMixer::AudioMixer> audio_mixer_;
    std::shared_ptr<AudioMixer::SoundPadManager> sound_pad_manager_;
};


#endif //DASH_N_BARK_TOOLINTERFACE_H