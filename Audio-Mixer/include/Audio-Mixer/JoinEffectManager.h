//
// Created by laojk on 2026-04-28.
//

#ifndef DASH_N_BARK_JOINEFFECTMANAGER_H
#define DASH_N_BARK_JOINEFFECTMANAGER_H

#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "soci/session.h"

namespace AudioMixer {
    class JoinEffectManager {
    public:
        JoinEffectManager() = default;
        ~JoinEffectManager();

        bool initialize(const std::string& db_path = "join_effects.db");
        void shutdown();

        bool set(const std::string& guild_id, const std::string& user_id, const std::string& clip_name) const;
        bool remove(const std::string& guild_id, const std::string& user_id) const;
        [[nodiscard]] std::optional<std::string> get(const std::string& guild_id, const std::string& user_id) const;
        [[nodiscard]] std::vector<std::pair<std::string, std::string>> listForGuild(const std::string& guild_id) const;

    private:
        soci::session* sql{nullptr};
    };
} // AudioMixer

#endif //DASH_N_BARK_JOINEFFECTMANAGER_H
