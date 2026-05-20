//
// Created by laojk on 2026-04-28.
//

#ifndef DASH_N_BARK_JOINEFFECTCOMMAND_H
#define DASH_N_BARK_JOINEFFECTCOMMAND_H

#include <mutex>
#include "CommandBase.h"

class JoinEffectCommand : public CommandBase {
private:
    // Per-invocation state for a single /joineffect picker session.
    // The soundpad clip catalog can be larger than Discord's 25-choice
    // autocomplete cap, so we mirror SoundpadCommand's paginated button UI
    // instead of relying on a slash-option dropdown.
    struct PickerSession {
        std::map<int, std::string> mappings;   // current page: clip_id -> clip_name
        int current_page = 0;
        int total_pages = 0;
        std::string target_user_id;            // the user whose join effect we're editing
        // Existing binding shown in the header so the invoker can see what's
        // already configured. Empty string = no binding. Resolved once when
        // the picker opens and not refreshed on page nav (an updated binding
        // by another concurrent picker would be a rare race we accept).
        std::string current_binding;
        std::time_t created_at = 0;
        std::string invoker_name;              // who ran /joineffect
        std::string last_action;               // human-readable description
    };

    static constexpr int MAX_SESSIONS = 3;
    static constexpr int SESSION_EXPIRE_SECS = 180;
    static constexpr int SESSION_CLEANUP_SECS = SESSION_EXPIRE_SECS * 1.2;
    static constexpr const char* JUMP_PAGE_COMPONENT_ID = "page_number";

    std::map<std::string, PickerSession> sessions_;
    mutable std::mutex sessions_mutex_;

    #ifdef NDEBUG
    static constexpr int PAGE_SIZE = 15;
    #else
    static constexpr int PAGE_SIZE = 3;
    #endif

    static auto random_gen_id(int len=4) -> std::string {
        static constexpr char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        thread_local static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);
        std::string s; s.reserve(len);
        for (int i = 0; i < len; ++i) s.push_back(chars[dist(rng)]);
        return s;
    }

    [[nodiscard]] static std::string build_status_line(const PickerSession& session) {
        std::string line = "Pick a join effect for <@" + session.target_user_id + ">";
        if (!session.invoker_name.empty()) {
            line += " (by " + session.invoker_name + ")";
        }
        line += "\nCurrent: ";
        line += session.current_binding.empty()
                    ? std::string("*(none)*")
                    : ("`" + session.current_binding + "`");
        if (!session.last_action.empty()) {
            line += "\nLast: " + session.last_action;
        }
        return line;
    }

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

    [[nodiscard]] std::vector<dpp::component> build_picker_component(
            const PickerSession& session, const std::string& uid,
            int items_per_row = 5) const {
        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
        auto page = session.current_page;
        std::vector<dpp::component> rows;

        if (items_per_row <= 0) items_per_row = 3;

        // Clip buttons (one per soundpad entry on the current page).
        dpp::component row;
        int in_row = 0;
        int added = 0;
        const int per_page = PAGE_SIZE;
        for (const auto& entry : session.mappings) {
            if (added >= per_page) break;
            const std::string& label = entry.second;
            int id = entry.first;
            row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label(label)
                    .set_style(dpp::cos_primary)
                    .set_id("joineffect::bind::" + std::to_string(id) +
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
                .set_id("joineffect::page_prev::" + std::to_string(page) + "::" + ts + "::" + uid);
        if (page <= 0) prev_btn.set_disabled(true);
        nav_row.add_component(prev_btn);

        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Page " + std::to_string(page + 1) +
                        " / " + std::to_string(session.total_pages))
                .set_style(dpp::cos_secondary)
                .set_id("joineffect::page_display::" + std::to_string(page) + "::" + ts + "::" + uid)
                .set_disabled(true)
        );

        dpp::component next_btn;
        next_btn.set_type(dpp::cot_button)
                .set_label(">")
                .set_style(dpp::cos_secondary)
                .set_id("joineffect::page_next::" + std::to_string(page) + "::" + ts + "::" + uid);
        if (page >= session.total_pages - 1) next_btn.set_disabled(true);
        nav_row.add_component(next_btn);

        if (session.total_pages > 1) {
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Jump")
                    .set_style(dpp::cos_secondary)
                    .set_id("joineffect::page_jump::0::" + ts + "::" + uid)
            );
        }
        rows.push_back(nav_row);

        // Bottom row: a single "Remove binding" button so the user can clear
        // the current binding without typing.
        dpp::component remove_row;
        remove_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Remove binding")
                .set_style(dpp::cos_danger)
                .set_id("joineffect::unbind::0::" + ts + "::" + uid)
        );
        rows.push_back(remove_row);

        return rows;
    }

    [[nodiscard]] dpp::message build_picker_message(
            dpp::snowflake channel_id, const PickerSession& session, const std::string& uid,
            const std::string& content) const {
        dpp::message msg(channel_id, content);
        for (auto& comp : build_picker_component(session, uid)) {
            msg.add_component(comp);
        }
        return msg;
    }

    // Reload the current page from the soundpad and emit an update_message reply.
    // Must be called with sessions_mutex_ held.
    void update_button_page(const dpp::button_click_t& event,
                            PickerSession& session, const std::string& uid) {
        auto res = tool_interface_->listSoundpadClipsPaged(
            session.current_page, PAGE_SIZE, session.total_pages, "");
        if (!res.success || !res.data.has_value() || session.total_pages == 0) {
            event.reply(dpp::ir_update_message, "Soundpad empty or unavailable.");
            sessions_.erase(uid);
            return;
        }
        session.mappings = res.data.value();
        auto msg = build_picker_message(event.command.channel_id, session, uid,
                                         build_status_line(session));
        event.reply(dpp::ir_update_message, msg);
    }

    // Same as above but for the form-submit (jump-to-page) path.
    // Must be called with sessions_mutex_ held.
    void update_page_edit_response(const dpp::form_submit_t& event,
                                    PickerSession& session, const std::string& uid) {
        auto res = tool_interface_->listSoundpadClipsPaged(
            session.current_page, PAGE_SIZE, session.total_pages, "");
        if (!res.success || !res.data.has_value() || session.total_pages == 0) {
            event.edit_original_response(dpp::message("Soundpad empty or unavailable."));
            sessions_.erase(uid);
            return;
        }
        session.mappings = res.data.value();
        auto msg = build_picker_message(event.command.channel_id, session, uid,
                                         build_status_line(session));
        event.edit_original_response(msg);
    }

