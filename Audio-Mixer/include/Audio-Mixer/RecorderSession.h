//
// Created by laojk on 2026-02-05.
//

#ifndef DASH_N_BARK_RECORDERSESSION_H
#define DASH_N_BARK_RECORDERSESSION_H
#include <atomic>
#include <chrono>
#include <mutex>
#include <queue>
#include <string>
#include <fstream>

#include "AudioBuffer.h"
#include "VoiceChanger.h"

namespace AudioMixer {

    class RecorderSession {
    public:
        RecorderSession(std::string user_id, int duration_seconds, bool keep_data, const std::string& base_path);
        ~RecorderSession();

        void recordAudio(const uint8_t* audio_data, size_t data_size);
        std::optional<AudioClip> streamAudio(int len_ms = 60);
        bool isTimeOut();
        std::string getUserId() const;
        bool checkUserId(const std::string& user_id);
        void shutdown();
        bool isShuttingDown();

        void setVoiceChangerEnabled(bool enabled);
        void setVoiceChangerPreset(VoiceChanger::VoicePreset preset);
        void setVoiceChangerPitch(double pitch);

    private:
        std::string user_id_;
        int target_duration_ms_;
        std::chrono::time_point<std::chrono::steady_clock> start_time_;
        bool keep_data_;
        int target_delay_ms_;
        std::string file_to_dump_;
        std::ofstream dump_file_;
        size_t processing_audio_pointer_;
        std::mutex mutex_;
        std::queue<
            std::tuple<std::chrono::time_point<std::chrono::steady_clock>, AudioBufferPtr>> audio_queue_;
        std::atomic<bool> is_shutting_down_;
        int headBufferPointer_;

        std::shared_ptr<VoiceChanger> voice_changer_;

        void applyAudioEffects(AudioBuffer& buffer);
    };

} // namespace AudioMixer

#endif //DASH_N_BARK_RECORDERSESSION_H