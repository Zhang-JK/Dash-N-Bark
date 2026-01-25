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
        auto url = std::get<std::string>(event.get_parameter("url"));
        int volume = 100;
        if (std::holds_alternative<int64_t>(event.get_parameter("volume"))) {
            volume = static_cast<int>(std::get<int64_t>(event.get_parameter("volume")));
        }
        spdlog::debug("Got user requested url {} volume {}", url.c_str(), volume);

        auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(url);
        if (!tool_res.success || !tool_res.data.has_value()) {
            event.reply("Failed to fetch with error code " + std::to_string(tool_res.error_code) + ": " + tool_res.message);
            return;
        }
        event.reply("Streaming " + tool_res.data.value());
    }
};

#endif //DASH_N_BARK_STREAMCOMMAND_H