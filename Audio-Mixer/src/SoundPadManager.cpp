//
// Created by laojk on 2025-11-25.
//

#include "Audio-Mixer/SoundPadManager.h"

#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

#include "soci/sqlite3/soci-sqlite3.h"
#include "spdlog/spdlog.h"


namespace AudioMixer {
    SoundPadManager::~SoundPadManager() {
        shutdown();
    }

    bool SoundPadManager::initialize(const std::string& db_path, const std::string& sound_path) {
        // check already initialized
        if (sql) {
            spdlog::error("already initialized");
            return false;
        }
        // create sound save path if not exist
        sound_save_path = sound_path;
        try {
            fs::path p(sound_save_path);
            if (!p.empty() && !fs::exists(p)) {
                if (!fs::create_directories(p)) {
                    spdlog::error("failed to create sound save path: {}", sound_save_path);
                    return false;
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("exception while creating sound save path '{}': {}", sound_save_path, e.what());
            return false;
        }

        // check if file exists
        bool db_exists = false;
        {
            std::ifstream db_file(db_path);
            db_exists = db_file.good();
        }
        try {
            if (db_exists) {
                spdlog::info("db exist, try to load from file...");
                // assert db has table sounds and tags
                sql = new soci::session(soci::sqlite3, db_path);
                int sound_flag = 0, tag_flag = 0;
                *sql << "SELECT count(name) FROM sqlite_master WHERE type='table' AND name='sounds';", soci::into(sound_flag);
                *sql << "SELECT count(name) FROM sqlite_master WHERE type='table' AND name='tags';", soci::into(tag_flag);
                if (tag_flag == 0 || sound_flag == 0) {
                    spdlog::error("db file invalid, missing table sounds or tags");
                    delete sql;
                    sql = nullptr;
                    return false;
                } else {
                    spdlog::info("db loaded successfully");
                    return true;
                }
            }
            sql = new soci::session(soci::sqlite3, db_path);
            // create tables
            *sql << "CREATE TABLE sounds ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT UNIQUE NOT NULL, "
                    "tag1 INTEGER, "
                    "tag2 INTEGER, "
                    "volume REAL NOT NULL DEFAULT 1.0, "
                    "path TEXT, "
                    "fav INTEGER NOT NULL DEFAULT 0, "
                    "binding1 TEXT, "
                    "binding2 TEXT, "
                    "reserved1 TEXT, "
                    "reserved2 TEXT);";
            *sql << "CREATE TABLE tags ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT UNIQUE NOT NULL);";
            spdlog::info("created new soundpad database at {}", db_path);
            return true;
        } catch (const soci::soci_error& e) {
            spdlog::error("failed to create sounds table: {}", e.what());
            delete sql;
            sql = nullptr;
            throw;
        }
    }

    void SoundPadManager::shutdown() {
        if (sql) {
            delete sql;
            sql = nullptr;
        }
    }

    std::tuple<bool, std::string> SoundPadManager::saveAudioClip(AudioClip& clip, const std::string& name,
            const std::string& user_id, const std::string& tag1, const std::string& tag2, bool fav) const {
        if (!sql) {
            spdlog::error("SoundPadManager not initialized");
            return {false, "SoundPadManager not initialized"};
        }
        // save audio data to file
        fs::path file_p = fs::path(sound_save_path) / (name + ".pcm");
        std::string file_path = file_p.string();
        std::ofstream output(file_path, std::ios::out | std::ios::binary);
        if (!output.is_open()) {
            spdlog::error("failed to open file for writing: {}", file_path);
            return {false, "failed to open file for writing: " + file_path};
        }
        output.write(reinterpret_cast<const char*>(clip.getData()), clip.getSize());
        output.close();

        try {
            int tag1_id = 0, tag2_id = 0;
            int fav_int = fav ? 1 : 0;
            if (!tag1.empty()) {
                *sql << "INSERT OR IGNORE INTO tags (name) VALUES (:name);", soci::use(tag1);
                *sql << "SELECT id FROM tags WHERE name = :name;", soci::into(tag1_id), soci::use(tag1);
            }
            if (!tag2.empty()) {
                *sql << "INSERT OR IGNORE INTO tags (name) VALUES (:name);", soci::use(tag2);
                *sql << "SELECT id FROM tags WHERE name = :name;", soci::into(tag2_id), soci::use(tag2);
            }
            *sql << "INSERT INTO sounds (name, tag1, tag2, path, fav, reserved1) "
                    "VALUES (:name, :tag1, :tag2, :path, :fav, :reserved1);",
                    soci::use(name), soci::use(tag1_id), soci::use(tag2_id),
                    soci::use(file_path), soci::use(fav_int), soci::use(user_id);
            spdlog::info("saved audio clip '{}' to database", name);
            return {true, ""};
        } catch (const soci::soci_error& e) {
            spdlog::error("failed to save audio clip to database: {}", e.what());
            return {false, e.what()};
        }
    }

    std::optional<AudioClipPtr> SoundPadManager::loadAudioClip(int id) const {
        // load sound entry from db
        if (!sql) {
            spdlog::error("SoundPadManager not initialized");
            return std::nullopt;
        }
        try {
            SoundEntry entry;
            soci::indicator tag1_ind, tag2_ind;
            *sql << "SELECT id, name, "
                    "(SELECT name FROM tags WHERE id = sounds.tag1) AS tag1, "
                    "(SELECT name FROM tags WHERE id = sounds.tag2) AS tag2, "
                    "path, fav "
                    "FROM sounds WHERE id = :id;",
                    soci::into(entry.id), soci::into(entry.name),
                    soci::into(entry.tag1, tag1_ind), soci::into(entry.tag2, tag2_ind),
                    soci::into(entry.path), soci::into(entry.fav),
                    soci::use(id);
            if (tag1_ind == soci::i_null) {
                entry.tag1 = "";
            }
            if (tag2_ind == soci::i_null) {
                entry.tag2 = "";
            }
            return checkAndLoadClip(entry);
        } catch (const soci::soci_error& e) {
            spdlog::error("failed to load audio clip from database: {}", e.what());
            return std::nullopt;
        }
    }

    std::optional<AudioClipPtr> SoundPadManager::loadAudioClip(const std::string& name) const {
        // load sound entry from db
        if (!sql) {
            spdlog::error("SoundPadManager not initialized");
            return std::nullopt;
        }
        try {
            SoundEntry entry;
            soci::indicator tag1_ind, tag2_ind;
            *sql << "SELECT id, name, "
                    "(SELECT name FROM tags WHERE id = sounds.tag1) AS tag1, "
                    "(SELECT name FROM tags WHERE id = sounds.tag2) AS tag2, "
                    "path, fav "
                    "FROM sounds WHERE name = :name;",
                    soci::into(entry.id), soci::into(entry.name),
                    soci::into(entry.tag1, tag1_ind), soci::into(entry.tag2, tag2_ind),
                    soci::into(entry.path), soci::into(entry.fav),
                    soci::use(name);
            if (tag1_ind == soci::i_null) {
                entry.tag1 = "";
            }
            if (tag2_ind == soci::i_null) {
                entry.tag2 = "";
            }
            return checkAndLoadClip(entry);
        } catch (const soci::soci_error& e) {
            spdlog::error("failed to load audio clip from database: {}", e.what());
            return std::nullopt;
        }
    }

    std::optional<AudioClipPtr> SoundPadManager::checkAndLoadClip(const SoundPadManager::SoundEntry& entry) {
        // check if file exists
        if (!fs::exists(entry.path)) {
            spdlog::error("audio file not found: {}", entry.path);
            return std::nullopt;
        }
        auto clip = std::make_shared<AudioClip>(entry.path, AudioBuffer::PCM_16BIT_STEREO_48K);
        if (!clip->getSize() || clip->getSize() % 4 != 0) {
            spdlog::error("invalid audio clip size or format: {}", entry.path);
            return std::nullopt;
        }
        return clip;
    }



} // AudioMixer