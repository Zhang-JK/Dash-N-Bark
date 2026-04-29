//
// Created by laojk on 2026-04-28.
//

#include "Audio-Mixer/JoinEffectManager.h"

#include <fstream>

#include "soci/soci.h"
#include "soci/sqlite3/soci-sqlite3.h"
#include "spdlog/spdlog.h"

namespace AudioMixer {
    JoinEffectManager::~JoinEffectManager() {
        shutdown();
    }

    bool JoinEffectManager::initialize(const std::string& db_path) {
        if (sql) {
            spdlog::error("JoinEffectManager already initialized");
            return false;
        }
        bool db_exists;
        {
            std::ifstream f(db_path);
            db_exists = f.good();
        }
        try {
            sql = new soci::session(soci::sqlite3, db_path);
            if (db_exists) {
                int flag = 0;
                *sql << "SELECT count(name) FROM sqlite_master WHERE type='table' AND name='join_effects';",
                        soci::into(flag);
                if (flag == 0) {
                    *sql << "CREATE TABLE join_effects ("
                            "guild_id TEXT NOT NULL, "
                            "user_id TEXT NOT NULL, "
                            "clip_name TEXT NOT NULL, "
                            "PRIMARY KEY (guild_id, user_id));";
                    spdlog::info("created join_effects table in existing db {}", db_path);
                }
            } else {
                *sql << "CREATE TABLE join_effects ("
                        "guild_id TEXT NOT NULL, "
                        "user_id TEXT NOT NULL, "
                        "clip_name TEXT NOT NULL, "
                        "PRIMARY KEY (guild_id, user_id));";
                spdlog::info("created new join_effects database at {}", db_path);
            }
            return true;
        } catch (const soci::soci_error& e) {
            spdlog::error("failed to init join_effects db: {}", e.what());
            delete sql;
            sql = nullptr;
            return false;
        }
    }

    void JoinEffectManager::shutdown() {
        if (sql) {
            delete sql;
            sql = nullptr;
        }
    }

    bool JoinEffectManager::set(const std::string& guild_id, const std::string& user_id,
                                 const std::string& clip_name) const {
        if (!sql) return false;
        try {
            *sql << "INSERT INTO join_effects (guild_id, user_id, clip_name) "
                    "VALUES (:g, :u, :c) "
                    "ON CONFLICT(guild_id, user_id) DO UPDATE SET clip_name = excluded.clip_name;",
                    soci::use(guild_id, "g"), soci::use(user_id, "u"), soci::use(clip_name, "c");
            return true;
        } catch (const soci::soci_error& e) {
            spdlog::error("join_effects set failed: {}", e.what());
            return false;
        }
    }

    bool JoinEffectManager::remove(const std::string& guild_id, const std::string& user_id) const {
        if (!sql) return false;
        try {
            *sql << "DELETE FROM join_effects WHERE guild_id = :g AND user_id = :u;",
                    soci::use(guild_id, "g"), soci::use(user_id, "u");
            return true;
        } catch (const soci::soci_error& e) {
            spdlog::error("join_effects remove failed: {}", e.what());
            return false;
        }
    }

    std::optional<std::string> JoinEffectManager::get(const std::string& guild_id,
                                                      const std::string& user_id) const {
        if (!sql) return std::nullopt;
        try {
            std::string clip_name;
            soci::indicator ind;
            *sql << "SELECT clip_name FROM join_effects WHERE guild_id = :g AND user_id = :u;",
                    soci::into(clip_name, ind), soci::use(guild_id, "g"), soci::use(user_id, "u");
            if (ind == soci::i_ok && !clip_name.empty()) return clip_name;
            return std::nullopt;
        } catch (const soci::soci_error& e) {
            spdlog::error("join_effects get failed: {}", e.what());
            return std::nullopt;
        }
    }

    std::vector<std::pair<std::string, std::string>> JoinEffectManager::listForGuild(
            const std::string& guild_id) const {
        std::vector<std::pair<std::string, std::string>> out;
        if (!sql) return out;
        try {
            soci::rowset<soci::row> rs = (sql->prepare
                << "SELECT user_id, clip_name FROM join_effects WHERE guild_id = :g;",
                soci::use(guild_id, "g"));
            for (const auto& row : rs) {
                out.emplace_back(row.get<std::string>(0), row.get<std::string>(1));
            }
        } catch (const soci::soci_error& e) {
            spdlog::error("join_effects list failed: {}", e.what());
        }
        return out;
    }
} // AudioMixer
