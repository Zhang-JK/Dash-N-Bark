//
// Created by laojk on 2026-02-05.
//

#ifndef DASH_N_BARK_RECODERSESSION_H
#define DASH_N_BARK_RECODERSESSION_H
#include <chrono>
#include <queue>
#include <string>
#include <fstream>

#include "AudioBuffer.h"

namespace AudioMixer {

    class RecorderSession {
    public:
        RecorderSession(std::string user_id, int duration_seconds, bool keep_data, const std::string& base_path);
        ~RecorderSession();

        void recordAudio(const uint8_t* audio_data, size_t data_size);
        std::optional<AudioClip> streamAudio(int len_ms = 60);
        bool isDone();
        bool checkUserId(const std::string& user_id);

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

        void applyAudioEffects(AudioBuffer& buffer);
    };

} // namespace AudioMixer

#endif //DASH_N_BARK_RECODERSESSION_H