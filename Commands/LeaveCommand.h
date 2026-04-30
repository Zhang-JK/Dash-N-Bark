//
// Created by laojk on 1/25/26.
//

#ifndef DASH_N_BARK_LEAVECOMMAND_H
#define DASH_N_BARK_LEAVECOMMAND_H

#include "CommandBase.h"

class LeaveCommand : public CommandBase {
public:
    LeaveCommand() = delete;

    LeaveCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            co_return;
        }

        auto voice_conn = event.from()->get_voice(event.command.guild_id);
        if (!voice_conn) {
            event.reply("I'm not in a voice channel!");
            co_return;
        }

        event.from()->disconnect_voice(event.command.guild_id);
        event.reply("Left the voice channel!");
        co_return;
    }
};


#endif //DASH_N_BARK_LEAVECOMMAND_H
