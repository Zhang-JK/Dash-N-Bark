//
// Created by laojk on 2025-12-02.
//

#include <spdlog/spdlog.h>

#include "BotRouter.h"

#include "Commands/JoinCommand.h"
#include "Commands/LeaveCommand.h"
#include "Commands/StreamCommand.h"

// helper function
std::function<void(const dpp::log_t&)> spdlog_logger() {
    return [](const dpp::log_t& event) {
        if (event.severity > dpp::ll_trace) {
            switch (event.severity) {
                case dpp::ll_debug:
                    spdlog::debug("{}", event.message);
                    break;
                case dpp::ll_info:
                    spdlog::info("{}", event.message);
                    break;
                case dpp::ll_warning:
                    spdlog::warn("{}", event.message);
                    break;
                case dpp::ll_error:
                    spdlog::error("{}", event.message);
                    break;
                case dpp::ll_critical:
                    spdlog::critical("{}", event.message);
                    break;
                default:
                    spdlog::info("{}", event.message);
                    break;
            }
        }
    };
}

BotRouter::BotRouter(const std::string& botToken, const std::string& workDir)
    : botToken_(std::move(botToken)),
      pbot_(std::make_shared<dpp::cluster>(botToken_)) {
    pbot_->on_log(spdlog_logger());
    tool_ = std::make_shared<ToolInterface>(workDir);

    this->setCmds();
    pbot_->on_ready(this->getRegisterCmdFunction());
    pbot_->on_slashcommand(this->getCmdRouterFunction());
}

BotRouter::~BotRouter() {
    std::weak_ptr weak_monitor = pbot_;
    if (stop_src_.request_stop()) {
        spdlog::info("Stop signal sent to background tasks.");
    } else {
        spdlog::warn("Failed to send stop signal to background tasks.");
    }
    pbot_.reset();
    auto start_time = std::chrono::steady_clock::now();
    while (!weak_monitor.expired()) {
        auto elapsed_time = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed_time).count() >= 120) {
            spdlog::warn("Timeout reached while waiting for bot pointers to be released.");
            break;
        }
        spdlog::info("Waiting for all bot pointers to be released... Current use count: {}",
                        weak_monitor.use_count());
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

void BotRouter::startBgTask() {
    using namespace std::chrono;
    auto bg_ticker = ex::schedule(pool_.get_scheduler())
        | ex::let_value([this, token = stop_src_.get_token()]() mutable {
            // Return a sender that performs the loop
            return ex::just() | ex::then([this, token]() {
                dpp::discord_client* client = nullptr;
                // todo: support multi instances
                std::optional<dpp::snowflake> serving_guild_id = std::nullopt;
                pbot_->on_voice_ready([&client, &serving_guild_id](const dpp::voice_ready_t& event) {
                    spdlog::debug("on_voice_ready triggered for channel {}", event.voice_client->channel_id);
                    client = event.from();
                    serving_guild_id = event.voice_client->server_id;
                });

                // cycle_ms * sample_rate * bytes_per_sample * channels
                const auto step_size = bg_task_cycle_ms_ * 48000 * 2 * 2 / 1000;
                auto audio_buffered_timestamp = steady_clock::now();
                auto sleep_duration = microseconds(bg_task_cycle_ms_*1000);
                bool init = true;

                while (!token.stop_requested()) {
                    if (token.stop_requested()) break;
                    try {
                        if (client && serving_guild_id) {
                            auto local_voice_conn = client->get_voice(serving_guild_id.value());
                            auto audio = tool_->stepAudioMixer(step_size);
                            if (local_voice_conn && audio) {
                                if (local_voice_conn->voiceclient && local_voice_conn->voiceclient->is_ready()) {
                                    local_voice_conn->voiceclient->send_audio_raw(reinterpret_cast<uint16_t *>(audio->getData()), audio->getSize());
                                    if (init) {
                                        audio_buffered_timestamp = steady_clock::now() + microseconds(bg_task_cycle_ms_ * 1000);
                                        init = false;
                                    } else {
                                        audio_buffered_timestamp += microseconds(bg_task_cycle_ms_ * 1000);
                                    }
                                    sleep_duration = duration_cast<microseconds>(audio_buffered_timestamp -
                                        microseconds(target_buffered_audio_ms_ * 1000) - steady_clock::now());
                                    if (sleep_duration < microseconds(0)) {
                                        sleep_duration = microseconds(0);
                                    }
                                } else {
                                    sleep_duration = microseconds(bg_task_cycle_ms_*1000);
                                }
                            } else {
                                if (!init) {
                                    spdlog::info("client disconnected voice");
                                    tool_->clearAllAudio();
                                    init = true;
                                }
                                sleep_duration = microseconds(bg_task_cycle_ms_*1000);
                            }
                        }
                    } catch (const dpp::voice_exception& e) {
                        spdlog::error("dpp voice exception in bg task: {}", e.what());
                        client = nullptr;
                        serving_guild_id = std::nullopt;
                    } catch (...) {
                        spdlog::error("Unknown exception in bg task.");
                        client = nullptr;
                        serving_guild_id = std::nullopt;
                    }
                    std::this_thread::sleep_for(sleep_duration);
                }
                spdlog::debug("[Ticker] Cleaning up and stopping.");
            });
        });

    ex::start_detached(std::move(bg_ticker));
}

