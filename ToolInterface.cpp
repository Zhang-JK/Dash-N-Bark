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

ToolInterface::ToolInvokeResult<StreamFetch::FetchManager::StreamFetchResult>
    ToolInterface::fetchAudioFromUrl(const std::string& url)
{
    spdlog::debug("fetch_manager_ {}", (void*)fetch_manager_.get());
    auto res = fetch_manager_->fetchFromURL(url);
    if (!res.isValid()) {
        spdlog::error("Failed to fetch playlist from URL: {}, error code: {}, reason: {}",
                            url, res.error_code, res.error_msg);
        return {
            .success = res.error_code==0,
            .error_code = res.error_code,
            .message = res.error_msg
        };
    }
    return {
        .success = res.error_code==0,
        .error_code = res.error_code,
        .message = res.error_msg,
        .data = res
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::fetchAndEnqueuePlaylist(const std::string& url, int volume) {
    spdlog::debug("fetch_manager_ {}", (void*)fetch_manager_.get());
    if (volume < 1 || volume > 200) {
        return {
            .success = false,
            .error_code = -1,
            .message = "Volume must be between 1 and 200"
        };
    }
    auto res = fetch_manager_->fetchFromURL(url);
    if (!res.isValid()) {
        spdlog::error("Failed to fetch playlist from URL: {}, error code: {}, reason: {}",
                            url, res.error_code, res.error_msg);
        return {
            .success = res.error_code==0,
            .error_code = res.error_code,
            .message = res.error_msg
        };
    }
    const auto audio_path = res.path.value();
    const AudioMixer::AudioClip clip(audio_path, AudioMixer::AudioBuffer::PCM_16BIT_STEREO_48K, res.title);
    audio_mixer_->registerAudio(clip,
        // hack for testing
        volume==77 ? AudioMixer::AudioMixer::AUDIO_EFFECT : AudioMixer::AudioMixer::SONG,
        static_cast<float>(volume) / 100.0f
    );
    return {
        .success = res.error_code==0,
        .error_code = res.error_code,
        .message = res.error_msg,
        .data = res.title
    };
}

ToolInterface::ToolInvokeResult<std::optional<std::vector<std::tuple<std::string, int, int>>>> ToolInterface::getPlaylist()
{
    auto playlist = audio_mixer_->getSongTrackQueueInfo();
    return {
        .success = playlist.has_value(),
        .data = std::move(playlist)
    };
}

ToolInterface::ToolInvokeResult<std::optional<std::tuple<std::string, int, int>>> ToolInterface::getCurrentSong()
{
    auto current_song = audio_mixer_->getCurrentPlayingSong();
    return {
        .success = current_song.has_value(),
        .data = std::move(current_song)
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::skipCurrentSong()
{
    return {
        .success = audio_mixer_->skipCurrentSong()
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::playAudioFromFile(const std::string& file_path)
{
    auto real_path = base_path_ + "/" + file_path;
    AudioMixer::AudioClip clip(real_path, AudioMixer::AudioBuffer::PCM_16BIT_STEREO_48K);
    audio_mixer_->registerAudio(clip, AudioMixer::AudioMixer::SONG);
    return {
        .success = true,
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::clearAllAudio()
{
    audio_mixer_->clear();
    return {
        .success = true,
    };
}

std::optional<AudioMixer::AudioClip> ToolInterface::stepAudioMixer(size_t step_size) const
{
    return audio_mixer_->step(step_size);
}

