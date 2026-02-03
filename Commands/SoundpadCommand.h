//
// Created by laojk on 2/1/26.
//

#ifndef DASH_N_BARK_SOUNDPADCOMMAND_H
#define DASH_N_BARK_SOUNDPADCOMMAND_H

#include "CommandBase.h"

#include <dpp/unicode_emoji.h>

class SoundpadCommand : public CommandBase {
private:
    struct PagedComponent {
        int current_page;
        int total_pages;
        std::string tag;
    };
    std::optional<std::map<int, std::string>> soundpad_mappings_;
    std::optional<PagedComponent> soundpad_pagination_;
    std::optional<std::string> command_uid_;
    int soundpad_volume_ = 100;

    static constexpr int PAGE_SIZE = 3;

    static auto random_gen_id(int len=4) -> std::string {
        static constexpr  char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        thread_local static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);
        std::string s; s.reserve(len);
        for (int i = 0; i < len; ++i) s.push_back(chars[dist(rng)]);
        return s;
    }

    void clean_up() {
        soundpad_mappings_.reset();
        soundpad_pagination_.reset();
        command_uid_.reset();
    }

    [[nodiscard]] std::vector<dpp::component> build_soundpad_component(int items_per_row = 3) const {
        if (!soundpad_mappings_ || soundpad_mappings_->empty() || !command_uid_) {
            return {}; // return empty vector if no mappings
        }

        if (items_per_row <= 0) items_per_row = 3;
        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
        auto page = soundpad_pagination_.value().current_page;
        std::vector<dpp::component> rows;

        // Volume row: [-] [ volume ] [+]
        dpp::component vol_row;
        vol_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("-")
                .set_style(dpp::cos_secondary)
                .set_disabled(soundpad_volume_ <= 10)
                .set_id("soundpad::vol_down::" + std::to_string(soundpad_volume_) + "::" + ts + "::" +command_uid_.value())
        );
        vol_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Vol " + std::to_string(soundpad_volume_))
                .set_style(dpp::cos_secondary)
                .set_id("soundpad::vol_display::" + std::to_string(soundpad_volume_) + "::" + ts + "::" +command_uid_.value())
                .set_disabled(true)
        );
        vol_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("+")
                .set_style(dpp::cos_secondary)
                .set_disabled(soundpad_volume_ >= 100)
                .set_id("soundpad::vol_up::" + std::to_string(soundpad_volume_) + "::" + ts + "::" +command_uid_.value())
        );
        rows.push_back(vol_row);

        // Mapping buttons: paginated, up to 15 per page, configurable items per row
        if (soundpad_mappings_ && !soundpad_mappings_->empty()) {
            dpp::component row;
            int in_row = 0;
            int added = 0;
            const int per_page = PAGE_SIZE;

            for (const auto &entry : *soundpad_mappings_) {
                if (added >= per_page) break;

                const std::string &label = entry.second;
                int id = entry.first;

                row.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label(label)
                        .set_style(dpp::cos_primary)
                        .set_id("soundpad::play::" + std::to_string(id) + "::" + ts + "::" +command_uid_.value())
                );

                ++in_row;
                ++added;
                if (in_row == items_per_row) {
                    rows.push_back(row);
                    row = dpp::component();
                    in_row = 0;
                }
            }
            if (in_row > 0) {
                rows.push_back(row);
            }

            // Navigation row: [Prev] [ Page X ] [Next]
            dpp::component nav_row;
            // Prev button (disabled when on first page)
            dpp::component prev_btn;
            prev_btn.set_type(dpp::cot_button)
                    .set_label("<")
                    .set_style(dpp::cos_secondary)
                    .set_id("soundpad::page_prev::" + std::to_string(page) + "::" + ts + "::" +command_uid_.value());
            if (page <= 0) prev_btn.set_disabled(true);
            nav_row.add_component(prev_btn);

            // Page display
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Page " + std::to_string(page + 1) +
                            " / " + std::to_string(soundpad_pagination_.value().total_pages))
                    .set_style(dpp::cos_secondary)
                    .set_id("soundpad::page_display::" + std::to_string(page) + "::" + ts + "::" +command_uid_.value())
                    .set_disabled(true)
            );

            // Next button (disabled if fewer than per_page items were added)
            dpp::component next_btn;
            next_btn.set_type(dpp::cot_button)
                    .set_label(">")
                    .set_style(dpp::cos_secondary)
                    .set_id("soundpad::page_next::" + std::to_string(page) + "::" + ts + "::" +command_uid_.value());
            if (page >= soundpad_pagination_.value().total_pages-1) next_btn.set_disabled(true);
            nav_row.add_component(next_btn);

            rows.push_back(nav_row);
        }

        return rows;
    }

    void reply_with_button_msg(const dpp::button_click_t &event, const std::string &content) const {
        dpp::message msg(event.command.channel_id, content);
        for (auto &comp : build_soundpad_component()) {
            msg.add_component(comp);
        }
        event.reply(dpp::ir_update_message, msg);
    }

