//
// Created by laojk on 2/1/26.
//

#ifndef DASH_N_BARK_SOUNDPADCOMMAND_H
#define DASH_N_BARK_SOUNDPADCOMMAND_H

#include <mutex>
#include "CommandBase.h"

class SoundpadCommand : public CommandBase {
private:
    struct PagedComponent {
        int current_page;
        int total_pages;
        std::string tag;
        bool by_tag;
    };

    // Per-invocation state for a single /soundpad session.
    // Keyed by a random UID embedded in every button/modal custom_id so that
    // concurrent sessions (multiple users, multiple /soundpad messages) are
    // completely independent and cannot interfere with each other.
    struct SoundpadSession {
        std::map<int, std::string> mappings;  // current page: clip_id -> clip_name
        PagedComponent pagination;             // pagination state for the current view
        int volume = 100;                      // current playback volume (10-100)
        std::time_t created_at = 0;           // unix timestamp used for TTL expiry
        std::string invoker_name;              // display name of the user who ran /soundpad
        std::string last_action;              // human-readable description of the most recent interaction
    };

    static constexpr int MAX_SESSIONS = 10;          // hard cap on concurrent sessions
    static constexpr int SESSION_EXPIRE_SECS = 180;  // buttons expire after 3 min (Discord's default TTL)
    static constexpr int SESSION_CLEANUP_SECS = SESSION_EXPIRE_SECS * 1.2;  // GC threshold
    // Component ID for the page-number text input inside the Jump modal.
    // Discord modals only support text inputs, so the value is validated server-side.
    static constexpr const char* JUMP_PAGE_COMPONENT_ID = "page_number";

    std::map<std::string, SoundpadSession> sessions_;
    mutable std::mutex sessions_mutex_;

    #ifdef NDEBUG
    static constexpr int PAGE_SIZE = 15;
    #else
    static constexpr int PAGE_SIZE = 3;
    #endif