void BotRouter::start() {
    try {
        pbot_->start(dpp::st_wait);
    } catch (const std::exception& e) {
        spdlog::info("Bot shutdown requested or exception occurred: {}", e.what());
    } catch (...) {
        spdlog::info("Bot shutdown requested or unknown exception occurred.");
    }
}

void BotRouter::setCmds() {
    cmds_["join"] = std::make_tuple(
        dpp::slashcommand("join", "Joins your voice channel.", pbot_->me.id)
            .add_localization("zh-CN", "加入", "加入你所在的语音频道。"),
        new JoinCommand(tool_)
    );
    cmds_["leave"] = std::make_tuple(
        dpp::slashcommand("leave", "Leaves the voice channel.", pbot_->me.id)
            .add_localization("zh-CN", "离开", "离开当前语音频道。"),
        new LeaveCommand(tool_)
    );
    cmds_["stream"] = std::make_tuple(
        dpp::slashcommand("stream", "Stream audio from Youtube or Bilibili.", pbot_->me.id)
            .add_localization("zh-CN", "播放", "从 Youtube 或 Bilibili 播放音频。")
            .add_option(
                dpp::command_option(dpp::co_string, "url", "Video URL from Youtube or Bilibili", true)
                    .add_localization("zh-CN", "链接", "来自 Youtube 或 Bilibili 的视频链接")
            )
            .add_option(
                dpp::command_option(dpp::co_integer, "volume", "Volume level (1-100)", false)
                    .add_localization("zh-CN", "音量", "音量大小 (1-100)")
                    .set_min_value(1)
                    .set_max_value(100)
            ),
            new StreamCommand(tool_)
    );
    // cmds_["add"] = std::make_tuple(
    //     dpp::slashcommand("add", "Add a clip to soundpad", pbot_->me.id)
    //         .add_localization("zh-CN", "添加", "添加音频片段到音效板。")
    //         .add_option(
    //             dpp::command_option(dpp::co_string, "url", "Video URL to be clipped", true)
    //                 .add_localization("zh-CN", "链接", "要剪辑的视频链接")
    //         )
    //         .add_option(
    //             dpp::command_option(dpp::co_string, "start", "Clip start time (x:xx or xxx seconds)", true)
    //                 .add_localization("zh-CN", "起始时间", "片段起始时间 (x:xx 或 xxx 秒)")
    //         )
    //         .add_option(
    //             dpp::command_option(dpp::co_string, "end", "Clip end time (x:xx or xxx seconds)", true)
    //                 .add_localization("zh-CN", "结束时间", "片段结束时间 (x:xx 或 xxx 秒)")
    //         )
    //         .add_option(
    //             dpp::command_option(dpp::co_string, "tag1", "First tag for the clip", false)
    //                 .add_localization("zh-CN", "标签1", "片段的第一个标签")
    //         )
    //         .add_option(
    //             dpp::command_option(dpp::co_string, "tag2", "Second tag for the clip", false)
    //                 .add_localization("zh-CN", "标签2", "片段的第二个标签")
    //         )
    //         .add_option(
    //             dpp::command_option(dpp::co_boolean, "pin", "Pin the clip to top page", false)
    //                 .add_localization("zh-CN", "置顶", "将片段置顶到首页")
    //         ),
    //     std::nullopt
    // );
    // cmds_["soundpad"] = std::make_tuple(
    //     dpp::slashcommand("soundpad", "Play soundpad effects.", pbot_->me.id)
    //         .add_localization("zh-CN", "音效板", "播放音效板效果。"),
    //     std::nullopt
    // );
    // cmds_["parrot"] = std::make_tuple(
    //     dpp::slashcommand("parrot", "Repeat your messages.", pbot_->me.id)
    //         .add_localization("zh-CN", "复读", "重复你的消息。"),
    //     std::nullopt
    // );
    // cmds_["tts"] = std::make_tuple(
    //     dpp::slashcommand("tts", "Text to speech.", pbot_->me.id)
    //         .add_localization("zh-CN", "语音合成", "将文字转换为语音。"),
    //     std::nullopt
    // );
    // cmds_["robot"] = std::make_tuple(
    //     dpp::slashcommand("robot", "React to your messages.", pbot_->me.id)
    //         .add_localization("zh-CN", "机器人", "对你的消息做出机器人音效回应。"),
    //     std::nullopt
    // );
}

