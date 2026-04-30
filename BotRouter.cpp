//
// Created by laojk on 2025-12-02.
//

#include <spdlog/spdlog.h>
#include <exec/start_detached.hpp>
#include <exec/repeat_until.hpp>
#include <exec/task.hpp>

#include "BotRouter.h"

#include "Commands/JoinCommand.h"
#include "Commands/LeaveCommand.h"
#include "Commands/StreamCommand.h"
#include "Commands/PlaylistCommand.h"
#include "Commands/SkipCommand.h"
#include "Commands/SoundpadCommand.h"
#include "Commands/AddCommand.h"
#include "Commands/ParrotCommand.h"
#include "Commands/SearchCommand.h"
#include "Commands/JoinEffectCommand.h" 

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
      pbot_(std::make_shared<dpp::cluster>(botToken_)),
      ppool_(std::make_shared<exec::static_thread_pool>(4)) {
    pbot_->on_log(spdlog_logger());
    tool_ = std::make_shared<ToolInterface>(workDir, ppool_);

    this->setCmds();
    pbot_->on_ready(this->getRegisterCmdFunction());
    pbot_->on_slashcommand(this->getCmdRouterFunction<dpp::slashcommand_t>());
    pbot_->on_button_click(this->getCmdRouterFunction<dpp::button_click_t>());
    pbot_->on_form_submit(this->getCmdRouterFunction<dpp::form_submit_t>());
    pbot_->on_select_click(this->getCmdRouterFunction<dpp::select_click_t>());

    pbot_->on_autocomplete([this, bot = pbot_, tool = tool_](const dpp::autocomplete_t& ev) {
        if (ev.name != "joineffect") return;
        auto sched = ppool_->get_scheduler();
        auto pool_keepalive = ppool_;
        auto work = stdexec::schedule(sched)
            | stdexec::then([ev, bot, tool, pool_keepalive]() noexcept {
                try {
                    for (const auto& opt : ev.options) {
                        if (!opt.focused || opt.name != "clip_name") continue;
                        std::string query;
                        if (std::holds_alternative<std::string>(opt.value)) {
                            query = std::get<std::string>(opt.value);
                        }
                        constexpr int max_choices = 25;
                        auto matches = tool->searchSoundpadByName(query, max_choices);
                        dpp::interaction_response resp(dpp::ir_autocomplete_reply);
                        for (const auto& [id, name] : matches) {
                            resp.add_autocomplete_choice(dpp::command_option_choice(name, name));
                        }
                        bot->interaction_response_create(ev.command.id, ev.command.token, resp);
                        return;
                    }
                } catch (const std::exception& e) {
                    spdlog::error("autocomplete pool work threw: {}", e.what());
                } catch (...) {
                    spdlog::error("autocomplete pool work threw unknown exception");
                }
            })
            | stdexec::upon_error([](auto&&) noexcept {})
            | stdexec::upon_stopped([]() noexcept {});
        exec::start_detached(std::move(work));
    });

    pbot_->on_voice_client_platform([this](const dpp::voice_client_platform_t& ev) {
        // Fires when another user's RTC is ready in a channel the bot is in.
        // If we have a pending join effect for that user, play it 200ms later.
        const auto user_id = ev.user_id;
        std::string clip_name;
        {
            std::lock_guard<std::mutex> lg(pending_join_effects_mutex_);
            auto it = pending_join_effects_.find(user_id);
            if (it == pending_join_effects_.end()) return; // no pending effect, or fallback already consumed it
            clip_name = std::move(it->second);
            pending_join_effects_.erase(it);
        }
        // Defensively set send audio type — when the bot just joined to follow this user,
        // nothing else has set it. Idempotent if already set.
        if (ev.voice_client) {
            ev.voice_client->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);
        }
        spdlog::debug("Join effect: platform ready for user {}, scheduling play in 800ms", user_id.str());
        auto stop_token = stop_src_.get_token();
        auto timed_sched = timed_thread_context_.get_scheduler();
        auto pool_sched = ppool_->get_scheduler();
        auto tool = tool_;
        auto work = exec::schedule_after(timed_sched, std::chrono::milliseconds(1800))
            | stdexec::continues_on(pool_sched)
            | stdexec::then([=]() {
                if (stop_token.stop_requested()) return;
                auto res = tool->playSoundpadClipByName(clip_name, 100);
                if (!res.success) {
                    spdlog::warn("Join effect playback failed: {}", res.message);
                }
            })
            | stdexec::upon_error([](auto&&) noexcept {})
            | stdexec::upon_stopped([]() noexcept {});
        exec::start_detached(std::move(work));
    });

    pbot_->on_voice_state_update([this](const dpp::voice_state_update_t& ev) {
        const auto guild_id = ev.state.guild_id;
        const auto user_id = ev.state.user_id;
        const auto channel_id = ev.state.channel_id;
        if (!guild_id || !user_id) return;
        if (user_id == pbot_->me.id) return; // ignore self

        // Detect channel transitions; ignore mute/deaf-only updates and leaves.
        dpp::snowflake prev{0};
        bool first_seen = false;
        {
            std::lock_guard<std::mutex> lg(last_voice_channel_mutex_);
            auto key = std::make_pair(guild_id, user_id);
            auto it = last_voice_channel_.find(key);
            if (it == last_voice_channel_.end()) {
                first_seen = true;
            } else {
                prev = it->second;
            }
            last_voice_channel_[key] = channel_id;
        }
        if (!channel_id) return;            // user left a channel
        if (!first_seen && prev == channel_id) return; // not a channel change
        // Suppress the initial snapshot Discord sends right after connect, so the
        // bot doesn't barge in on people who were already in voice when it started.
        if (std::chrono::steady_clock::now() - startup_time_ < std::chrono::seconds(15)) return;

        auto* client = ev.from();
        if (!client) return;

        // Move the DB read + voice connect off the DPP event-loop thread.
        auto sched = ppool_->get_scheduler();
        auto pool_keepalive = ppool_;
        auto tool = tool_;
        auto self_bot = pbot_;
        auto work = stdexec::schedule(sched)
            | stdexec::then([this, guild_id, user_id, channel_id, client, tool, self_bot, pool_keepalive]() noexcept {
                try {
                    auto clip = tool->getJoinEffect(guild_id.str(), user_id.str());
                    if (!clip) return;

                    auto* vc = client->get_voice(guild_id);
                    if (vc != nullptr) {
                        if (vc->channel_id == channel_id) {
                            spdlog::debug("Join effect: user {} joined bot's channel {}, queued for platform event",
                                            user_id.str(), channel_id.str());
                            armJoinEffect(user_id, *clip, std::chrono::seconds(5));
                        } else {
                            spdlog::debug("Join effect: user {} joined different channel {}, bot in {}, ignoring",
                                            user_id.str(), channel_id.str(), vc->channel_id.str());
                        }
                        return;
                    }

                    dpp::guild* g = dpp::find_guild(guild_id);
                    if (!g) return;
                    if (!g->connect_member_voice(*self_bot, user_id)) {
                        spdlog::warn("Join effect: connect_member_voice failed for user {} in guild {}",
                                        user_id.str(), guild_id.str());
                        return;
                    }
                    spdlog::debug("Join effect: connecting to user {}'s channel {}, queued for platform event",
                                    user_id.str(), channel_id.str());
                    armJoinEffect(user_id, *clip, std::chrono::seconds(10));
                } catch (const std::exception& e) {
                    spdlog::error("voice_state_update pool work threw: {}", e.what());
                } catch (...) {
                    spdlog::error("voice_state_update pool work threw unknown exception");
                }
            })
            | stdexec::upon_error([](auto&&) noexcept {})
            | stdexec::upon_stopped([]() noexcept {});
        exec::start_detached(std::move(work));
    });
}

