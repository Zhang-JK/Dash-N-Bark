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
    join_effect_manager_ = std::make_shared<AudioMixer::JoinEffectManager>();
    join_effect_manager_->initialize(base_path_ + "/join_effects.db");
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

ToolInterface::ToolInvokeResult<> ToolInterface::playSoundpadClipByName(const std::string& clip_name, int volume) {
    volume = std::clamp(volume, 1, 200);
    auto res = sound_pad_manager_->loadAudioClip(clip_name);
    if (!res) {
        return {
            .success = false,
            .error_code = -1,
            .message = "Clip not found: " + clip_name
        };
    }
    audio_mixer_->registerAudio(res.value(), AudioMixer::AudioMixer::AUDIO_EFFECT,
                            static_cast<float>(volume) / 100.0f);
    return {
        .success = true
    };
}

std::vector<std::pair<int, std::string>> ToolInterface::searchSoundpadByName(
        const std::string& query, int limit) const {
    return sound_pad_manager_->searchByName(query, limit);
}

ToolInterface::ToolInvokeResult<> ToolInterface::setJoinEffect(const std::string& guild_id,
        const std::string& user_id, const std::string& clip_name) {
    if (!sound_pad_manager_->loadAudioClip(clip_name)) {
        return {.success = false, .error_code = -1, .message = "Soundpad clip not found: " + clip_name};
    }
    bool ok = join_effect_manager_->set(guild_id, user_id, clip_name);
    return {.success = ok, .error_code = ok ? 0 : -1, .message = ok ? "" : "Failed to save join effect"};
}

ToolInterface::ToolInvokeResult<> ToolInterface::removeJoinEffect(const std::string& guild_id,
        const std::string& user_id) {
    bool ok = join_effect_manager_->remove(guild_id, user_id);
    return {.success = ok, .error_code = ok ? 0 : -1, .message = ok ? "" : "Failed to remove join effect"};
}

std::optional<std::string> ToolInterface::getJoinEffect(const std::string& guild_id,
        const std::string& user_id) const {
    return join_effect_manager_->get(guild_id, user_id);
}

std::vector<std::pair<std::string, std::string>> ToolInterface::listJoinEffects(
        const std::string& guild_id) const {
    return join_effect_manager_->listForGuild(guild_id);
}

ToolInterface::ToolInvokeResult<> ToolInterface::initRecordingService(std::string user_id, int duration_seconds, AudioMixer::VoiceChanger::VoicePreset voice_preset) {
    std::shared_ptr<AudioMixer::RecorderSession> session = nullptr;
    {
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
        session = recorder_sessions_[user_id];
        session->setVoiceChangerPreset(voice_preset);
    } // release lock here
    auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds+1);    // delay stream for 1s
    auto sched = ppool_->get_scheduler();
    exec::timed_thread_scheduler timed_sched = timed_thread_context_.get_scheduler();
    auto work = stdexec::schedule(sched) | stdexec::let_value(
        [timed_sched, sched, session, end_time, this] {
            return exec::repeat_until(
                exec::schedule_after(timed_sched, std::chrono::milliseconds(60))
                | stdexec::continues_on(sched)
                | stdexec::then([session, end_time, this] {
                    spdlog::debug("Stream left: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(end_time - std::chrono::steady_clock::now()).count());
                    auto local_clip = session->streamAudio();
                    if (local_clip) {
                        audio_mixer_->registerAudio(local_clip.value(), AudioMixer::AudioMixer::AUDIO_EFFECT, 0.8);
                    }
                    return std::chrono::steady_clock::now() >= end_time;
                })
            ) | stdexec::then([session, this] {
                spdlog::info("Recording session ended, shutting down session for user_id: {}", session->getUserId());
                {
                    std::lock_guard<std::mutex> lock(recorder_mutex_);
                    recorder_sessions_.erase(session->getUserId());
                }
                session->shutdown();
            });
        }
    )
    | stdexec::upon_error([session, this](auto&&) noexcept {
        spdlog::info("Recording session ended, shutting down session for user_id: {}", session->getUserId());
        {
            std::lock_guard<std::mutex> lock(recorder_mutex_);
            recorder_sessions_.erase(session->getUserId());
        }
        session->shutdown();
    })
    | stdexec::upon_stopped([session, this]() noexcept {
        spdlog::info("Recording session ended, shutting down session for user_id: {}", session->getUserId());
        {
            std::lock_guard<std::mutex> lock(recorder_mutex_);
            recorder_sessions_.erase(session->getUserId());
        }
        session->shutdown();
    });
    exec::start_detached(std::move(work));
    return {
        .success = true,
    };
}

void ToolInterface::recordingVoiceCallback(std::vector<uint8_t> data, size_t size, const std::string& user_id) {
    // First, grab a shared_ptr to the session under recorder_mutex_, then
    // release the lock before calling any RecorderSession methods to avoid
    // potential lock-order inversions.
    std::shared_ptr<AudioMixer::RecorderSession> session;
    {
        std::lock_guard<std::mutex> lock(recorder_mutex_);
        auto it = recorder_sessions_.find(user_id);
        if (it == recorder_sessions_.end()) {
            return;
        }
        session = it->second;
    }

    if (session->isShuttingDown()) {
        spdlog::info("Recording session ended, stop receiving audio data for user_id: {}", user_id);
    } else {
        spdlog::debug("Recording session received audio data for user_id: {}, size: {}", user_id, size);
        if (session->isTimeout()) {
            spdlog::warn("Recording session timed out, force shutting down session for user_id: {}", user_id);
            {
                std::lock_guard<std::mutex> lock(recorder_mutex_);
                recorder_sessions_.erase(user_id);
            }
            session->shutdown();
            return;
        }
        session->recordAudio(data.data(), size);
    }
}

std::vector<StreamFetch::FetchManager::SearchResult> ToolInterface::search(const std::string& keyword, int max_results) {
    return fetch_manager_->search(keyword, max_results);
}

std::vector<StreamFetch::FetchManager::SearchResult> ToolInterface::searchByPlatform(const std::string& keyword, const std::string& platform, int max_results) {
    return fetch_manager_->searchByPlatform(keyword, platform, max_results);
}
