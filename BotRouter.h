//
// Created by laojk on 2025-12-02.
//

#ifndef DASH_N_BARK_BOTROUTER_H
#define DASH_N_BARK_BOTROUTER_H

#include <dpp/dpp.h>

#include "ToolInterface.h"
#include "Commands/CommandBase.h"
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

namespace ex = stdexec;

class BotRouter {
public:
    BotRouter(const std::string& botToken, const std::string& workDir);
    BotRouter() = delete;
    ~BotRouter();
    void start();
    void startBgTask();
    void setCmds();
    auto getRegisterCmdFunction() -> std::function<void(const dpp::ready_t &event)>;
    auto getCmdRouterFunction() -> std::function<void(const dpp::slashcommand_t &event)>;

private:
    std::string botToken_;
    std::shared_ptr<dpp::cluster> pbot_;
    std::map<std::string, std::tuple<dpp::slashcommand,
        std::optional<CommandBase*>>> cmds_;
    std::shared_ptr<ToolInterface> tool_;

    exec::static_thread_pool pool_{4};
    std::stop_source stop_src_;
    int bg_task_cycle_ms_ = 60;
    int target_buffered_audio_ms_ = 20;
};

#endif //DASH_N_BARK_BOTROUTER_H