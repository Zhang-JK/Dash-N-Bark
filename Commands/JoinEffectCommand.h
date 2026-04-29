//
// Created by laojk on 2026-04-28.
//

#ifndef DASH_N_BARK_JOINEFFECTCOMMAND_H
#define DASH_N_BARK_JOINEFFECTCOMMAND_H

#include "CommandBase.h"

class JoinEffectCommand : public CommandBase {
public:
    JoinEffectCommand() = delete;

    JoinEffectCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        const std::string guild_id = event.command.guild_id.str();

        bool list = std::holds_alternative<bool>(event.get_parameter("list"))
                && std::get<bool>(event.get_parameter("list"));
        if (list) {
            auto bindings = tool_interface_->listJoinEffects(guild_id);
            if (bindings.empty()) {
                event.reply("No join effects bound in this server.");
                return;
            }
            std::string out = "Join effects in this server:\n";
            for (const auto& [uid, clip] : bindings) {
                out += "<@" + uid + "> -> " + clip + "\n";
            }
            event.reply(out);
            return;
        }

        auto user_param = event.get_parameter("user");
        if (!std::holds_alternative<dpp::snowflake>(user_param)) {
            event.reply("Missing required `user` option.");
            return;
        }
        const std::string user_id = std::get<dpp::snowflake>(user_param).str();

        auto clip_param = event.get_parameter("clip_name");
        std::string clip_name = std::holds_alternative<std::string>(clip_param)
                ? std::get<std::string>(clip_param) : std::string();

        if (clip_name.empty()) {
            auto res = tool_interface_->removeJoinEffect(guild_id, user_id);
            event.reply(res.success
                ? std::string("Removed join effect for <@") + user_id + ">."
                : std::string("Failed to remove join effect: ") + res.message);
            return;
        }

        auto res = tool_interface_->setJoinEffect(guild_id, user_id, clip_name);
        if (!res.success) {
            event.reply(std::string("Failed to set join effect: ") + res.message);
            return;
        }
        event.reply("Bound join effect: <@" + user_id + "> -> `" + clip_name + "`.");
    }
};

#endif //DASH_N_BARK_JOINEFFECTCOMMAND_H
