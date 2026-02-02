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

ToolInterface::ToolInvokeResult<> ToolInterface::playAudioClip(AudioMixer::AudioClip& clip,
                                                    AudioMixer::AudioMixer::SoundType type)
{
    audio_mixer_->registerAudio(clip, type);
    return {
        .success = true,
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::fetchAndEnqueuePlaylist(const std::string& url, int volume) {
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

ToolInterface::ToolInvokeResult<std::optional<AudioMixer::AudioClip>> ToolInterface::fetchSoundFromUrl(const std::string& url)
{
    auto res = fetch_manager_->fetchFromURL(url);
    return {
        .success = res.isValid(),
        .error_code = res.error_code,
        .message = res.error_msg,
        .data = res.isValid()
            ? std::optional(
                AudioMixer::AudioClip(
                    res.path.value(), AudioMixer::AudioBuffer::PCM_16BIT_STEREO_48K, res.title))
            : std::nullopt
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::addToSoundpad(AudioMixer::AudioClip& clip, const std::string& name,
        const std::string& user_id, const std::string& tag1, const std::string& tag2, bool fav)
{
    auto res = sound_pad_manager_->saveAudioClip(clip, name, user_id, tag1, tag2, fav);
    return {
        .success = std::get<0>(res),
        .error_code = std::get<0>(res) ? 0 : -1,
        .message = std::get<1>(res)
    };
}
