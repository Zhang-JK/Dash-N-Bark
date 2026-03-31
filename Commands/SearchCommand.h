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
        auto platform = std::get<std::string>(event.get_parameter("platform"));
        auto keyword = std::get<std::string>(event.get_parameter("keyword"));
        spdlog::debug("Search command received keyword: {} on platform: {}", keyword, platform);

        event.thinking();

        int max_results = (platform == "bilibili") ? 50 : 20;
        auto results = tool_interface_->searchByPlatform(keyword, platform, max_results);
        if (results.empty()) {
            event.edit_original_response(dpp::message("No search results found for \"" + keyword + "\" on " + platform));
            return;
        }

        std::string uid = random_gen_id();
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            evict_old_sessions();
            Session& session = sessions_[uid];
            session.results = std::move(results);
            session.keyword = keyword;
            session.platform = platform;
            session.current_page = 0;
            session.created_at = std::time(nullptr);
            session.invoker_name = get_user_name_from_event(event);
            session.last_action = "Opened search";
        }

        auto msg = buildSearchMessage(uid, sessions_[uid]);
        event.edit_original_response(msg);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.empty() || ids[0] != "search" || ids.size() < 4) {
            return;
        }

        const std::string& cmd = ids[1];
        auto recv_ts = std::stoll(ids[2]);
        const std::string& uid = ids[3];
        auto now_ts = std::time(nullptr);

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.erase(uid);
            }
            event.edit_original_response(dpp::message("This search interaction has expired."));
            return;
        }

        Session* session_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(uid);
            if (it == sessions_.end()) {
                event.edit_original_response(dpp::message("Search session not found."));
                return;
            }
            session_ptr = &it->second;
        }

        Session& session = *session_ptr;
        std::string user_name = get_user_name_from_event(event);

        if (cmd == "prev") {
            session.current_page--;
            session.last_action = user_name + " went to previous page";
            auto msg = buildSearchMessage(uid, session);
            event.edit_original_response(msg);
        } else if (cmd == "next") {
            session.current_page++;
            session.last_action = user_name + " went to next page";
            auto msg = buildSearchMessage(uid, session);
            event.edit_original_response(msg);
        } else if (cmd == "play") {
            if (ids.size() < 5) {
                return;
            }
            std::string vid = ids[4];
            session.last_action = user_name + " played";

            std::string url;
            if (session.platform == "youtube") {
                url = "https://www.youtube.com/watch?v=" + vid;
            } else {
                url = "https://www.bilibili.com/video/" + vid;
            }

            auto msg = buildSearchMessage(uid, session);
            event.edit_original_response(msg);

            if (!joinVoiceChannel(event, true)) {
                return;
            }

            event.reply("Fetching audio...");
            auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(url, 100);
            if (!tool_res.success || !tool_res.data.has_value()) {
                event.reply("Failed to fetch: " + tool_res.message);
                return;
            }
            event.reply("Streaming " + tool_res.data.value());
        }
    }

    void select(const dpp::select_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() < 4 || ids[0] != "search" || ids[1] != "select") {
            return;
        }

        auto recv_ts = std::stoll(ids[2]);
        const std::string& uid = ids[3];
        auto now_ts = std::time(nullptr);

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.erase(uid);
            }
            event.edit_original_response(dpp::message("This search interaction has expired."));
            return;
        }

        Session* session_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(uid);
            if (it == sessions_.end()) {
                event.reply("Search session not found.");
                return;
            }
            session_ptr = &it->second;
        }

        Session& session = *session_ptr;
        auto selected_values = event.values;

        if (selected_values.empty()) {
            return;
        }

        std::string vid = selected_values[0];
        session.last_action = get_user_name_from_event(event) + " selected";

        std::string url;
        if (session.platform == "youtube") {
            url = "https://www.youtube.com/watch?v=" + vid;
        } else {
            url = "https://www.bilibili.com/video/" + vid;
        }

        auto msg = buildSearchMessage(uid, session);
        event.reply(dpp::ir_update_message, msg);

        if (!joinVoiceChannel(event, true)) {
            return;
        }

        event.reply("Fetching audio...");
        auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(url, 100);
        if (!tool_res.success || !tool_res.data.has_value()) {
            event.reply("Failed to fetch: " + tool_res.message);
            return;
        }
        event.reply("Streaming " + tool_res.data.value());
    }

