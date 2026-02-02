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

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            return;
        }

        if (!joinVoiceChannel(event)) {
            return;
        }
        event.reply("Joined your voice channel!");
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // No button interaction for this command
    }

};


#endif //DASH_N_BARK_JOINCOMMAND_H