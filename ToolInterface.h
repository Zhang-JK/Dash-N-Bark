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
#include "Audio-Mixer/JoinEffectManager.h"
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

    // stream audio
    ToolInvokeResult<> playAudioClip(AudioMixer::AudioClip& clip, AudioMixer::AudioMixer::SoundType type);
    ToolInvokeResult<> playAudioFromFile(std::string path, AudioMixer::AudioMixer::SoundType type, float volume);
    ToolInvokeResult<> fetchAndEnqueuePlaylist(const std::string& url, int volume);
    ToolInvokeResult<std::optional<std::vector<std::tuple<std::string, int, int>>>> getPlaylist();
    ToolInvokeResult<std::optional<std::tuple<std::string, int, int>>> getCurrentSong();
    ToolInvokeResult<> skipCurrentSong();
    ToolInvokeResult<> clearAllAudio();
    [[nodiscard]] std::optional<AudioMixer::AudioClip> stepAudioMixer(size_t step_size) const;

    // sound pad
    ToolInvokeResult<std::optional<AudioMixer::AudioClip>> fetchSoundFromUrl(const std::string& url);
    ToolInvokeResult<> addToSoundpad(AudioMixer::AudioClip& clip, const std::string& name,
        const std::string& user_id, const std::string& tag1, const std::string& tag2, bool fav);
    ToolInvokeResult<std::optional<std::map<int, std::string>>> listTagsPaged(int page, int page_size, int& total_pages);
    ToolInvokeResult<std::optional<std::map<int, std::string>>> listSoundpadClipsPaged(
                                int page, int page_size, int& total_pages, const std::string& tag = "");
    ToolInvokeResult<> playSoundpadClip(int clip_id, int volume);
    ToolInvokeResult<> playSoundpadClipByName(const std::string& clip_name, int volume);
    [[nodiscard]] std::vector<std::pair<int, std::string>> searchSoundpadByName(
            const std::string& query, int limit) const;

    // join effect (per guild + user -> soundpad clip name)
    ToolInvokeResult<> setJoinEffect(const std::string& guild_id, const std::string& user_id,
                                      const std::string& clip_name);
    ToolInvokeResult<> removeJoinEffect(const std::string& guild_id, const std::string& user_id);
    [[nodiscard]] std::optional<std::string> getJoinEffect(const std::string& guild_id,
                                                            const std::string& user_id) const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> listJoinEffects(
            const std::string& guild_id) const;

    // recording
    ToolInvokeResult<> initRecordingService(std::string user_id, int duration_seconds, AudioMixer::VoiceChanger::VoicePreset voice_preset = AudioMixer::VoiceChanger::VoicePreset::Baby);
    void recordingVoiceCallback(std::vector<uint8_t> data, size_t size, const std::string& user_id);

    // search
    std::vector<StreamFetch::FetchManager::SearchResult> search(const std::string& keyword, int max_results = 10);
    std::vector<StreamFetch::FetchManager::SearchResult> searchByPlatform(const std::string& keyword, const std::string& platform, int max_results = 10);

private:
    std::string base_path_;
    std::shared_ptr<exec::static_thread_pool> ppool_;
    exec::timed_thread_context timed_thread_context_;
    std::shared_ptr<StreamFetch::FetchManager> fetch_manager_;
    std::shared_ptr<AudioMixer::AudioMixer> audio_mixer_;
    std::shared_ptr<AudioMixer::SoundPadManager> sound_pad_manager_;
    std::shared_ptr<AudioMixer::JoinEffectManager> join_effect_manager_;

    std::map<std::string, std::shared_ptr<AudioMixer::RecorderSession>> recorder_sessions_;
    // todo: better change to one for each rec session
    std::mutex recorder_mutex_;
};


#endif //DASH_N_BARK_TOOLINTERFACE_H