private:
    struct Session {
        std::vector<StreamFetch::FetchManager::SearchResult> results;
        std::string keyword;
        std::string platform;
        int current_page = 0;
        std::time_t created_at = 0;
        std::string invoker_name;
        std::string last_action;
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

    dpp::message buildSearchMessage(const std::string& uid, const Session& session) {
        int total_results = static_cast<int>(session.results.size());
        int total_pages = (total_results + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
        int start_idx = session.current_page * RESULTS_PER_PAGE;
        int end_idx = std::min(start_idx + RESULTS_PER_PAGE, total_results);

        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));

        dpp::message msg;
        std::string platform_label = (session.platform == "youtube") ? "YouTube" : "Bilibili";
        msg.set_content("**" + platform_label + " Search** by " + session.invoker_name + 
                       "\nLast: " + session.last_action + 
                       "\nKeyword: " + session.keyword);

        dpp::embed embed;
        embed.set_title(platform_label + " Results")
              .set_color(session.platform == "youtube" ? 0xFF0000 : 0x00A1FF);

        std::string description;
        description += "**Page " + std::to_string(session.current_page + 1) + "/" + std::to_string(total_pages) + "**";
        description += " (" + std::to_string(total_results) + " results)\n\n";

        for (int i = start_idx; i < end_idx; ++i) {
            const auto& r = session.results[i];
            description += std::to_string(i + 1) + ". **" + r.title + "**\n";
            description += r.creator + " · " + r.view_count + " views\n";
            description += r.duration + " · " + r.publish_date + "\n\n";
        }

        embed.set_description(description);
        msg.add_embed(embed);

        dpp::component select_row;
        select_row.set_type(dpp::cot_action_row);
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu);
        select_menu.set_id("search::select::" + ts + "::" + uid);
        select_menu.set_placeholder("Select a video to play...");

        for (int i = start_idx; i < end_idx; ++i) {
            const auto& r = session.results[i];
            std::string option_label = std::to_string(i + 1) + ". " + r.title;
            if (option_label.length() > 100) {
                option_label = option_label.substr(0, 97) + "...";
            }
            select_menu.add_select_option(dpp::select_option(option_label, r.bvid_or_vid, 
                r.creator + " | " + r.duration));
        }

        select_row.add_component(select_menu);
        msg.add_component(select_row);

        dpp::component nav_row;
        nav_row.set_type(dpp::cot_action_row);
        
        auto prev_btn = dpp::component()
            .set_type(dpp::cot_button)
            .set_label("<")
            .set_style(dpp::cos_secondary)
            .set_id("search::prev::" + ts + "::" + uid);
        if (session.current_page <= 0) prev_btn.set_disabled(true);
        nav_row.add_component(prev_btn);

        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label(std::to_string(session.current_page + 1) + " / " + std::to_string(total_pages))
                .set_style(dpp::cos_secondary)
                .set_id("search::page::" + ts + "::" + uid)
                .set_disabled(true)
        );

        auto next_btn = dpp::component()
            .set_type(dpp::cot_button)
            .set_label(">")
            .set_style(dpp::cos_secondary)
            .set_id("search::next::" + ts + "::" + uid);
        if (session.current_page >= total_pages - 1) next_btn.set_disabled(true);
        nav_row.add_component(next_btn);

        msg.add_component(nav_row);

        return msg;
    }
};

#endif //DASH_N_BARK_SEARCHCOMMAND_H