    static auto random_gen_id(int len=4) -> std::string {
        static constexpr  char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        thread_local static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);
        std::string s; s.reserve(len);
        for (int i = 0; i < len; ++i) s.push_back(chars[dist(rng)]);
        return s;
    }

    // Build the header line shown in every soundpad message so the channel can
    // always see who owns the session and what the most recent interaction was.
    // Format: "Soundpad (by <invoker>)\nLast: <action>"
    [[nodiscard]] static std::string build_status_line(const SoundpadSession& session) {
        std::string line = "Soundpad";
        if (!session.invoker_name.empty()) {
            line += " (by " + session.invoker_name + ")";
        }
        if (!session.last_action.empty()) {
            line += "\nLast: " + session.last_action;
        }
        return line;
    }

    // Remove sessions older than SESSION_CLEANUP_SECS, then evict the oldest
    // entry if the hard cap MAX_SESSIONS is still exceeded.
    // Must be called with sessions_mutex_ held.
    void evict_old_sessions() {
        auto now = std::time(nullptr);
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (now - it->second.created_at >= SESSION_CLEANUP_SECS) {
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

    [[nodiscard]] std::vector<dpp::component> build_soundpad_component(
            const SoundpadSession& session, const std::string& uid,
            bool isTag = false, int items_per_row = 5) const {
        if (session.mappings.empty()) {
            return {};
        }

        if (items_per_row <= 0) items_per_row = 3;
        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
        auto page = session.pagination.current_page;
        std::vector<dpp::component> rows;

        // Volume row: [-] [ volume ] [+]
        dpp::component vol_row;
        vol_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("-")
                .set_style(dpp::cos_secondary)
                .set_disabled(session.volume <= 10)
                .set_id("soundpad::vol_down::" + std::to_string(session.volume) + "::" + ts + "::" + uid)
        );
        vol_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Vol " + std::to_string(session.volume))
                .set_style(dpp::cos_secondary)
                .set_id("soundpad::vol_display::" + std::to_string(session.volume) + "::" + ts + "::" + uid)
                .set_disabled(true)
        );
        vol_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("+")
                .set_style(dpp::cos_secondary)
                .set_disabled(session.volume >= 100)
                .set_id("soundpad::vol_up::" + std::to_string(session.volume) + "::" + ts + "::" + uid)
        );
        rows.push_back(vol_row);

        // Mapping buttons: paginated, up to PAGE_SIZE per page, configurable items per row
        dpp::component row;
        int in_row = 0;
        int added = 0;
        const int per_page = PAGE_SIZE;

        for (const auto &entry : session.mappings) {
            if (added >= per_page) break;

            const std::string &label = entry.second;
            int id = entry.first;

            row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label(label)
                    .set_style(dpp::cos_primary)
                    .set_id("soundpad::" + std::string(isTag ? "tag" : "play") +
                            "::" + (isTag ? label : std::to_string(id)) +
                            "::" + ts + "::" + uid)
            );

            ++in_row;
            ++added;
            if (in_row == items_per_row) {
                rows.push_back(row);
                row = dpp::component();
                in_row = 0;
            }
        }
        if (in_row > 0) {
            rows.push_back(row);
        }

        // Navigation row: [<] [Page X/Y] [>] [Jump]
        dpp::component nav_row;
        dpp::component prev_btn;
        prev_btn.set_type(dpp::cot_button)
                .set_label("<")
                .set_style(dpp::cos_secondary)
                .set_id("soundpad::page_prev::" + std::to_string(page) + "::" + ts + "::" + uid);
        if (page <= 0) prev_btn.set_disabled(true);
        nav_row.add_component(prev_btn);

        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Page " + std::to_string(page + 1) +
                        " / " + std::to_string(session.pagination.total_pages))
                .set_style(dpp::cos_secondary)
                .set_id("soundpad::page_display::" + std::to_string(page) + "::" + ts + "::" + uid)
                .set_disabled(true)
        );

        dpp::component next_btn;
        next_btn.set_type(dpp::cot_button)
                .set_label(">")
                .set_style(dpp::cos_secondary)
                .set_id("soundpad::page_next::" + std::to_string(page) + "::" + ts + "::" + uid);
        if (page >= session.pagination.total_pages - 1) next_btn.set_disabled(true);
        nav_row.add_component(next_btn);

        // Jump to page button (only shown when there are multiple pages)
        if (session.pagination.total_pages > 1) {
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Jump")
                    .set_style(dpp::cos_secondary)
                    .set_id("soundpad::page_jump::0::" + ts + "::" + uid)
            );
        }

        rows.push_back(nav_row);

        return rows;
    }

    [[nodiscard]] dpp::message build_soundpad_message(
            dpp::snowflake channel_id, const SoundpadSession& session, const std::string& uid,
            const std::string& content, bool is_tag = false) const {
        dpp::message msg(channel_id, content);
        for (auto &comp : build_soundpad_component(session, uid, is_tag)) {
            msg.add_component(comp);
        }
        return msg;
    }

    // Fetches the current page data and updates the session, then sends the reply.
    // session.last_action should be updated before calling this.
    // Must be called with sessions_mutex_ held.
    void update_button_page(const dpp::button_click_t &event,
                            SoundpadSession& session, const std::string& uid) {
        decltype(tool_interface_->listSoundpadClipsPaged(0, PAGE_SIZE,
            session.pagination.total_pages, "")) res;
        bool is_tag = session.pagination.by_tag;
        if (is_tag) {
            res = tool_interface_->listTagsPaged(
                                    session.pagination.current_page, PAGE_SIZE,
                                    session.pagination.total_pages);
        } else {
            res = tool_interface_->listSoundpadClipsPaged(
                                    session.pagination.current_page, PAGE_SIZE,
                                    session.pagination.total_pages, session.pagination.tag);
        }
        if (!res.success || !res.data.has_value() || session.pagination.total_pages == 0) {
            event.reply(dpp::ir_update_message, "Fetch data failed");
            sessions_.erase(uid);
            return;
        }
        session.mappings = res.data.value();
        auto msg = build_soundpad_message(event.command.channel_id, session, uid,
            build_status_line(session), is_tag);
        event.reply(dpp::ir_update_message, msg);
    }

    // Fetches the current page data, updates the session, then edits the original response.
    // Used after a modal submission. Must be called with sessions_mutex_ held.
    void update_page_edit_response(const dpp::form_submit_t &event,
                                   SoundpadSession& session, const std::string& uid) {
        decltype(tool_interface_->listSoundpadClipsPaged(0, PAGE_SIZE,
            session.pagination.total_pages, "")) res;
        bool is_tag = session.pagination.by_tag;
        if (is_tag) {
            res = tool_interface_->listTagsPaged(
                                    session.pagination.current_page, PAGE_SIZE,
                                    session.pagination.total_pages);
        } else {
            res = tool_interface_->listSoundpadClipsPaged(
                                    session.pagination.current_page, PAGE_SIZE,
                                    session.pagination.total_pages, session.pagination.tag);
        }
        if (!res.success || !res.data.has_value() || session.pagination.total_pages == 0) {
            event.edit_original_response(dpp::message("Fetch data failed"));
            sessions_.erase(uid);
            return;
        }
        session.mappings = res.data.value();
        auto msg = build_soundpad_message(event.command.channel_id, session, uid,
            build_status_line(session), is_tag);
        event.edit_original_response(msg);
    }

