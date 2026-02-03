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

ToolInterface::ToolInvokeResult<std::optional<std::map<int, std::string>>> ToolInterface::listTagsPaged(
    int page, int page_size, int& total_pages) {
    auto res = sound_pad_manager_->listTags(page, page_size);
    total_pages = sound_pad_manager_->countTags() / page_size +
                  (sound_pad_manager_->countTags() % page_size == 0 ? 0 : 1);
    return {
        .success = res.has_value(),
        .data = std::move(res)
    };
}

ToolInterface::ToolInvokeResult<std::optional<std::map<int, std::string>>> ToolInterface::listSoundpadClipsPaged(
                                int page, int page_size, int& total_pages, const std::string& tag) {
    auto data = sound_pad_manager_->listSounds(page, page_size, tag);
    std::map<int, std::string> res;
    if (data) {
        for (auto& [id, sound] : *data) {
            res[id] = sound.name;
        }
        total_pages = sound_pad_manager_->countSounds(tag) / page_size +
                      (sound_pad_manager_->countSounds(tag) % page_size == 0 ? 0 : 1);
    }
    return {
        .success = data.has_value(),
        .data = data.has_value() ? std::optional(std::move(res)) : std::nullopt
    };
}

ToolInterface::ToolInvokeResult<> ToolInterface::playSoundpadClip(int clip_id, int volume) {
    volume = std::clamp(volume, 1, 200);
    auto res = sound_pad_manager_->loadAudioClip(clip_id);
    if (!res) {
        return {
            .success = false,
            .error_code = -1,
            .message = "Clip not found"
        };
    }
    audio_mixer_->registerAudio(res.value(), AudioMixer::AudioMixer::AUDIO_EFFECT,
                            static_cast<float>(volume) / 100.0f);
    return {
        .success = true
    };
}
