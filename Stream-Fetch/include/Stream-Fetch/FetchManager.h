//
// Created by laojk on 2025-11-27.
//

#ifndef DASH_N_BARK_FETCHMANAGER_H
#define DASH_N_BARK_FETCHMANAGER_H
#include <optional>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace StreamFetch {
    class FetchManager {
    public:
        FetchManager() = delete;
        explicit FetchManager(const std::string& base, std::size_t limit = 1024);

        struct StreamFetchResult {
            int error_code;
            std::string error_msg;
            std::string title;
            int duration;
            std::optional<std::string> path;
            std::string primaryKey;
            std::optional<std::string> secondaryKey;

            [[nodiscard]] bool isValid() const {
                return error_code == 0 && path.has_value();
            }
        };

        struct SearchResult {
            std::string platform;
            std::string title;
            std::string creator;
            std::string duration;
            std::string view_count;
            std::string publish_date;
            std::string url;
            std::string bvid_or_vid;
        };

        enum class VideoPlatform {
            BILIBILI,
            YOUTUBE,
            UNKNOWN
        };

        [[nodiscard]] StreamFetchResult fetchFromURL(const std::string &url) const;
        [[nodiscard]] std::vector<SearchResult> search(const std::string &keyword, int max_results = 5);
        [[nodiscard]] std::vector<SearchResult> searchByPlatform(const std::string &keyword, const std::string &platform, int max_results = 10);

    private:
        [[nodiscard]] StreamFetchResult saveBilibiliVideo(const std::string &vid, int sub_index) const;
        [[nodiscard]] StreamFetchResult saveYoutubeVideo(const std::string &vid) const;

        static std::optional<std::string> convertM4S2PCM(const std::string &path);
        static std::optional<std::string> collectAndCache(const std::string &filename,
                                    VideoPlatform platform, const std::string &url);

        [[nodiscard]] std::string getBuvid3() const;
        [[nodiscard]] std::vector<SearchResult> searchBilibili(const std::string &keyword, int max_results) const;
        [[nodiscard]] std::vector<SearchResult> searchYoutube(const std::string &keyword, int max_results) const;

        int maxRetryTimeBiliSearch = 5;
        std::string baseSavePath;
        std::size_t storageLimitMB = 1024;
    };
} // StreamFetch

#endif //DASH_N_BARK_FETCHMANAGER_H