public:
    SoundpadCommand() = delete;

    SoundpadCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // get and verify params
        auto tag = std::holds_alternative<std::string>(event.get_parameter("tag"))
                ? std::get<std::string>(event.get_parameter("tag")) : std::string{};

        // query soundpad db
        int local_total_pages = 0;
        auto res = tool_interface_->listSoundpadClipsPaged(0, PAGE_SIZE, local_total_pages, tag);
        if (!res.success || ! res.data.has_value() || local_total_pages == 0) {
            if (tag.empty()) event.reply("Soundpad empty or unavailable.");
            else event.reply("Nothing found for tag: " + tag);
            return;
        }
        clean_up();
        soundpad_mappings_ = res.data.value();
        soundpad_pagination_ = PagedComponent{0, local_total_pages, tag};
        command_uid_ = random_gen_id();

        // build message with button components
        dpp::message msg(event.command.channel_id,
            "Soundpad Clips" + (tag.empty() ? "" : (" for tag: " + tag)));
        for (auto &comp : build_soundpad_component()) {
            msg.add_component(comp);
        }

        // Reply to the user with our message.
        event.reply(msg);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        // verify timestamp and command uid
        if (ids.size() != 5 || ids[0] != "soundpad") {
            event.reply("Invalid button ID for soundpad");
            return;
        }
        auto recv_ts = std::stoll(ids[3]);
        auto now_ts = std::time(nullptr);
        if (!command_uid_ || ids[4] != command_uid_.value()) {
            event.reply(dpp::ir_update_message, "This soundpad interaction is no longer valid.");
            return;
        }
        if (std::abs(now_ts - recv_ts) > 180) {
            // 3 minutes expiration
            event.reply(dpp::ir_update_message, "This soundpad interaction has expired.");
            clean_up();
            return;
        }
        if (!soundpad_mappings_ || !soundpad_pagination_) {
            event.reply(dpp::ir_update_message, "Soundpad state is invalid. Please try again.");
            clean_up();
            return;
        }

        // process commands
        auto cmd = ids[1];
        auto cmd_param = ids[2];
        if (cmd == "vol_down") {
            soundpad_volume_ = std::max(10, soundpad_volume_ - 10);
            reply_with_button_msg(event, "Volume decreased to " + std::to_string(soundpad_volume_));
        } else if (cmd == "vol_up") {
            soundpad_volume_ = std::min(100, soundpad_volume_ + 10);
            reply_with_button_msg(event, "Volume increased to " + std::to_string(soundpad_volume_));
        } else if (cmd == "page_prev") {
            soundpad_pagination_->current_page = std::max(0, soundpad_pagination_->current_page - 1);
            auto res = tool_interface_->listSoundpadClipsPaged(
                                        soundpad_pagination_->current_page, PAGE_SIZE,
                                        soundpad_pagination_->total_pages, soundpad_pagination_->tag);
            if (!res.success || ! res.data.has_value() || soundpad_pagination_->total_pages == 0) {
                event.reply(dpp::ir_update_message, "Fetch data failed");
                clean_up();
                return;
            }
            soundpad_mappings_.reset();
            soundpad_mappings_ = res.data.value();
            reply_with_button_msg(event, "Page " + std::to_string(soundpad_pagination_->current_page + 1));
        } else if (cmd == "page_next") {
            soundpad_pagination_->current_page = std::min(soundpad_pagination_->total_pages - 1, soundpad_pagination_->current_page + 1);
            auto res = tool_interface_->listSoundpadClipsPaged(
                                        soundpad_pagination_->current_page, PAGE_SIZE,
                                        soundpad_pagination_->total_pages, soundpad_pagination_->tag);
            if (!res.success || ! res.data.has_value() || soundpad_pagination_->total_pages == 0) {
                event.reply(dpp::ir_update_message, "Fetch data failed");
                clean_up();
                return;
            }
            soundpad_mappings_.reset();
            soundpad_mappings_ = res.data.value();
            reply_with_button_msg(event, "Page " + std::to_string(soundpad_pagination_->current_page + 1));
        } else if (cmd == "play") {
            if (!joinVoiceChannel(event)) {
                return;
            }
            int clip_id = std::stoi(cmd_param);
            auto res = tool_interface_->playSoundpadClip(clip_id, soundpad_volume_);
            if (!res.success) {
                event.reply(dpp::ir_update_message, "Failed to play clip ID: " + std::to_string(clip_id));
                return;
            }
            reply_with_button_msg(event, "Play clip: " + soundpad_mappings_->at(clip_id));
        } else {
            event.reply(dpp::ir_update_message, "Unknown command");
        }
    }

};

#endif //DASH_N_BARK_SOUNDPADCOMMAND_H