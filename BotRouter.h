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

// for those who will be in std standard
namespace ex = stdexec;

template <typename T>
concept SlashAndButtonCmd = std::same_as<T, dpp::slashcommand_t> || std::same_as<T, dpp::button_click_t> || std::same_as<T, dpp::form_submit_t>;

class BotRouter {
public:
    BotRouter(const std::string& botToken, const std::string& workDir);
    BotRouter() = delete;
    ~BotRouter();
    void start();
    void startBgTask();
    void setCmds();
    auto getRegisterCmdFunction() -> std::function<void(const dpp::ready_t &event)>;
    template<SlashAndButtonCmd CmdType>
    auto getCmdRouterFunction() -> std::function<void(const CmdType &event)>;
    template<SlashAndButtonCmd CmdType>
    std::string getCommandName(const CmdType& event);
    template<SlashAndButtonCmd CmdType>
    void handlerExecWrapper(CommandBase* handler, const CmdType& event, std::shared_ptr<dpp::cluster> bot);

private:
    std::string botToken_;
    std::shared_ptr<dpp::cluster> pbot_;
    std::map<std::string, std::tuple<dpp::slashcommand,
        std::optional<CommandBase*>>> cmds_;
    std::shared_ptr<ToolInterface> tool_;

    std::shared_ptr<exec::static_thread_pool> ppool_;
    std::stop_source stop_src_;
    int bg_task_cycle_ms_ = 60;
    int target_buffered_audio_ms_ = 20;
};

#endif //DASH_N_BARK_BOTROUTER_H