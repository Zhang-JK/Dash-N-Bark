//
// Created by laojk on 2026-01-15.
//

#ifndef DASH_N_BARK_COMMANDBASE_H
#define DASH_N_BARK_COMMANDBASE_H
#include <memory>

#include <dpp/dpp.h>
#include <spdlog/spdlog.h>
#include "../ToolInterface.h"

class CommandBase {
public:
    explicit CommandBase(std::shared_ptr<ToolInterface> tool_interface)
        : tool_interface_(std::move(tool_interface)) {
        assert(tool_interface_ != nullptr && "ToolInterface pointer cannot be nullCommandBase()");
    }

    virtual ~CommandBase() = default;

    virtual void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) = 0;
    virtual void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) = 0;
    virtual void form_submit(const dpp::form_submit_t &event, std::shared_ptr<dpp::cluster> bot) {}

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

    static bool joinVoiceChannel(const dpp::interaction_create_t &event, bool is_button = false) {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            spdlog::error("Guild not found for guild ID: {}", event.command.guild_id);
            return false;
        }

        // Send deferred response to prevent Discord interaction timeout
        if (is_button) {
            event.reply(dpp::ir_deferred_update_message, "");
        } else {
            event.thinking();
        }

        dpp::voiceconn* vc_bot = event.from()->get_voice(event.command.guild_id);
        if (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready()) {
            if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
                event.edit_original_response(dpp::message("You don't seem to be in a voice channel!"));
                return false;
            }
            // wait for ready
            auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(3000);
            do {
                vc_bot = event.from()->get_voice(event.command.guild_id);
                if (std::chrono::steady_clock::now() - start > timeout) {
                    event.edit_original_response(dpp::message("Timeout waiting for voice client to become ready."));
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready());
        }
        vc_bot->voiceclient->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);
        return true;
    }

    // utils
    static auto random_gen_id(int len=4) -> std::string {
        static constexpr  char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        thread_local static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);
        std::string s; s.reserve(len);
        for (int i = 0; i < len; ++i) s.push_back(chars[dist(rng)]);
        return s;
    }

    // utils
    static auto get_user_name_from_event(const dpp::interaction_create_t &event) -> std::string {
        return event.command.member.get_nickname().empty()
                    ? event.command.usr.username
                    : event.command.member.get_nickname();
    }


    std::shared_ptr<ToolInterface> tool_interface_;
};


#endif //DASH_N_BARK_COMMANDBASE_H