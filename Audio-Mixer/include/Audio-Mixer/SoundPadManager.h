//
// Created by laojk on 2025-11-25.
//

#ifndef DASH_N_BARK_SOUNDPADMANAGER_H
#define DASH_N_BARK_SOUNDPADMANAGER_H
#include <optional>
#include <string>
#include <vector>
#include <Audio-Mixer/AudioBuffer.h>

#include "soci/session.h"

namespace AudioMixer {
    class SoundPadManager {
    public:
        struct SoundEntry {
            int id;
            std::string name;
            std::string tag1;
            std::string tag2;
            std::string path;
            std::string user_id;
            int fav;
        };

        SoundPadManager() = default;
        ~SoundPadManager();

        bool initialize(const std::string& db_path="soundpad.db", const std::string& sound_path="./sounds/");
        void shutdown();

        [[nodiscard]] std::tuple<bool, std::string> saveAudioClip(AudioClip& clip, const std::string& name,
            const std::string& user_id, const std::string& tag1, const std::string& tag2="", bool fav=false) const;
        [[nodiscard]] std::optional<AudioClip> loadAudioClip(int id) const;
        [[nodiscard]] std::optional<AudioClip> loadAudioClip(const std::string& name) const;

        [[nodiscard]] std::optional<std::map<int, std::string>> listTags(int page, int page_size) const;
        [[nodiscard]] int countTags() const;
        [[nodiscard]] std::optional<std::map<int, SoundEntry>> listSounds(int page, int page_size, std::string tag) const;
        [[nodiscard]] int countSounds(const std::string& tag="") const;

    private:
        soci::session *sql{nullptr};
        std::string sound_save_path = "./sounds/";

        static std::optional<AudioClip> checkAndLoadClip(const SoundEntry& entry) ;
    };
} // AudioMixer

#endif //DASH_N_BARK_SOUNDPADMANAGER_H