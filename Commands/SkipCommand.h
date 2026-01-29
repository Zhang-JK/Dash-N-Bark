//
// Created by laojk on 1/29/26.
//

#ifndef DASH_N_BARK_SKIP_COMMAND_H
#define DASH_N_BARK_SKIP_COMMAND_H

#include "CommandBase.h"

class SkipCommand : public CommandBase {
public:
    SkipCommand() = delete;

    SkipCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            return;
        }

        auto current_song = tool_interface_->getCurrentSong();
        if (!current_song.success) {
            event.reply("No song is currently playing!");
            return;
        }

        if (!tool_interface_->skipCurrentSong().success) {
            event.reply("Play list is empty");
            return;
        }

        auto next_song = tool_interface_->getCurrentSong();
        if (!next_song.success) {
            event.reply("**Skipped** " + std::get<0>(*current_song.data) + "\n**No more songs in the queue.**");
        } else {
            event.reply("**Skipped** " + std::get<0>(*current_song.data) +
                        "\n**Now playing** " + std::get<0>(*next_song.data));
        }
    }
};


#endif //DASH_N_BARK_SKIP_COMMAND_H