//
// Created by laojk on 2026-01-15.
//

#ifndef DASH_N_BARK_COMMANDBASE_H
#define DASH_N_BARK_COMMANDBASE_H
#include <memory>

#include <dpp/dpp.h>
#include <spdlog/spdlog.h>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/start_detached.hpp>
#include <exec/task.hpp>
#include "../ToolInterface.h"

class CommandBase {
public:
    explicit CommandBase(std::shared_ptr<ToolInterface> tool_interface)
        : tool_interface_(std::move(tool_interface)) {
        assert(tool_interface_ != nullptr && "ToolInterface pointer cannot be nullCommandBase()");
    }

    virtual ~CommandBase() = default;

    virtual exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) {
        spdlog::warn("execute not impl in this command!");
        co_return;
    }
    virtual exec::task<void> button(dpp::button_click_t event, std::shared_ptr<dpp::cluster> bot) {
        spdlog::warn("button not impl in this command!");
        co_return;
    }
    virtual exec::task<void> form_submit(dpp::form_submit_t event, std::shared_ptr<dpp::cluster> bot) {
        spdlog::warn("form_submit not impl in this command!");
        co_return;
    }
    virtual exec::task<void> select(dpp::select_click_t event, std::shared_ptr<dpp::cluster> bot) {
        spdlog::warn("select not impl in this command!");
        co_return;
    }

protected:
    static std::vector<std::string> parseButtonId(const std::string &button_id, const std::string &delimiter = "::") {
        std::vector<std::string> parts;
        if (delimiter.empty()) {
            return parts;
        }
        size_t start = 0;
        size_t end = button_id.find(delimiter, start);
        while (end != std::string::npos) {
            parts.emplace_back(button_id.substr(start, end - start));
            start = end + delimiter.size();
            end = button_id.find(delimiter, start);
        }
        if (start <= button_id.size()) {
            parts.emplace_back(button_id.substr(start));
        }
        return parts;
    }

    // Coroutine: poll for voice readiness on the timed scheduler, hopping back
    // to the pool after every wait so heavy work never lands on the timed
    // thread. Caller is already on a pool worker (handlerExecWrapper schedules
    // every command body via starts_on(pool_sched, ...)).
    exec::task<bool> joinVoiceChannel(dpp::interaction_create_t event, bool is_button = false) {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            spdlog::error("Guild not found for guild ID: {}", event.command.guild_id);
            co_return false;
        }

        if (is_button) {
            event.reply(dpp::ir_deferred_update_message, "");
        } else {
            event.thinking();
        }

        dpp::voiceconn* vc_bot = event.from()->get_voice(event.command.guild_id);
        if (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready()) {
            if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
                event.edit_original_response(dpp::message("You don't seem to be in a voice channel!"));
                co_return false;
            }
            auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(3000);
            auto timed_sched = tool_interface_->getTimedScheduler();
            auto pool_sched = tool_interface_->getPoolScheduler();
            do {
                vc_bot = event.from()->get_voice(event.command.guild_id);
                if (std::chrono::steady_clock::now() - start > timeout) {
                    event.edit_original_response(dpp::message("Timeout waiting for voice client to become ready."));
                    co_return false;
                }
                co_await (exec::schedule_after(timed_sched, std::chrono::milliseconds(100))
                          | stdexec::continues_on(pool_sched));
            } while (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready());
        }
        vc_bot->voiceclient->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);
        co_return true;
    }

    static auto random_gen_id(int len=4) -> std::string {
        static constexpr  char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        thread_local static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);
        std::string s; s.reserve(len);
        for (int i = 0; i < len; ++i) s.push_back(chars[dist(rng)]);
        return s;
    }

    static auto get_user_name_from_event(const dpp::interaction_create_t &event) -> std::string {
        return event.command.member.get_nickname().empty()
                    ? event.command.usr.username
                    : event.command.member.get_nickname();
    }

    std::shared_ptr<ToolInterface> tool_interface_;
};


#endif //DASH_N_BARK_COMMANDBASE_H
