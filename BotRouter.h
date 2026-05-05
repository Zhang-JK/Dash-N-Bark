//
// Created by laojk on 2025-12-02.
//

#ifndef DASH_N_BARK_BOTROUTER_H
#define DASH_N_BARK_BOTROUTER_H

#include <atomic>
#include <dpp/dpp.h>

#include "ToolInterface.h"
#include "Commands/CommandBase.h"
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/timed_thread_scheduler.hpp>

// for those who will be in std standard
namespace ex = stdexec;

template <typename T>
concept SlashAndButtonCmd = std::same_as<T, dpp::slashcommand_t> || std::same_as<T, dpp::button_click_t> || std::same_as<T, dpp::form_submit_t> || std::same_as<T, dpp::select_click_t>;

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
    std::string getCommandName(const std::string& str);
    template<SlashAndButtonCmd CmdType>
    std::string getCommandName(const CmdType& event);
    template<SlashAndButtonCmd CmdType>
    void handlerExecWrapper(CommandBase* handler, const CmdType& event, std::shared_ptr<dpp::cluster> bot);

    // Stash a pending join-effect entry and arm a fallback timer. The platform
    // event handler or the fallback (whichever runs first) will play the clip.
    void armJoinEffect(dpp::snowflake user_id, std::string clip_name,
                       std::chrono::seconds fallback);

private:
    std::string botToken_;
    std::shared_ptr<dpp::cluster> pbot_;
    std::map<std::string, std::tuple<dpp::slashcommand,
        std::optional<CommandBase*>>> cmds_;
    std::shared_ptr<ToolInterface> tool_;

    std::shared_ptr<exec::static_thread_pool> ppool_;
    exec::timed_thread_context timed_thread_context_;
    std::stop_source stop_src_;
    int bg_task_cycle_ms_ = 60;
    int target_buffered_audio_ms_ = 20;

    std::map<std::pair<dpp::snowflake, dpp::snowflake>, dpp::snowflake> last_voice_channel_;
    std::mutex last_voice_channel_mutex_;
    // user_id -> clip_name. Entry is consumed by whichever fires first:
    // on_voice_client_platform (user RTC ready) or the 5s fallback timer.
    std::map<dpp::snowflake, std::string> pending_join_effects_;
    std::mutex pending_join_effects_mutex_;
    // Bounded counter for in-flight voice_receive packets. If too many packets
    // are queued onto the pool faster than RecorderSession can consume them,
    // we drop newer ones rather than let the queue grow unbounded.
    std::atomic<int> voice_recv_inflight_{0};
    std::chrono::steady_clock::time_point startup_time_ = std::chrono::steady_clock::now();
};

#endif //DASH_N_BARK_BOTROUTER_H