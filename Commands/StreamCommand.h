//
// Created by laojk on 1/25/26.
//

#ifndef DASH_N_BARK_STREAMCOMMAND_H
#define DASH_N_BARK_STREAMCOMMAND_H

#include "CommandBase.h"
#include <future>

class StreamCommand : public CommandBase {
public:
    StreamCommand() = delete;

    StreamCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            co_return;
        }

        auto url = std::get<std::string>(event.get_parameter("url"));
        int volume = 100;
        if (std::holds_alternative<int64_t>(event.get_parameter("volume"))) {
            volume = static_cast<int>(std::get<int64_t>(event.get_parameter("volume")));
        }
        spdlog::debug("Got user requested url {} volume {}", url.c_str(), volume);

        if (!co_await joinVoiceChannel(event)) {
            co_return;
        }

        event.edit_original_response(dpp::message("Fetching sound from URL..."));

        // Offload blocking I/O to a separate thread
        auto tool = tool_interface_;
        auto future = std::async(std::launch::async, [tool, url, volume]() {
            return tool->fetchAndEnqueuePlaylist(url, volume);
        });

        // Yield the worker thread while waiting for I/O
        auto timed_sched = tool_interface_->getTimedScheduler();
        auto pool_sched = tool_interface_->getPoolScheduler();
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            co_await (exec::schedule_after(timed_sched, std::chrono::milliseconds(100))
                      | stdexec::continues_on(pool_sched));
        }

        auto tool_res = future.get();
        if (!tool_res.success || !tool_res.data.has_value()) {
            event.edit_original_response(dpp::message("Failed to fetch with error code " +
                        std::to_string(tool_res.error_code) + ": " + tool_res.message));
            co_return;
        }

        event.edit_original_response(dpp::message("Streaming " + tool_res.data.value()));
        co_return;
    }
};

#endif //DASH_N_BARK_STREAMCOMMAND_H
