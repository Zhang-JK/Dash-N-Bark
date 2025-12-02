//
// Created by laojk on 2025-11-17.
//

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <dpp/dpp.h>
#include <spdlog/spdlog.h>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <nlohmann/json.hpp>
#include "Audio-Mixer/AudioMixer.h"
#include "Audio-Mixer/SoundPadManager.h"
#include "Stream-Fetch/include/Stream-Fetch/FetchManager.h"

#include "BotRouter.h"

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#define GLOBAL_WORKING_DIR "../testdata/"
#define GLOBAL_CONFIG_PATH "../testdata/config.json"
#else
#define GLOBAL_WORKING_DIR "./data/"
#define GLOBAL_CONFIG_PATH "config.json"
#endif

int main() {
    // set logging
    #if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
        spdlog::set_level(spdlog::level::trace);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    #else
        spdlog::set_level(spdlog::level::info);
    #endif

    // read token from config.json file
    nlohmann::json config;
    std::string token;
    try {
        std::ifstream cfg_file(GLOBAL_CONFIG_PATH);
        if (!cfg_file.is_open()) {
            spdlog::error("Failed to open `{}`", GLOBAL_CONFIG_PATH);
            return 1;
        }
        config = nlohmann::json::parse(cfg_file);
        token = config.value("token", std::string());
        if (token.empty()) {
            spdlog::error("Token missing in `{}`", GLOBAL_CONFIG_PATH);
            return 1;
        }
    } catch (const nlohmann::json::parse_error &e) {
        spdlog::error("JSON parse error: {}", e.what());
        return 1;
    } catch (const std::exception &e) {
        spdlog::error("Error reading config: {}", e.what());
        return 1;
    }

    spdlog::info("Launching bot...");
    BotRouter botRouter(token);
    botRouter.start();

    return 0;
}
