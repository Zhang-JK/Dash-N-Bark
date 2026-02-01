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

        auto voice_conn = event.from()->get_voice(event.command.guild_id);
        if (voice_conn) {
            auto users_vc = g->voice_members.find(event.command.get_issuing_user().id);
            if (users_vc != g->voice_members.end() && voice_conn->channel_id == users_vc->second.channel_id) {
                event.reply("I'm already in your voice channel!");
                return;
            }
        }
        if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
            event.reply("You don't seem to be in a voice channel!");
            return;
        }
        event.reply("Joined your voice channel!");
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // No button interaction for this command
    }

};


#endif //DASH_N_BARK_JOINCOMMAND_H