void BotRouter::armJoinEffect(dpp::snowflake user_id, std::string clip_name,
                               std::chrono::seconds fallback) {
    {
        std::lock_guard<std::mutex> lg(pending_join_effects_mutex_);
        pending_join_effects_[user_id] = clip_name;
    }
    auto stop_token = stop_src_.get_token();
    auto timed_sched = timed_thread_context_.get_scheduler();
    auto pool_sched = ppool_->get_scheduler();
    auto tool = tool_;
    auto user_key = user_id;
    auto work = exec::schedule_after(timed_sched, fallback)
        | stdexec::continues_on(pool_sched)
        | stdexec::then([this, user_key, tool, stop_token]() {
            if (stop_token.stop_requested()) return;
            std::string local_clip;
            {
                std::lock_guard<std::mutex> lg(pending_join_effects_mutex_);
                auto it = pending_join_effects_.find(user_key);
                if (it == pending_join_effects_.end()) return; // platform event already consumed it
                local_clip = std::move(it->second);
                pending_join_effects_.erase(it);
            }
            spdlog::debug("Join effect: fallback (no platform event) for user {}", user_key.str());
            auto res = tool->playSoundpadClipByName(local_clip, 100);
            if (!res.success) {
                spdlog::warn("Join effect playback failed: {}", res.message);
            }
        })
        | stdexec::upon_error([](auto&&) noexcept {})
        | stdexec::upon_stopped([]() noexcept {});
    exec::start_detached(std::move(work));
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

    pbot_->on_voice_receive([this, tool = tool_](const dpp::voice_receive_t &event) {
        // Hop the per-packet recording write onto the worker pool so the DPP
        // voice thread is never blocked by mutex contention or session work.
        auto sched = ppool_->get_scheduler();
        auto pool_keepalive = ppool_;
        std::vector<uint8_t> data = event.audio_data;
        size_t size = event.audio_size;
        std::string user_id = event.user_id.str();
        auto work = stdexec::schedule(sched)
            | stdexec::then([tool, data = std::move(data), size, user_id = std::move(user_id), pool_keepalive]() mutable noexcept {
                try {
                    tool->recordingVoiceCallback(std::move(data), size, user_id);
                } catch (...) {
                    spdlog::error("voice_receive pool work threw");
                }
            })
            | stdexec::upon_error([](auto&&) noexcept {})
            | stdexec::upon_stopped([]() noexcept {});
        exec::start_detached(std::move(work));
    });

    #ifdef NDEBUG
    static constexpr int VOICE_IDLE_TIMEOUT_SEC = 600;
    #else
    static constexpr int VOICE_IDLE_TIMEOUT_SEC = 60;
    #endif

    struct TickerState {
        std::mutex client_mutex;
        dpp::discord_client* client = nullptr;
        std::optional<dpp::snowflake> serving_guild_id;
        steady_clock::time_point audio_buffered_timestamp{};
        microseconds sleep_duration{0};
        bool init = true;
        int idle_count = 0;
    };
    auto state = std::make_shared<TickerState>();
    state->sleep_duration = microseconds(bg_task_cycle_ms_ * 1000);

    pbot_->on_voice_ready([state](const dpp::voice_ready_t& event) {
        std::lock_guard<std::mutex> lg(state->client_mutex);
        spdlog::debug("on_voice_ready triggered for channel {}", event.voice_client->channel_id);
        state->client = event.from();
        state->serving_guild_id = event.voice_client->server_id;
    });

    const auto step_size = bg_task_cycle_ms_ * 48000 * 2 * 2 / 1000;
    const auto cycle_us = microseconds(bg_task_cycle_ms_ * 1000);
    const auto target_buf_us = microseconds(target_buffered_audio_ms_ * 1000);
    const auto max_idle_count = VOICE_IDLE_TIMEOUT_SEC * 1000 / bg_task_cycle_ms_;
    auto timed_sched = timed_thread_context_.get_scheduler();
    auto pool_keepalive = ppool_;
    auto token = stop_src_.get_token();

    auto loop = stdexec::schedule(ppool_->get_scheduler())
        | stdexec::let_value([this, state, token, timed_sched, step_size, cycle_us, target_buf_us, max_idle_count, pool_keepalive]() mutable {
            auto pool_sched = ppool_->get_scheduler();
            return exec::repeat_until(
                stdexec::just()
                | stdexec::let_value([state, timed_sched, pool_sched]() {
                    return exec::schedule_after(timed_sched, state->sleep_duration)
                        | stdexec::continues_on(pool_sched);
                })
                | stdexec::then([this, state, token, step_size, cycle_us, target_buf_us, max_idle_count]() noexcept {
                    if (token.stop_requested()) {
                        spdlog::debug("[Ticker] Cleaning up and stopping.");
                        return true;
                    }
                    try {
                        std::lock_guard<std::mutex> lg(state->client_mutex);
                        if (state->client && state->serving_guild_id) {
                            auto local_voice_conn = state->client->get_voice(state->serving_guild_id.value());
                            auto audio = tool_->stepAudioMixer(step_size);
                            if (local_voice_conn && audio) {
                                state->idle_count = 0;
                                if (local_voice_conn->voiceclient && local_voice_conn->voiceclient->is_ready()) {
                                    local_voice_conn->voiceclient->send_audio_raw(
                                        reinterpret_cast<uint16_t *>(audio->getData()), audio->getSize());
                                    if (state->init) {
                                        state->audio_buffered_timestamp = steady_clock::now() + cycle_us;
                                        state->init = false;
                                    } else {
                                        state->audio_buffered_timestamp += cycle_us;
                                    }
                                    auto next = duration_cast<microseconds>(
                                        state->audio_buffered_timestamp - target_buf_us - steady_clock::now());
                                    if (next < microseconds(0)) next = microseconds(0);
                                    state->sleep_duration = next;
                                } else {
                                    state->sleep_duration = cycle_us;
                                }
                            } else {
                                if (!state->init) {
                                    state->idle_count = 0;
                                    state->init = true;
                                    if (!local_voice_conn) {
                                        spdlog::info("client disconnected voice");
                                        tool_->clearAllAudio();
                                    }
                                }
                                if (local_voice_conn) {
                                    state->idle_count++;
                                    if (state->idle_count >= max_idle_count) {
                                        spdlog::info("Voice connection idle for too long, disconnecting...");
                                        state->client->disconnect_voice(state->serving_guild_id.value());
                                        state->idle_count = 0;
                                        tool_->clearAllAudio();
                                        state->init = true;
                                    }
                                }
                                state->sleep_duration = cycle_us;
                            }
                        } else {
                            state->sleep_duration = cycle_us;
                        }
                    } catch (const dpp::voice_exception& e) {
                        spdlog::error("dpp voice exception in bg task: {}", e.what());
                        state->client = nullptr;
                        state->serving_guild_id = std::nullopt;
                        state->sleep_duration = cycle_us;
                    } catch (...) {
                        spdlog::error("Unknown exception in bg task.");
                        state->client = nullptr;
                        state->serving_guild_id = std::nullopt;
                        state->sleep_duration = cycle_us;
                    }
                    return token.stop_requested();
                })
            );
        })
        | stdexec::upon_error([](auto&&) noexcept {})
        | stdexec::upon_stopped([]() noexcept {});

    exec::start_detached(std::move(loop));
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
    cmds_["search"] = std::make_tuple(
        dpp::slashcommand("search", "Search videos on Youtube or Bilibili.", pbot_->me.id)
            .add_localization("zh-CN", "搜索", "在 Youtube 或 Bilibili 上搜索视频。")
            .add_option(
                dpp::command_option(dpp::co_string, "platform", "Platform to search", true)
                    .add_localization("zh-CN", "平台", "搜索平台")
                    .add_choice(dpp::command_option_choice("YouTube", "youtube"))
                    .add_choice(dpp::command_option_choice("Bilibili", "bilibili"))
            )
            .add_option(
                dpp::command_option(dpp::co_string, "keyword", "Search keyword", true)
                    .add_localization("zh-CN", "关键词", "搜索关键词")
            ),
        new SearchCommand(tool_)
    );
    cmds_["playlist"] = std::make_tuple(
        dpp::slashcommand("playlist", "Show the current song in queue.", pbot_->me.id)
            .add_localization("zh-CN", "播放列表", "显示当前队列中的歌曲。"),
        new PlaylistCommand(tool_)
    );
    cmds_["skip"] = std::make_tuple(
        dpp::slashcommand("skip", "Skip the current playing song.", pbot_->me.id)
            .add_localization("zh-CN", "跳过", "跳过当前正在播放的歌曲。"),
        new SkipCommand(tool_)
    );

    cmds_["add"] = std::make_tuple(
        dpp::slashcommand("add", "Add a clip to soundpad", pbot_->me.id)
            .add_localization("zh-CN", "添加", "添加音频片段到音效板。")
            .add_option(
                dpp::command_option(dpp::co_string, "url", "Video URL to be clipped", true)
                    .add_localization("zh-CN", "链接", "要剪辑的视频链接")
            )
            .add_option(
                dpp::command_option(dpp::co_string, "start", "Clip start time (e.g. 13.67 or 1:4.25)", true)
                    .add_localization("zh-CN", "起始时间", "片段起始时间 (如 13.67 或 1:4.25)")
            )
            .add_option(
                dpp::command_option(dpp::co_string, "end", "Clip end time (e.g. 13.67 or 1:4.25)", true)
                    .add_localization("zh-CN", "结束时间", "片段结束时间 (如 13.67 或 1:4.25)")
            )
            .add_option(
                dpp::command_option(dpp::co_string, "name", "Name your clip", true)
                    .add_localization("zh-CN", "名称", "为你的片段命名")
            )
            .add_option(
                dpp::command_option(dpp::co_string, "tag1", "First tag for the clip", false)
                    .add_localization("zh-CN", "标签1", "片段的第一个标签")
            )
            .add_option(
                dpp::command_option(dpp::co_string, "tag2", "Second tag for the clip", false)
                    .add_localization("zh-CN", "标签2", "片段的第二个标签")
            )
            .add_option(
                dpp::command_option(dpp::co_boolean, "pin", "Pin the clip to top page", false)
                    .add_localization("zh-CN", "置顶", "将片段置顶到首页")
            ),
        new AddCommand(tool_)
    );
    cmds_["soundpad"] = std::make_tuple(
        dpp::slashcommand("soundpad", "Play soundpad effects.", pbot_->me.id)
            .add_localization("zh-CN", "音效板", "播放音效板效果。")
            .add_option(
                dpp::command_option(dpp::co_boolean, "by_tag", "Select sound by tag (true) or by index (false)", false)
                    .add_localization("zh-CN", "按标签选择", "是否通过标签选择音效 (true/false)")
            ),
        new SoundpadCommand(tool_)
    );
    // cmds_["manage_soundpad"] = std::make_tuple(
    //     dpp::slashcommand("manage_soundpad", "Manage soundpad clips.", pbot_->me.id)
    //         .add_localization("zh-CN", "管理音效板", "管理音效板片段。"),
    //     std::nullopt
    // );

    cmds_["parrot"] = std::make_tuple(
        dpp::slashcommand("parrot", "Repeat your messages.", pbot_->me.id)
            .add_localization("zh-CN", "复读", "重复你的消息。")
            .add_option(
                dpp::command_option(dpp::co_user, "target", "User to be parroted", true)
                    .add_localization("zh-CN", "目标用户", "要被复读的用户")
            ).add_option(
                dpp::command_option(dpp::co_integer, "duration", "Duration of parroting in seconds (default 30)", false)
                    .add_localization("zh-CN", "持续时间", "复读持续时间，单位为秒 (默认 30)")
                    .set_min_value(5)
                    .set_max_value(120)
            ).add_option(
                dpp::command_option(dpp::co_string, "voice_preset", "Voice preset (default: little_girl)", false)
                    .add_localization("zh-CN", "声音预设", "声音预设 (默认: little_girl)")
                    .add_choice(dpp::command_option_choice("little_girl", "little_girl"))
                    .add_choice(dpp::command_option_choice("baby", "baby"))
                    .add_choice(dpp::command_option_choice("chipmunk", "chipmunk"))
                    .add_choice(dpp::command_option_choice("deep_voice", "deep_voice"))
                    .add_choice(dpp::command_option_choice("robot", "robot"))
                    .add_choice(dpp::command_option_choice("none", "none"))
            ),
        new ParrotCommand(tool_)
    );
    cmds_["joineffect"] = std::make_tuple(
        dpp::slashcommand("joineffect", "Bind a soundpad clip to play when a user joins voice.", pbot_->me.id)
            .add_localization("zh-CN", "进场音效", "为用户绑定加入语音频道时播放的音效。")
            .add_option(
                dpp::command_option(dpp::co_user, "user", "User to watch", false)
                    .add_localization("zh-CN", "用户", "要监听的用户")
            )
            .add_option(
                dpp::command_option(dpp::co_string, "clip_name", "Soundpad clip name (omit to remove binding)", false)
                    .add_localization("zh-CN", "音效名称", "音效板片段名称 (留空则取消绑定)")
                    .set_auto_complete(true)
            )
            .add_option(
                dpp::command_option(dpp::co_boolean, "list", "List bindings in this server", false)
                    .add_localization("zh-CN", "列表", "列出本服务器的绑定")
            ),
        new JoinEffectCommand(tool_)
    );
    // cmds_["tts"] = std::make_tuple(
    //     dpp::slashcommand("tts", "Text to speech.", pbot_->me.id)
    //         .add_localization("zh-CN", "语音合成", "将文字转换为语音。"),
    //     std::nullopt
    // );
    // cmds_["robot"] = std::make_tuple(
    //     dpp::slashcommand("robot", "Be triggered to certain voice chat.", pbot_->me.id)
    //         .add_localization("zh-CN", "机器人", "被触发到特定的语音聊天。"),
    //     std::nullopt
    // );
}

