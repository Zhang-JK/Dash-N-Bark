//
// Created by laojk on 2025-11-27.
//

#include "Stream-Fetch/FetchManager.h"

#include <filesystem>
#include <map>

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#include "spdlog/spdlog.h"

namespace StreamFetch {
    static std::map<int, std::string> error_code_and_msg = {
            {-1, "url not valid"},
            {-2, "authentication failed"},
            {-3, "video not found"},
            {-4, "network error"},
            {-100, "unknown error"}
    };

    FetchManager::FetchManager(const std::string& base, std::size_t limit)
        : baseSavePath(std::move(base)), storageLimitMB(limit) {
        // create base path if not exists
        std::filesystem::create_directories(baseSavePath);
        // make sure base path ends with /
        if (baseSavePath.back() != '/' && baseSavePath.back() != '\\') {
            baseSavePath += '/';
        }
    }

    FetchManager::StreamFetchResult FetchManager::fetchFromURL(const std::string &url) const {
        // check if url is valid
        if (url.find("bilibili") != std::string::npos || url.find("b23.tv") != std::string::npos) {
            // parse vid and sub_index from url
            std::string vid;
            int sub_index = 1;          // starts from 1 not 0
            size_t pos = url.find("video/");
            if (pos != std::string::npos) {
                size_t start = pos + 6;
                size_t end = url.find_first_of("/?", start);
                vid = url.substr(start, end - start);
                // check for sub_index
                pos = url.find("p=");
                if (pos != std::string::npos) {
                    size_t start_p = pos + 2;
                    size_t end_p = url.find_first_of("&", start_p);
                    try {
                        sub_index = std::stoi(url.substr(start_p, end_p - start_p));
                    } catch (const std::exception &) {
                        sub_index = 1;
                    }
                }
                return saveBilibiliVideo(vid, sub_index);
            }
        }
        return {-1, error_code_and_msg[-1]};
    }


