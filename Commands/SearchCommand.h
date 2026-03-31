//
// Created by laojk on 2026-03-30.
//

#ifndef DASH_N_BARK_SEARCHCOMMAND_H
#define DASH_N_BARK_SEARCHCOMMAND_H

#include "CommandBase.h"
#include "Stream-Fetch/FetchManager.h"
#include <vector>

class SearchCommand : public CommandBase {
public:
    SearchCommand() = delete;

    explicit SearchCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto keyword = std::get<std::string>(event.get_parameter("keyword"));
        spdlog::debug("Search command received keyword: {}", keyword);

        event.thinking();

        auto search_results = tool_interface_->search(keyword, 10);
        if (search_results.empty()) {
            event.edit_original_response(dpp::message("No search results found for \"" + keyword + "\""));
            return;
        }

        auto embed = buildSearchResultEmbed(search_results, keyword);
        event.edit_original_response(embed);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto parts = parseButtonId(event.custom_id);
        if (parts.empty()) {
            return;
        }

        if (parts[0] == "search_play") {
            if (parts.size() < 3) {
                event.reply(dpp::message("Invalid button data"));
                return;
            }

            std::string platform = parts[1];
            std::string url_or_bvid = parts[2];

            if (!joinVoiceChannel(event, true)) {
                return;
            }

            std::string url;
            if (platform == "youtube") {
                url = "https://www.youtube.com/watch?v=" + url_or_bvid;
            } else {
                url = "https://www.bilibili.com/video/" + url_or_bvid;
            }

            event.edit_original_response(dpp::message("Fetching audio from " + platform + "..."));
            auto tool_res = tool_interface_->fetchAndEnqueuePlaylist(url, 100);
            if (!tool_res.success || !tool_res.data.has_value()) {
                event.edit_original_response(dpp::message("Failed to fetch with error code " +
                            std::to_string(tool_res.error_code) + ": " + tool_res.message));
                return;
            }
            event.edit_original_response(dpp::message("Streaming " + tool_res.data.value()));
        }
    }

private:
    static dpp::message buildSearchResultEmbed(
            const std::vector<StreamFetch::FetchManager::SearchResult>& results,
            const std::string& keyword) {

        dpp::embed embed;
        embed.set_title("Search Results: " + keyword)
             .set_description("Found " + std::to_string(results.size()) + " results")
             .set_color(0x00D4FF);

        std::string description;
        dpp::component action_rows;

        int button_count = 0;
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];

            description += "**" + std::to_string(i + 1) + ". " + r.title + "**\n";
            description += "   " + r.creator + " | " + r.duration + " | " + r.view_count + " views\n";
            description += "   " + r.publish_date + " | [" + r.platform + "](" + r.url + ")\n\n";

            dpp::component button;
            button.set_type(dpp::cot_button)
                  .set_label(r.platform == "youtube" ? "▶ YT" : "▶ Bili")
                  .set_style(dpp::cos_primary)
                  .set_id("search_play::" + r.platform + "::" + r.bvid_or_vid);

            action_rows.add_component(button);
            button_count++;

            if (button_count >= 5) {
                break;
            }
        }

        embed.set_description(description);
        action_rows.set_type(dpp::cot_action_row);

        dpp::message msg;
        msg.add_embed(embed);
        msg.add_component(action_rows);

        return msg;
    }
};

#endif //DASH_N_BARK_SEARCHCOMMAND_H
