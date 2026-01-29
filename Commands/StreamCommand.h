//
// Created by laojk on 1/25/26.
//

#ifndef DASH_N_BARK_STREAMCOMMAND_H
#define DASH_N_BARK_STREAMCOMMAND_H

#include "CommandBase.h"

class StreamCommand : public CommandBase {
public:
    StreamCommand() = delete;

    StreamCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            return;
        }

        auto url = std::get<std::string>(event.get_parameter("url"));
        int volume = 100;
        if (std::holds_alternative<int64_t>(event.get_parameter("volume"))) {
            volume = static_cast<int>(std::get<int64_t>(event.get_parameter("volume")));
        }
        spdlog::debug("Got user requested url {} volume {}", url.c_str(), volume);

        bool is_ready = true;
        dpp::voiceconn* vc_bot = event.from()->get_voice(event.command.guild_id);
        if (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready()) {
            event.reply("Bot does not in any channel. Try to join");
            is_ready = false;
            if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
                event.edit_response("You don't seem to be in a voice channel!");
                return;
            }
            // wait for ready
            auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(3000);
            do {
                vc_bot = event.from()->get_voice(event.command.guild_id);
                if (std::chrono::steady_clock::now() - start > timeout) {
                    event.edit_response("Timeout waiting for voice client to become ready.");
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready());
        }
        vc_bot->voiceclient->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);

        auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(url, volume);
        if (!tool_res.success || !tool_res.data.has_value()) {
            event.reply("Failed to fetch with error code " + std::to_string(tool_res.error_code) + ": " + tool_res.message);
            return;
        }

        if (is_ready) {
            event.reply("Streaming " + tool_res.data.value());
        } else {
            event.edit_response("Streaming " + tool_res.data.value());
        }
    }
};

#endif //DASH_N_BARK_STREAMCOMMAND_H