auto BotRouter::getRegisterCmdFunction() -> std::function<void(const dpp::ready_t &event)> {
    return [local_bot = pbot_, cmds = &cmds_](const dpp::ready_t &event) {
        // get server id from config
        const auto& session = event.session_id;
        const auto& id = local_bot->me.id;
        spdlog::info("Bot is ready. Session ID: {}", session);
        spdlog::info("Bot ID: {}", id);
        if (dpp::run_once<struct register_bot_commands>()) {
            for (const auto& [cmd_name, cmd_tuple] : *cmds) {
                const auto& [cmd, _] = cmd_tuple;
                local_bot->global_command_create(cmd, [cmd_name](const dpp::confirmation_callback_t& cc) {
                    if (cc.is_error()) {
                        spdlog::error("Failed to register command '{}': {}", cmd_name, cc.get_error().message);
                    } else {
                        spdlog::info("Registered command '{}'", cmd_name);
                    }
                });
            }
            spdlog::info("Registered bot commands globally.");
        } else {
            spdlog::warn("Bot commands have already been registered.");
        }
    };
}

auto BotRouter::getCmdRouterFunction() -> std::function<void(const dpp::slashcommand_t &event)> {
    return [local_bot = pbot_, cmds = &cmds_](const dpp::slashcommand_t &event) {
        const auto& cmd_name = event.command.get_command_name();
        spdlog::debug("Received command: {}", cmd_name);
        auto it = cmds->find(cmd_name);
        if (it != cmds->end()) {
            const auto& [_, handler_opt] = it->second;
            if (handler_opt.has_value()) {
                const auto& handler = handler_opt.value();
                handler->execute(event, local_bot);
            } else {
                spdlog::warn("No handler defined for command: {}", cmd_name);
                event.reply("This command is not yet implemented.");
            }
        } else {
            spdlog::warn("Unknown command received: {}", cmd_name);
            event.reply("Unknown command.");
        }
    };
}