public:
    SoundpadCommand() = delete;

    SoundpadCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto use_tag = std::holds_alternative<bool>(event.get_parameter("by_tag"))
                ? std::get<bool>(event.get_parameter("by_tag")) : false;

        int local_total_pages = 0;
        decltype(tool_interface_->listSoundpadClipsPaged(0, PAGE_SIZE, local_total_pages, "")) res;
        if (use_tag) {
            res = tool_interface_->listTagsPaged(0, PAGE_SIZE, local_total_pages);
        } else {
            res = tool_interface_->listSoundpadClipsPaged(0, PAGE_SIZE, local_total_pages, "");
        }
        if (!res.success || !res.data.has_value() || local_total_pages == 0) {
            event.reply("Soundpad empty or unavailable.");
            co_return;
        }

        std::string uid;
        dpp::message msg;
        {
            std::lock_guard<std::mutex> lg(sessions_mutex_);
            uid = random_gen_id();
            evict_old_sessions();
            SoundpadSession& session = sessions_[uid];
            session.mappings = res.data.value();
            session.pagination = {0, local_total_pages, "", use_tag};
            session.volume = 100;
            session.created_at = std::time(nullptr);
            session.invoker_name = get_user_name_from_event(event);
            session.last_action = "Opened soundpad" + std::string(use_tag ? " tag view" : "");

            msg = build_soundpad_message(event.command.channel_id, session, uid,
                use_tag ? "Select your tag:" : build_status_line(session), use_tag);
        }

        event.reply(msg);
        co_return;
    }

    exec::task<void> button(dpp::button_click_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 5 || ids[0] != "soundpad") {
            event.reply("Invalid button ID for soundpad");
            co_return;
        }
        auto recv_ts = std::stoll(ids[3]);
        auto now_ts = std::time(nullptr);
        const std::string& uid = ids[4];
        const std::string& cmd = ids[1];
        const std::string& cmd_param = ids[2];

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            event.reply(dpp::ir_update_message, "This soundpad interaction has expired.");
            std::lock_guard<std::mutex> lg(sessions_mutex_);
            sessions_.erase(uid);
            co_return;
        }

        // play branch needs to await joinVoiceChannel — handle without holding mutex across co_await.
        if (cmd == "play") {
            int clip_id;
            try {
                clip_id = std::stoi(cmd_param);
            } catch (...) {
                event.reply(dpp::ir_update_message, "Invalid clip ID.");
                co_return;
            }
            int vol = 100;
            bool is_tag = false;
            std::string clip_name;
            dpp::message msg;
            {
                std::unique_lock<std::mutex> lock(sessions_mutex_);
                auto it = sessions_.find(uid);
                if (it == sessions_.end()) {
                    lock.unlock();
                    event.reply(dpp::ir_update_message, "This soundpad interaction is no longer valid.");
                    co_return;
                }
                SoundpadSession& session = it->second;
                if (session.mappings.find(clip_id) == session.mappings.end()) {
                    lock.unlock();
                    event.reply(dpp::ir_update_message, "Clip not found.");
                    co_return;
                }
                vol = session.volume;
                is_tag = session.pagination.by_tag;
                clip_name = session.mappings.at(clip_id);
                session.last_action = get_user_name_from_event(event) + " played " + clip_name;
                msg = build_soundpad_message(event.command.channel_id, session, uid,
                                                      build_status_line(session), is_tag);
            }

            if (!co_await joinVoiceChannel(event, true)) {
                co_return;
            }
            auto res = tool_interface_->playSoundpadClip(clip_id, vol);
            if (!res.success) {
                event.edit_original_response(dpp::message(
                    "Failed to play clip ID: " + std::to_string(clip_id)));
                co_return;
            }
            event.edit_original_response(msg);
            co_return;
        }

        std::unique_lock<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(uid);
        if (it == sessions_.end()) {
            lock.unlock();
            event.reply(dpp::ir_update_message, "This soundpad interaction is no longer valid.");
            co_return;
        }
        SoundpadSession& session = it->second;

        if (cmd == "vol_down") {
            session.volume = std::max(10, session.volume - 10);
            auto msg = build_soundpad_message(event.command.channel_id, session, uid,
                build_status_line(session), session.pagination.by_tag);
            lock.unlock();
            event.reply(dpp::ir_update_message, msg);
        } else if (cmd == "vol_up") {
            session.volume = std::min(100, session.volume + 10);
            auto msg = build_soundpad_message(event.command.channel_id, session, uid,
                build_status_line(session), session.pagination.by_tag);
            lock.unlock();
            event.reply(dpp::ir_update_message, msg);
        } else if (cmd == "page_prev") {
            session.pagination.current_page = std::max(0, session.pagination.current_page - 1);
            update_button_page(event, session, uid);
        } else if (cmd == "page_next") {
            session.pagination.current_page = std::min(
                session.pagination.total_pages - 1, session.pagination.current_page + 1);
            update_button_page(event, session, uid);
        } else if (cmd == "page_jump") {
            int total = session.pagination.total_pages;
            lock.unlock();
            auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
            dpp::interaction_modal_response modal(
                "soundpad::jump_modal::" + ts + "::" + uid,
                "Jump to Page"
            );
            modal.add_component(
                dpp::component()
                    .set_type(dpp::cot_text)
                    .set_label("Page Number (1 - " + std::to_string(total) + ")")
                    .set_id(JUMP_PAGE_COMPONENT_ID)
                    .set_placeholder("Enter a page number")
                    .set_required(true)
                    .set_min_length(1)
                    .set_max_length(5)
                    .set_text_style(dpp::text_short)
            );
            event.dialog(modal);
        } else if (cmd == "tag") {
            session.pagination.current_page = 0;
            session.pagination.tag = cmd_param;
            session.pagination.by_tag = false;
            update_button_page(event, session, uid);
        } else {
            lock.unlock();
            event.reply(dpp::ir_update_message, "Unknown command");
        }
        co_return;
    }

    exec::task<void> form_submit(dpp::form_submit_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 4 || ids[0] != "soundpad" || ids[1] != "jump_modal") {
            event.reply("Invalid form submit for soundpad");
            co_return;
        }
        auto recv_ts = std::stoll(ids[2]);
        auto now_ts = std::time(nullptr);
        const std::string& uid = ids[3];

        event.reply(dpp::ir_deferred_update_message, "");

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            event.edit_original_response(dpp::message("This soundpad interaction has expired."));
            std::lock_guard<std::mutex> lg(sessions_mutex_);
            sessions_.erase(uid);
            co_return;
        }

        std::string input;
        if (event.components.empty() || event.components[0].custom_id != JUMP_PAGE_COMPONENT_ID) {
            event.edit_original_response(dpp::message("Failed to read page number."));
            co_return;
        }
        try {
            input = std::get<std::string>(event.components[0].value);
        } catch (...) {
            event.edit_original_response(dpp::message("Failed to read page number."));
            co_return;
        }

        int requested_page;
        try {
            requested_page = std::stoi(input) - 1;
        } catch (...) {
            event.edit_original_response(dpp::message("Invalid page number: \"" + input + "\""));
            co_return;
        }

        std::unique_lock<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(uid);
        if (it == sessions_.end()) {
            lock.unlock();
            event.edit_original_response(dpp::message("This soundpad interaction is no longer valid."));
            co_return;
        }
        SoundpadSession& session = it->second;

        requested_page = std::max(0, std::min(session.pagination.total_pages - 1, requested_page));
        session.pagination.current_page = requested_page;

        update_page_edit_response(event, session, uid);
        co_return;
    }

};

#endif //DASH_N_BARK_SOUNDPADCOMMAND_H