auto BotRouter::getRegisterCmdFunction() -> std::function<void(const dpp::ready_t &event)> {
    return [this, local_bot = pbot_, cmds = &cmds_](const dpp::ready_t &event) {
        const auto& session = event.session_id;
        const auto& id = local_bot->me.id;
        spdlog::info("Bot is ready. Session ID: {}", session);
        spdlog::info("Bot ID: {}", id);
        if (!dpp::run_once<struct register_bot_commands>()) {
            spdlog::warn("Bot commands have already been registered.");
            return;
        }

        // Replace the previous sleep_for(1s) loop with a stdexec pipeline
        // that schedules each global_command_create one second apart on the
        // timed_thread_context, so no worker is ever blocked by sleep_for.
        auto items = std::make_shared<std::vector<std::pair<std::string, dpp::slashcommand>>>();
        for (auto& [cmd_name, cmd_tuple] : *cmds) {
            items->emplace_back(cmd_name, std::get<0>(cmd_tuple));
        }
        auto idx = std::make_shared<std::atomic<size_t>>(0);
        auto timed_sched = timed_thread_context_.get_scheduler();
        auto pool_sched = ppool_->get_scheduler();
        auto pool_keepalive = ppool_;
        auto loop = stdexec::schedule(ppool_->get_scheduler())
            | stdexec::let_value([items, idx, timed_sched, pool_sched, local_bot, pool_keepalive]() mutable {
                return exec::repeat_until(
                    exec::schedule_after(timed_sched, std::chrono::seconds(1))
                    | stdexec::continues_on(pool_sched)
                    | stdexec::then([items, idx, local_bot]() noexcept {
                        try {
                            size_t i = idx->fetch_add(1);
                            if (i >= items->size()) return true;
                            const auto& [cmd_name, cmd] = (*items)[i];
                            local_bot->global_command_create(cmd,
                                [cmd_name = cmd_name](const dpp::confirmation_callback_t& cc) {
                                    if (cc.is_error()) {
                                        spdlog::error("Failed to register command '{}': {}",
                                                cmd_name, cc.get_error().message);
                                    } else {
                                        spdlog::info("Registered command '{}'", cmd_name);
                                    }
                                });
                            if (i + 1 >= items->size()) {
                                spdlog::info("Registered bot commands globally.");
                                return true;
                            }
                            return false;
                        } catch (const std::exception& e) {
                            spdlog::error("cmd register loop threw: {}", e.what());
                            return true;
                        } catch (...) {
                            spdlog::error("cmd register loop threw unknown exception");
                            return true;
                        }
                    })
                );
            })
            | stdexec::upon_error([](auto&&) noexcept {})
            | stdexec::upon_stopped([]() noexcept {});
        exec::start_detached(std::move(loop));
    };
}