    // private methods
    FetchManager::StreamFetchResult FetchManager::saveBilibiliVideo(const std::string &vid, int sub_index) const {
        // if vid begins with "BV", use bvid parameter, else use aid parameter
        cpr::Response r = cpr::Get(cpr::Url{"https://api.bilibili.com/x/web-interface/wbi/view"},
            cpr::Parameters{vid.rfind("BV", 0) == 0 ? cpr::Parameter{"bvid", vid} : cpr::Parameter{"aid", vid}});
        if (r.status_code != 200) {
            return {-4, error_code_and_msg[-4]};
        }
        std::string bvid;
        std::string title;
        long long cid = 0;
        int duration = 0;
        try {
            auto j = nlohmann::json::parse(r.text);
            if (j.value("code", -100) != 0 || !j.contains("data")) {
                return {-3, error_code_and_msg[-3]};
            }
            auto &data = j["data"];
            bvid = data.value("bvid", std::string());
            title = data.value("title", std::string());
            if (data.contains("pages") && data["pages"].is_array()) {
                bool found = false;
                for (const auto &p : data["pages"]) {
                    if (p.value("page", 0) == sub_index) {
                        cid = p.value("cid", 0LL);
                        duration = p.value("duration", 0);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // fallback: treat sub_index as one-based index
                    if (sub_index > 0 && sub_index <= static_cast<int>(data["pages"].size())) {
                        const auto &p = data["pages"][sub_index-1];
                        cid = p.value("cid", 0LL);
                        duration = p.value("duration", 0);
                    }
                }
            }
        } catch (const std::exception &) {
            return {-100, error_code_and_msg[-100]};
        }

        if (cid == 0) {
            return {-3, error_code_and_msg[-3]};
        }

        // get downloading url
        r = cpr::Get(cpr::Url{"https://api.bilibili.com/x/player/playurl"},
            cpr::Parameters{cpr::Parameter{"bvid", bvid},
                               cpr::Parameter{"cid", std::to_string(cid)},
                               cpr::Parameter{"qn", "32"},
                               cpr::Parameter{"fnval", "16"}});
        if (r.status_code != 200) {
            return {-4, error_code_and_msg[-4]};
        }
        std::string audio_link;
        try {
            auto j = nlohmann::json::parse(r.text);
            if (j.value("code", -100) != 0 || !j["data"]["dash"].contains("audio")) {
                return {-3, error_code_and_msg[-3]};
            }
            auto &audio = j["data"]["dash"]["audio"];
            if (!audio.is_array() || audio.empty()) {
                return {-3, error_code_and_msg[-3]};
            }
            audio_link = audio[0].value("baseUrl", std::string());
        } catch (const std::exception &) {
            return {-100, error_code_and_msg[-100]};
        }

        // download audio
        std::string savePath = baseSavePath + "bilibili_" + bvid + "_" + std::to_string(cid) + ".m4s";
        auto savePathReal = collectAndCache(savePath, VideoPlatform::BILIBILI, audio_link);
        if (!savePathReal.has_value()) {
            return {-4, error_code_and_msg[-4]};
        }
        return {0, "success", title, duration, savePathReal.value(), bvid, std::to_string(cid)};
    }

    std::optional<std::string> FetchManager::collectAndCache(const std::string &filename,
                                    FetchManager::VideoPlatform platform, const std::string &url) {
        // find if file already exists
        auto pcm_path = filename.substr(0, filename.find_last_of('.')) + ".pcm";
        if (std::filesystem::exists(pcm_path)) {
            spdlog::info("cache hit for file {}", pcm_path);
            return pcm_path;
        }
        if (std::filesystem::exists(filename)) {
            spdlog::info("cache hit for file {}", filename);
            return convertM4S2PCM(filename);
        }
        // download file
        auto fout = std::ofstream(filename, std::ios::binary);
        if (!fout) {
            return {};
        }
        switch (platform) {
            case VideoPlatform::BILIBILI: {
                cpr::Response download_response = cpr::Get(cpr::Url{url},
                    cpr::Header{
                        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
                                       " (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"},
                        {"Referer", "https://www.bilibili.com/"},
                    },
                    cpr::WriteCallback(
                        [&fout](std::string_view data, intptr_t _) {
                            fout.write(data.data(), data.size());
                            return true;
                    }));
                // check status code
                if (download_response.status_code != 200) {
                    fout.close();
                    std::filesystem::remove(filename);
                    return {};
                }
                // check file size
                if (download_response.header.find("Content-Length") != download_response.header.end()) {
                    size_t content_length = std::stoull(download_response.header.at("Content-Length"));
                    fout.flush();
                    auto file_size = std::filesystem::file_size(filename);
                    if (file_size != content_length) {
                        spdlog::error("downloaded file size mismatch: expected {}, got {}",
                            content_length, file_size);
                        fout.close();
                        std::filesystem::remove(filename);
                        return {};
                    }
                }
                fout.close();
                return convertM4S2PCM(filename);
            }
            case VideoPlatform::YOUTUBE: {
                throw std::runtime_error("Not implemented yet");
            }
            default: {
                return {};
            }
        }
    }

    std::optional<std::string> FetchManager::convertM4S2PCM(const std::string &path) {
        if (!std::filesystem::exists(path)) {
            spdlog::error("input file does not exist: {}", path);
            return {};
        }
        
        std::string output_path = path.substr(0, path.find_last_of('.')) + ".pcm";
        
        // Use FFmpeg to convert m4s to 16-bit stereo 44100Hz PCM
        // todo: make sure ffmpeg is installed and in PATH or just use ffmpeg static build
        std::string ffmpeg_cmd = "ffmpeg -i \"" + path + "\" -quality good -c:a pcm_s16le -f s16le -ar 48000 -ac 2 \"" + output_path + "\" -y";
        
        std::string ffmpeg_cmd_silent = ffmpeg_cmd + " > /dev/null 2>&1";
        int ret = std::system(ffmpeg_cmd_silent.c_str());
        if (ret != 0) {
            spdlog::error("ffmpeg conversion failed with exit code: {}", ret);
            return {};
        }
        
        if (!std::filesystem::exists(output_path)) {
            spdlog::error("output pcm file was not created: {}", output_path);
            return {};
        }
        
        spdlog::info("successfully converted {} to PCM", path);
        return output_path;
    }


} // StreamFetch