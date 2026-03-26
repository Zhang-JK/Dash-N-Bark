//
// Created by laojk on 2026-01-15.
//

#ifndef DASH_N_BARK_TOOLINTERFACE_H
#define DASH_N_BARK_TOOLINTERFACE_H
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/timed_thread_scheduler.hpp>

#include "Audio-Mixer/SoundPadManager.h"
#include "Audio-Mixer/AudioMixer.h"
#include "Audio-Mixer/RecorderSession.h"
#include "Stream-Fetch/FetchManager.h"


class ToolInterface {
public:
    ToolInterface() = delete;
    ToolInterface(std::string  base, std::shared_ptr<exec::static_thread_pool> pool);
    ~ToolInterface();

    template<typename T = std::optional<std::string>>
    struct ToolInvokeResult {
        bool success;
        int error_code;
        std::string message;
        T data;
    };

    // stream audio (per-guild: guild_id identifies the server)
    ToolInvokeResult<> playAudioClip(const std::string& guild_id, AudioMixer::AudioClip& clip, AudioMixer::AudioMixer::SoundType type);
    ToolInvokeResult<> playAudioFromFile(const std::string& guild_id, std::string path, AudioMixer::AudioMixer::SoundType type, float volume);
    ToolInvokeResult<> fetchAndEnqueuePlaylist(const std::string& guild_id, const std::string& url, int volume);
    ToolInvokeResult<std::optional<std::vector<std::tuple<std::string, int, int>>>> getPlaylist(const std::string& guild_id);
    ToolInvokeResult<std::optional<std::tuple<std::string, int, int>>> getCurrentSong(const std::string& guild_id);
    ToolInvokeResult<> skipCurrentSong(const std::string& guild_id);
    ToolInvokeResult<> clearAllAudio(const std::string& guild_id);
    std::optional<AudioMixer::AudioClip> stepAudioMixer(const std::string& guild_id, size_t step_size);

    // sound pad (shared across all servers)
    ToolInvokeResult<std::optional<AudioMixer::AudioClip>> fetchSoundFromUrl(const std::string& url);
    ToolInvokeResult<> addToSoundpad(AudioMixer::AudioClip& clip, const std::string& name,
        const std::string& user_id, const std::string& tag1, const std::string& tag2, bool fav);
    ToolInvokeResult<std::optional<std::map<int, std::string>>> listTagsPaged(int page, int page_size, int& total_pages);
    ToolInvokeResult<std::optional<std::map<int, std::string>>> listSoundpadClipsPaged(
                                int page, int page_size, int& total_pages, const std::string& tag = "");
    ToolInvokeResult<> playSoundpadClip(const std::string& guild_id, int clip_id, int volume);

    // recording (per-guild)
    ToolInvokeResult<> initRecordingService(const std::string& guild_id, std::string user_id, int duration_seconds, AudioMixer::VoiceChanger::VoicePreset voice_preset = AudioMixer::VoiceChanger::VoicePreset::Baby);
    void recordingVoiceCallback(std::vector<uint8_t> data, size_t size, const std::string& guild_id, const std::string& user_id);

private:
    std::string base_path_;
    std::shared_ptr<exec::static_thread_pool> ppool_;
    exec::timed_thread_context timed_thread_context_;
    std::shared_ptr<StreamFetch::FetchManager> fetch_manager_;
    std::shared_ptr<AudioMixer::SoundPadManager> sound_pad_manager_;

    // Per-guild audio mixers: guild_id -> AudioMixer
    std::map<std::string, std::shared_ptr<AudioMixer::AudioMixer>> guild_mixers_;
    std::mutex guild_mixers_mutex_;

    // Per-guild recording sessions: "guild_id::user_id" -> RecorderSession
    std::map<std::string, std::shared_ptr<AudioMixer::RecorderSession>> recorder_sessions_;
    std::mutex recorder_mutex_;

    std::shared_ptr<AudioMixer::AudioMixer> getOrCreateMixer(const std::string& guild_id);
};


#endif //DASH_N_BARK_TOOLINTERFACE_H