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
            int fav;
        };

        SoundPadManager() = default;
        ~SoundPadManager();

        bool initialize(const std::string& db_path="soundpad.db", const std::string& sound_path="./sounds/");
        void shutdown();

        [[nodiscard]] bool saveAudioClip(const AudioClip& clip, const std::string& name,
            const std::string& tag1, const std::string& tag2="", bool fav=false) const;
        std::optional<AudioClipPtr> loadAudioClip(int id) const;
        std::optional<AudioClipPtr> loadAudioClip(const std::string& name) const;

    private:
        soci::session *sql{nullptr};
        std::string sound_save_path = "./sounds/";

        static std::optional<AudioClipPtr> checkAndLoadClip(const SoundEntry& entry) ;
    };
} // AudioMixer

#endif //DASH_N_BARK_SOUNDPADMANAGER_H