template<SlashAndButtonCmd CmdType>
auto BotRouter::getCmdRouterFunction() -> std::function<void(const CmdType &event)> {
    return [local_bot = pbot_, cmds = &cmds_, this](const CmdType &event) {
        const auto& cmd_name = this->getCommandName<CmdType>(event);
        spdlog::debug("Received command: {}", cmd_name);
        auto it = cmds->find(cmd_name);
        if (it != cmds->end()) {
            const auto& [_, handler_opt] = it->second;
            if (handler_opt.has_value()) {
                const auto& handler = handler_opt.value();
                this->handlerExecWrapper<CmdType>(handler, event, local_bot);
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

std::string BotRouter::getCommandName(const std::string& str) {
    const auto pos = str.find("::");
    if (pos == std::string::npos) {
        return str;
    }
    return str.substr(0, pos);
}

template<SlashAndButtonCmd CmdType>
std::string BotRouter::getCommandName(const CmdType& event) {
    throw std::runtime_error("Not implemented getCommandName");
}

template<>
std::string BotRouter::getCommandName(const dpp::slashcommand_t& event) {
    return event.command.get_command_name();
}

template<>
std::string BotRouter::getCommandName(const dpp::button_click_t& event) {
    return getCommandName(event.custom_id);
}

template<>
std::string BotRouter::getCommandName(const dpp::form_submit_t& event) {
    return getCommandName(event.custom_id);
}

template<>
std::string BotRouter::getCommandName(const dpp::select_click_t& event) {
    return getCommandName(event.custom_id);
}

template<SlashAndButtonCmd CmdType>
void BotRouter::handlerExecWrapper(CommandBase* handler, const CmdType& event, std::shared_ptr<dpp::cluster> bot) {
    throw std::runtime_error("Not implemented handlerExecWrapper");
}

namespace {
    template<typename Sender>
    void launchHandlerOnPool(std::shared_ptr<exec::static_thread_pool> pool, Sender&& s) {
        auto sched = pool->get_scheduler();
        auto work = stdexec::starts_on(sched, std::forward<Sender>(s))
            | stdexec::upon_error([](auto&&) noexcept {
                spdlog::error("command coroutine completed with error");
            })
            | stdexec::upon_stopped([]() noexcept {});
        exec::start_detached(std::move(work));
    }
}

template<>
void BotRouter::handlerExecWrapper(CommandBase* handler, const dpp::slashcommand_t& event, std::shared_ptr<dpp::cluster> bot) {
    launchHandlerOnPool(ppool_, handler->execute(event, bot));
}

template<>
void BotRouter::handlerExecWrapper(CommandBase* handler, const dpp::button_click_t& event, std::shared_ptr<dpp::cluster> bot) {
    launchHandlerOnPool(ppool_, handler->button(event, bot));
}

template<>
void BotRouter::handlerExecWrapper(CommandBase* handler, const dpp::form_submit_t& event, std::shared_ptr<dpp::cluster> bot) {
    launchHandlerOnPool(ppool_, handler->form_submit(event, bot));
}

template<>
void BotRouter::handlerExecWrapper(CommandBase* handler, const dpp::select_click_t& event, std::shared_ptr<dpp::cluster> bot) {
    launchHandlerOnPool(ppool_, handler->select(event, bot));
}
