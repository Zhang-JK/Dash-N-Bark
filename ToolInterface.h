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

    template<typename T = std::optional<std::string>>
    struct ToolInvokeResult {
        bool success;
        int error_code;
        std::string message;
        T data;
    };

    ToolInvokeResult<> playAudioFromFile(const std::string& file_path);
    ToolInvokeResult<StreamFetch::FetchManager::StreamFetchResult> fetchAudioFromUrl(const std::string& url);

    ToolInvokeResult<> fetchAndEnqueuePlaylist(const std::string& url, int volume);
    ToolInvokeResult<std::optional<std::vector<std::tuple<std::string, int, int>>>> getPlaylist();
    ToolInvokeResult<std::optional<std::tuple<std::string, int, int>>> getCurrentSong();
    ToolInvokeResult<> skipCurrentSong();
    ToolInvokeResult<> clearAllAudio();

    [[nodiscard]] std::optional<AudioMixer::AudioClip> stepAudioMixer(size_t step_size) const;

private:
    std::string base_path_;
    std::shared_ptr<StreamFetch::FetchManager> fetch_manager_;
    std::shared_ptr<AudioMixer::AudioMixer> audio_mixer_;
    std::shared_ptr<AudioMixer::SoundPadManager> sound_pad_manager_;
};


#endif //DASH_N_BARK_TOOLINTERFACE_H