public:
    JoinEffectCommand() = delete;

    JoinEffectCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) override {
        const std::string guild_id = event.command.guild_id.str();

        auto user_param = event.get_parameter("user");
        if (!std::holds_alternative<dpp::snowflake>(user_param)) {
            event.reply("Missing required `user` option.");
            co_return;
        }
        const std::string target_user_id = std::get<dpp::snowflake>(user_param).str();

        // Open the paginated picker.
        int local_total_pages = 0;
        auto res = tool_interface_->listSoundpadClipsPaged(0, PAGE_SIZE, local_total_pages, "");
        if (!res.success || !res.data.has_value() || local_total_pages == 0) {
            event.reply("Soundpad empty or unavailable.");
            co_return;
        }

        // Resolve any existing binding for this user so the picker header can
        // show it. nullopt or empty -> "(none)".
        std::string existing_binding;
        if (auto cur = tool_interface_->getJoinEffect(guild_id, target_user_id)) {
            existing_binding = *cur;
        }

        std::string uid;
        dpp::message msg;
        {
            std::lock_guard<std::mutex> lg(sessions_mutex_);
            uid = random_gen_id();
            evict_old_sessions();
            PickerSession& session = sessions_[uid];
            session.mappings = res.data.value();
            session.current_page = 0;
            session.total_pages = local_total_pages;
            session.target_user_id = target_user_id;
            session.current_binding = std::move(existing_binding);
            session.created_at = std::time(nullptr);
            session.invoker_name = get_user_name_from_event(event);
            session.last_action = "Opened picker";

            msg = build_picker_message(event.command.channel_id, session, uid,
                                        build_status_line(session));
        }

        event.reply(msg);
        co_return;
    }

    exec::task<void> button(dpp::button_click_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 5 || ids[0] != "joineffect") {
            event.reply("Invalid button ID for joineffect");
            co_return;
        }
        auto recv_ts = std::stoll(ids[3]);
        auto now_ts = std::time(nullptr);
        const std::string& uid = ids[4];
        const std::string& cmd = ids[1];
        const std::string& cmd_param = ids[2];

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            event.reply(dpp::ir_update_message, "This join-effect interaction has expired.");
            std::lock_guard<std::mutex> lg(sessions_mutex_);
            sessions_.erase(uid);
            co_return;
        }

        std::unique_lock<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(uid);
        if (it == sessions_.end()) {
            lock.unlock();
            event.reply(dpp::ir_update_message, "This join-effect interaction is no longer valid.");
            co_return;
        }
        PickerSession& session = it->second;

        if (cmd == "bind") {
            int clip_id;
            try {
                clip_id = std::stoi(cmd_param);
            } catch (...) {
                lock.unlock();
                event.reply(dpp::ir_update_message, "Invalid clip ID.");
                co_return;
            }
            auto map_it = session.mappings.find(clip_id);
            if (map_it == session.mappings.end()) {
                lock.unlock();
                event.reply(dpp::ir_update_message, "Clip not on this page anymore — try refreshing.");
                co_return;
            }
            std::string clip_name = map_it->second;
            std::string guild_id = event.command.guild_id.str();
            std::string target_user_id = session.target_user_id;
            std::string actor = get_user_name_from_event(event);
            // Selection finalizes the picker: drop the session and stop showing
            // any buttons so the channel doesn't display stale interactive
            // controls. The reply is a plain message with no components.
            sessions_.erase(uid);
            lock.unlock();

            auto res = tool_interface_->setJoinEffect(guild_id, target_user_id, clip_name);
            if (!res.success) {
                event.reply(dpp::ir_update_message,
                    dpp::message("Failed to set join effect: " + res.message));
                co_return;
            }
            dpp::message confirm(event.command.channel_id,
                actor + " bound <@" + target_user_id + "> -> `" + clip_name + "`.");
            event.reply(dpp::ir_update_message, confirm);
            co_return;
        } else if (cmd == "unbind") {
            std::string guild_id = event.command.guild_id.str();
            std::string target_user_id = session.target_user_id;
            std::string actor = get_user_name_from_event(event);
            // Same finalization as bind: remove the session and post a plain
            // confirmation without interactive components.
            sessions_.erase(uid);
            lock.unlock();

            auto res = tool_interface_->removeJoinEffect(guild_id, target_user_id);
            if (!res.success) {
                event.reply(dpp::ir_update_message,
                    dpp::message("Failed to remove join effect: " + res.message));
                co_return;
            }
            dpp::message confirm(event.command.channel_id,
                actor + " removed join effect for <@" + target_user_id + ">.");
            event.reply(dpp::ir_update_message, confirm);
            co_return;
        } else if (cmd == "page_prev") {
            session.current_page = std::max(0, session.current_page - 1);
            update_button_page(event, session, uid);
        } else if (cmd == "page_next") {
            session.current_page = std::min(session.total_pages - 1, session.current_page + 1);
            update_button_page(event, session, uid);
        } else if (cmd == "page_jump") {
            int total = session.total_pages;
            lock.unlock();
            auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
            dpp::interaction_modal_response modal(
                "joineffect::jump_modal::" + ts + "::" + uid,
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
        } else {
            lock.unlock();
            event.reply(dpp::ir_update_message, "Unknown command");
        }
        co_return;
    }

    exec::task<void> form_submit(dpp::form_submit_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 4 || ids[0] != "joineffect" || ids[1] != "jump_modal") {
            event.reply("Invalid form submit for joineffect");
            co_return;
        }
        auto recv_ts = std::stoll(ids[2]);
        auto now_ts = std::time(nullptr);
        const std::string& uid = ids[3];

        event.reply(dpp::ir_deferred_update_message, "");

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            event.edit_original_response(dpp::message("This join-effect interaction has expired."));
            std::lock_guard<std::mutex> lg(sessions_mutex_);
            sessions_.erase(uid);
            co_return;
        }

        if (event.components.empty() || event.components[0].custom_id != JUMP_PAGE_COMPONENT_ID) {
            event.edit_original_response(dpp::message("Failed to read page number."));
            co_return;
        }
        std::string input;
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
            event.edit_original_response(dpp::message("This join-effect interaction is no longer valid."));
            co_return;
        }
        PickerSession& session = it->second;
        requested_page = std::max(0, std::min(session.total_pages - 1, requested_page));
        session.current_page = requested_page;

        update_page_edit_response(event, session, uid);
        co_return;
    }
};

#endif //DASH_N_BARK_JOINEFFECTCOMMAND_H
