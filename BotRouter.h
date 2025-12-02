//
// Created by laojk on 2025-12-02.
//

#ifndef DASH_N_BARK_BOTROUTER_H
#define DASH_N_BARK_BOTROUTER_H

#include <dpp/dpp.h>

class BotRouter {
public:
    BotRouter(std::string botToken);
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
        std::optional<std::function<void(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster>)>>>> cmds_;
};

#endif //DASH_N_BARK_BOTROUTER_H