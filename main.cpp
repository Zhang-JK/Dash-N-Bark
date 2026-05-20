//
// Created by laojk on 2025-11-17.
//

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <dpp/dpp.h>
#include <spdlog/spdlog.h>
#include <exec/static_thread_pool.hpp>
#include <nlohmann/json.hpp>

#include "BotRouter.h"
#include "CrashHandler.h"

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#define GLOBAL_WORKING_DIR "../testdata/"
#define GLOBAL_CONFIG_PATH "../testdata/config.json"
#else
#define GLOBAL_WORKING_DIR "./data/"
#define GLOBAL_CONFIG_PATH "config.json"
#endif

int main() {
    // Install crash + deadlock-trace signal handlers first so we capture even
    // very-early crashes (e.g. during config parsing).
    CrashHandler::install();

    // set logging
    #if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
        spdlog::set_level(spdlog::level::trace);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    #else
        spdlog::set_level(spdlog::level::debug);
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

    std::string yt_dlp_path = "bin/yt-dlp_linux";
    std::string yt_cookie_path = GLOBAL_WORKING_DIR "/cookies.txt";
    if (!std::filesystem::exists(yt_dlp_path)) {
        spdlog::error("{} not found. Please ensure the binary exists.", yt_dlp_path);
        return 1;
    }
    if (!std::filesystem::exists(yt_cookie_path)) {
        spdlog::error("{} not found. Please ensure the cookie exists.", yt_cookie_path);
        return 1;
    }

    std::string join_sound_effect = GLOBAL_WORKING_DIR "system/se-rec.pcm";
    if (!std::filesystem::exists(join_sound_effect)) {
        spdlog::error("{} not found. Please ensure the sound effect exists.", join_sound_effect);
        return 1;
    }

    spdlog::info("Launching bot...");
    BotRouter botRouter(token, GLOBAL_WORKING_DIR);
    botRouter.startBgTask();
    botRouter.start();

    return 0;
}
