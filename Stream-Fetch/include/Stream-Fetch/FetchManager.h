//
// Created by laojk on 2025-11-27.
//

#ifndef DASH_N_BARK_FETCHMANAGER_H
#define DASH_N_BARK_FETCHMANAGER_H
#include <optional>
#include <string>

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

        enum class VideoPlatform {
            BILIBILI,
            YOUTUBE,
            UNKNOWN
        };

        [[nodiscard]] StreamFetchResult fetchFromURL(const std::string &url) const;

    private:
        [[nodiscard]] StreamFetchResult saveBilibiliVideo(const std::string &vid, int sub_index) const;
        [[nodiscard]] StreamFetchResult saveYoutubeVideo(const std::string &vid) const;

        static std::optional<std::string> convertM4S2PCM(const std::string &path);
        static std::optional<std::string> collectAndCache(const std::string &filename,
                                    VideoPlatform platform, const std::string &url);

        std::string baseSavePath;
        // todo: implement storage limit management
        std::size_t storageLimitMB = 1024; // Default 1GB
    };
} // StreamFetch

#endif //DASH_N_BARK_FETCHMANAGER_H