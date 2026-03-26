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

        if (!joinVoiceChannel(event)) {
            return;
        }

        event.edit_original_response(dpp::message("Fetching sound from URL..."));
        auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(event.command.guild_id.str(), url, volume);
        if (!tool_res.success || !tool_res.data.has_value()) {
            event.edit_original_response(dpp::message("Failed to fetch with error code " +
                        std::to_string(tool_res.error_code) + ": " + tool_res.message));
            return;
        }

        event.edit_original_response(dpp::message("Streaming " + tool_res.data.value()));
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // No button interaction for this command
    }
};

#endif //DASH_N_BARK_STREAMCOMMAND_H