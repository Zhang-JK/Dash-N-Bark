//
// Created by laojk on 1/20/26.
//

#ifndef DASH_N_BARK_JOINCOMMAND_H
#define DASH_N_BARK_JOINCOMMAND_H

#include "CommandBase.h"

class JoinCommand : public CommandBase {
public:
    JoinCommand() = delete;

    JoinCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            co_return;
        }

        if (!co_await joinVoiceChannel(event)) {
            co_return;
        }
        event.edit_original_response(dpp::message("Joined your voice channel!"));
        co_return;
    }
};


#endif //DASH_N_BARK_JOINCOMMAND_H
