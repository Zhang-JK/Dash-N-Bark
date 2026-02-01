//
// Created by laojk on 1/29/26.
//

#ifndef DASH_N_BARK_PLAYLIST_COMMAND_H
#define DASH_N_BARK_PLAYLIST_COMMAND_H

#include "CommandBase.h"

class PlaylistCommand : public CommandBase {
public:
    PlaylistCommand() = delete;

    PlaylistCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            return;
        }

        auto playlist = tool_interface_->getPlaylist().data;
        if (!playlist.has_value()) {
            event.reply("The playlist is currently empty.");
            return;
        }
        auto &pl = playlist.value();
        std::string out;
        out += "🎶 Playlist\n";
        out += "────────────────\n";
        auto formatTime = [](int secs) -> std::string {
            int m = secs / 60;
            int s = secs % 60;
            return std::to_string(m) + ":" + (s < 10 ? "0" + std::to_string(s) : std::to_string(s));
        };

        for (size_t i = 0; i < pl.size(); ++i) {
            const auto &entry = pl[i];
            const std::string &name = std::get<0>(entry);
            int total = std::get<1>(entry);
            int played = std::get<2>(entry);
            std::string totalStr = formatTime(total);
            if (i == 0 && played > 0) {
                std::string playedStr = formatTime(played);
                out += "▶️ **" + std::to_string(i + 1) + ". " + name + " — " + playedStr + " / " + totalStr + "**\n";
                // if (pl.size() > 1) {
                //     out += "────────────────\n";
                // }
            } else {
                out += "▫️ " + std::to_string(i + 1) + ". " + name + " — " + totalStr + "\n";
            }
        }
        event.reply(out);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // No button interaction for this command
    }
};


#endif //DASH_N_BARK_PLAYLIST_COMMAND_H