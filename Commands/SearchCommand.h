//
// Created by laojk on 2026-03-30.
//

#ifndef DASH_N_BARK_SEARCHCOMMAND_H
#define DASH_N_BARK_SEARCHCOMMAND_H

#include "CommandBase.h"
#include "Stream-Fetch/FetchManager.h"
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include <random>

class SearchCommand : public CommandBase {
public:
    SearchCommand() = delete;

    explicit SearchCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto keyword = std::get<std::string>(event.get_parameter("keyword"));
        spdlog::debug("Search command received keyword: {}", keyword);

        event.thinking();

        constexpr int max_per_platform = 50;
        auto all_results = tool_interface_->search(keyword, max_per_platform);
        if (all_results.empty()) {
            event.edit_original_response(dpp::message("No search results found for \"" + keyword + "\""));
            return;
        }

        std::string session_id = random_gen_id();
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            Session& session = sessions_[session_id];
            session.results = std::move(all_results);
            session.keyword = keyword;
            session.current_page = 0;
            session.created_at = std::time(nullptr);
        }

        auto msg = buildSearchResultMessage(session_id, sessions_[session_id]);
        event.edit_original_response(msg);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto parts = parseButtonId(event.custom_id);
        if (parts.empty()) return;

        if (parts[0] == "search_play") {
            if (parts.size() < 3) {
                event.reply(dpp::message("Invalid button data"));
                return;
            }

            std::string platform = parts[1];
            std::string url_or_bvid = parts[2];

            if (!joinVoiceChannel(event, true)) {
                return;
            }

            std::string url;
            if (platform == "youtube") {
                url = "https://www.youtube.com/watch?v=" + url_or_bvid;
            } else {
                url = "https://www.bilibili.com/video/" + url_or_bvid;
            }

            event.edit_original_response(dpp::message("Fetching audio from " + platform + "..."));
            auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(url, 100);
            if (!tool_res.success || !tool_res.data.has_value()) {
                event.edit_original_response(dpp::message("Failed to fetch with error code " +
                            std::to_string(tool_res.error_code) + ": " + tool_res.message));
                return;
            }
            event.edit_original_response(dpp::message("Streaming " + tool_res.data.value()));
        } else if (parts[0] == "search_page") {
            if (parts.size() < 3) return;

            std::string session_id = parts[2];
            int direction = std::stoi(parts[1]);

            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                event.reply(dpp::message("Search session expired. Please search again."));
                return;
            }

            Session& session = it->second;
            session.current_page += direction;

            auto msg = buildSearchResultMessage(session_id, session);
            event.edit_original_response(msg);
        }
    }

private:
    struct Session {
        std::vector<StreamFetch::FetchManager::SearchResult> results;
        std::string keyword;
        int current_page = 0;
        std::time_t created_at = 0;
    };

    static constexpr int RESULTS_PER_PAGE = 5;
    static constexpr int MAX_SESSIONS = 10;
    static constexpr int SESSION_EXPIRE_SECS = 300;

    std::map<std::string, Session> sessions_;
    mutable std::mutex sessions_mutex_;

    static std::string random_gen_id(int len = 6) {
        static constexpr char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static thread_local std::mt19937_64 rng{static_cast<std::mt19937_64::result_type>(std::random_device{}())};
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) s.push_back(chars[dist(rng)]);
        return s;
    }

    void evict_old_sessions() {
        auto now = std::time(nullptr);
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (now - it->second.created_at >= SESSION_EXPIRE_SECS) {
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
        if (sessions_.size() >= MAX_SESSIONS) {
            auto oldest = sessions_.begin();
            for (auto it = std::next(oldest); it != sessions_.end(); ++it) {
                if (it->second.created_at < oldest->second.created_at) {
                    oldest = it;
                }
            }
            sessions_.erase(oldest);
        }
    }

    dpp::message buildSearchResultMessage(const std::string& session_id, const Session& session) {
        int total_pages = (session.results.size() + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
        int start_idx = session.current_page * RESULTS_PER_PAGE;
        int end_idx = std::min(start_idx + RESULTS_PER_PAGE, static_cast<int>(session.results.size()));

        dpp::embed embed;
        embed.set_title("Search: " + session.keyword)
              .set_description("Page " + std::to_string(session.current_page + 1) + "/" + std::to_string(total_pages) + 
                               " (" + std::to_string(session.results.size()) + " results)")
              .set_color(0x00D4FF);

        std::string description;
        std::vector<dpp::component> all_rows;
        dpp::component current_row;

        for (int i = start_idx; i < end_idx; ++i) {
            const auto& r = session.results[i];
            int display_num = i + 1;

            description += "**" + std::to_string(display_num) + ". " + r.title + "**\n";
            description += r.creator + " | " + r.duration + " | " + r.view_count + " views | " + r.publish_date + "\n";
            description += "[" + r.platform + "](" + r.url + ")\n\n";

            dpp::component button;
            button.set_type(dpp::cot_button)
                  .set_label(r.platform == "youtube" ? "Play" : "播放")
                  .set_style(dpp::cos_primary)
                  .set_id("search_play::" + r.platform + "::" + r.bvid_or_vid);

            current_row.add_component(button);

            if (current_row.components.size() >= 5) {
                current_row.set_type(dpp::cot_action_row);
                all_rows.push_back(current_row);
                current_row = dpp::component();
            }
        }

        if (!current_row.components.empty()) {
            current_row.set_type(dpp::cot_action_row);
            all_rows.push_back(current_row);
        }

        dpp::component nav_row;
        if (session.current_page > 0) {
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("<")
                    .set_style(dpp::cos_secondary)
                    .set_id("search_page::-1::" + session_id)
            );
        }
        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label(std::to_string(session.current_page + 1) + "/" + std::to_string(total_pages))
                .set_style(dpp::cos_secondary)
                .set_id("search_page::0::" + session_id)
                .set_disabled(true)
        );
        if (session.current_page < total_pages - 1) {
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label(">")
                    .set_style(dpp::cos_secondary)
                    .set_id("search_page::1::" + session_id)
            );
        }
        nav_row.set_type(dpp::cot_action_row);
        all_rows.push_back(nav_row);

        embed.set_description(description);

        dpp::message msg;
        msg.add_embed(embed);
        for (auto& row : all_rows) {
            msg.add_component(row);
        }

        return msg;
    }
};

#endif //DASH_N_BARK_SEARCHCOMMAND_H
