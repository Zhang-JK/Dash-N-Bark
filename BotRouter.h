//
// Created by laojk on 2025-12-02.
//

#ifndef DASH_N_BARK_BOTROUTER_H
#define DASH_N_BARK_BOTROUTER_H

#include <dpp/dpp.h>

#include "ToolInterface.h"
#include "Commands/CommandBase.h"
#include "Commands/JoinCommand.h"

class BotRouter {
public:
    BotRouter(const std::string& botToken, const std::string& workDir);
    BotRouter() = delete;
    ~BotRouter();
    void start();
    void setCmds();
    auto getRegisterCmdFunction() -> std::function<void(const dpp::ready_t &event)>;
    auto getCmdRouterFunction() -> std::function<void(const dpp::slashcommand_t &event)>;

private:
    std::string botToken_;
    std::shared_ptr<dpp::cluster> pbot_;
    std::map<std::string, std::tuple<dpp::slashcommand,
        std::optional<CommandBase*>>> cmds_;
    std::shared_ptr<ToolInterface> tool_;
};

#endif //DASH_N_BARK_BOTROUTER_H