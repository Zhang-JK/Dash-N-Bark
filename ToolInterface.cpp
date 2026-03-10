//
// Created by laojk on 2026-01-15.
//

#include "ToolInterface.h"

#include <filesystem>
#include <spdlog/spdlog.h>

#include <exec/repeat_until.hpp>
#include <exec/start_detached.hpp>

#include <utility>

ToolInterface::ToolInterface(std::string base, std::shared_ptr<exec::static_thread_pool> pool)
: base_path_(std::move(base)), ppool_(std::move(pool))
{
    fetch_manager_ = std::make_shared<StreamFetch::FetchManager>(base_path_);
    audio_mixer_ = std::make_shared<AudioMixer::AudioMixer>();
    sound_pad_manager_ = std::make_shared<AudioMixer::SoundPadManager>();
    sound_pad_manager_->initialize(base_path_ + "/soundpad.db", base_path_ + "/sounds");
    recorder_sessions_ = {};
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

ToolInterface::ToolInvokeResult<> ToolInterface::playAudioFromFile(std::string path,
                                                                   AudioMixer::AudioMixer::SoundType type,
                                                                   float volume)
{
    auto full_path = base_path_ + "/" + path;
    if (!std::filesystem::exists(full_path)) {
        return {
            .success = false,
            .error_code = -1,
            .message = "File does not exist: " + full_path
        };
    }
    AudioMixer::AudioClip clip(full_path, AudioMixer::AudioBuffer::PCM_16BIT_STEREO_48K, path);
    audio_mixer_->registerAudio(clip, type, volume);
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

ToolInterface::ToolInvokeResult<> ToolInterface::initRecordingService(std::string user_id, int duration_seconds) {
    std::lock_guard<std::mutex> lock(recorder_mutex_);
    if (recorder_sessions_.find(user_id) != recorder_sessions_.end()) {
        return {
            .success = false,
            .error_code = -1,
            .message = "Recording session already exists for user_id"
        };
    }
    // Reserve an entry (default constructed session pointer); actual session initialization can be done later
    recorder_sessions_[user_id] = std::make_shared<AudioMixer::RecorderSession>(user_id, duration_seconds, true, base_path_);
    auto session = recorder_sessions_[user_id];
    // todo: hackcode here
    auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds+1);
    auto sched = ppool_->get_scheduler();
    exec::timed_thread_scheduler timed_sched = timed_thread_context_.get_scheduler();
    auto work = stdexec::schedule(sched) | stdexec::let_value(
        [timed_sched, session, end_time, this] {
            return exec::repeat_until(
                exec::schedule_after(timed_sched, std::chrono::milliseconds(60))
                | stdexec::then([session, end_time, this] {
                    auto local_clip = session->streamAudio();
                    if (local_clip) {
                        audio_mixer_->registerAudio(local_clip.value(), AudioMixer::AudioMixer::AUDIO_EFFECT);
                    }
                    return std::chrono::steady_clock::now() >= end_time;
                })
            ) | stdexec::then([session] {
                spdlog::info("Recording session ended, shutting down session");
                session->shutdown();
            });
        }
    )
    | stdexec::upon_error([](auto&&) noexcept {})
    | stdexec::upon_stopped([]() noexcept {});
    exec::start_detached(std::move(work));
    return {
        .success = true,
    };
}

void ToolInterface::recordingVoiceCallback(std::vector<uint8_t> data, size_t size, const std::string& user_id) {
    {
        std::lock_guard<std::mutex> lock(recorder_mutex_);
        if (recorder_sessions_.find(user_id) == recorder_sessions_.end()) {
            return;
        }
        auto session = recorder_sessions_[user_id];
        if (session->isShuttingDown()) {
            spdlog::info("Recording session ended, stop receiving audio data for user_id: {}", user_id);
            recorder_sessions_.erase(session->getUserId());
        } else {
            session->recordAudio(data.data(), size);
        }
    }
}

