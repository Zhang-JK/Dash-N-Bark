//
// Created by laojk on 2026-02-02.
//

#ifndef DASH_N_BARK_ADDCOMMAND_H
#define DASH_N_BARK_ADDCOMMAND_H

#include "CommandBase.h"

#include <dpp/unicode_emoji.h>

class AddCommand : public CommandBase {
private:
    std::optional<AudioMixer::AudioClip> processing_clip_;

public:
    AddCommand() = delete;

    AddCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    // helper function
    static auto parseTime(const std::string &s) -> int {
        if (s.empty()) return -1;
        size_t pos = s.find(':');
        try {
            if (pos == std::string::npos) {
                for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
                return std::stoi(s);
            } else {
                std::string a = s.substr(0, pos);
                std::string b = s.substr(pos + 1);
                if (a.empty() || b.empty()) return -1;
                for (char c : a) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
                for (char c : b) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
                int A = std::stoi(a);
                int B = std::stoi(b);
                if (B < 0 || B >= 60) return -1; // seconds part must be 0..59
                return A * 60 + B;
            }
        } catch (...) {
            return -1;
        }
    }

    static auto timeToBufferIndex(int time_sec, int sample_rate) -> size_t {
        return static_cast<size_t>(time_sec * sample_rate);
    }

    static auto assembleButton() -> dpp::component {
        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
        return dpp::component().set_type(dpp::cot_action_row)
            .add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Preview")
                    .set_emoji(dpp::unicode_emoji::smile)
                    .set_style(dpp::cos_secondary)
                    .set_id("add::Preview::" + ts)
            )
            .add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Confirm")
                    .set_emoji(dpp::unicode_emoji::white_check_mark)
                    .set_style(dpp::cos_success)
                    .set_id("add::Confirm::" + ts)
            )
            .add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Cancel")
                    .set_emoji(dpp::unicode_emoji::x)
                    .set_style(dpp::cos_danger)
                    .set_id("add::Cancel::" + ts)
            );
    }

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // extract parameters
        auto url = std::get<std::string>(event.get_parameter("url"));
        auto start = std::get<std::string>(event.get_parameter("start"));
        auto end = std::get<std::string>(event.get_parameter("end"));
        auto name = std::get<std::string>(event.get_parameter("name"));
        auto tag1 = std::holds_alternative<std::string>(event.get_parameter("tag1"))
                ? std::get<std::string>(event.get_parameter("tag1")) : std::string{};
        auto tag2 = std::holds_alternative<std::string>(event.get_parameter("tag2"))
                ? std::get<std::string>(event.get_parameter("tag2")) : std::string{};
        auto pin = std::holds_alternative<bool>(event.get_parameter("pin"))
                ? std::get<bool>(event.get_parameter("pin")) : false;

        // verify parameters
        if (url.empty() || name.empty()) {
            event.reply("URL and name are required parameters.");
            return;
        }
        int start_sec = parseTime(start);
        int end_sec = parseTime(end);
        if (start_sec < 0 || end_sec < 0) {
            event.reply("`start` and `end` must be either `{int}` or `{int}:{int}` (mm:ss).");
            return;
        }
        if (end_sec <= start_sec) {
            event.reply("`end` must be greater than `start`.");
            return;
        }
        auto start_pos = timeToBufferIndex(start_sec, AudioMixer::BYTES_PER_SEC_DEFAULT);
        auto buffer_length = timeToBufferIndex(end_sec - start_sec, AudioMixer::BYTES_PER_SEC_DEFAULT);

        // fetch from url
        auto fetch_res = tool_interface_->fetchSoundFromUrl(url);
        if (!fetch_res.success || !fetch_res.data) {
            event.reply("Failed to fetch sound for " + fetch_res.message +
                        " with err code " + std::to_string(fetch_res.error_code));
            return;
        }
        if (fetch_res.data.value().getSize()  < start_pos + buffer_length) {
            event.reply("The fetched sound is shorter than the specified end time.");
            return;
        }
        if (processing_clip_) {
            processing_clip_.reset();
        }
        processing_clip_ = std::optional(
            AudioMixer::AudioClip(std::move(fetch_res.data.value()), start_pos, buffer_length));

        // Create a message
        dpp::message msg(event.command.channel_id, "You are adding a new sound clip:\n"
                                                 "Name: " + name + "\n"
                                                 "Tags: " + tag1 + " " + tag2 + "\n"
                                                 "Pinned: " + (pin ? "Yes" : "No") + "\n"
                                                 "Duration: " + std::to_string(end_sec - start_sec) + " seconds");

        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
        msg.add_component(assembleButton());
        event.reply(msg);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 3 || ids[0] != "add") {
            event.reply("Invalid button ID for add");
            return;
        }
        if (ids[1] == "Preview") {
            if (processing_clip_) {
                // placeholder
                dpp::message msg(event.command.channel_id,
                        "Previewing the audio clip " + std::string("{placeholder}"));
                msg.add_component(assembleButton());
                event.reply(dpp::ir_update_message, msg);
            } else {
                event.reply(dpp::ir_update_message, "Audio missing, Session is cancelled");
            }
        } else if (ids[1] == "Confirm") {
            if (processing_clip_) {
                // placeholder
                event.reply(dpp::ir_update_message, "Added " + std::string("{placeholder}") +
                        " to soundpad, This session is done");
                processing_clip_.reset();
            } else {
                event.reply(dpp::ir_update_message, "Audio missing, Session is cancelled");
            }
        } else if (ids[1] == "Cancel") {
            if (processing_clip_) {
                processing_clip_.reset();
                event.reply(dpp::ir_update_message, "Operation cancelled, This session is done");
            } else {
                event.reply(dpp::ir_update_message, "Audio missing, Session is cancelled");
            }
        } else {
            spdlog::info("Unknown action: {} {} {}", ids[0], ids[1], ids[2]);
            event.reply(dpp::ir_update_message, "Unknown action.");
        }
    }

};

#endif //DASH_N_BARK_ADDCOMMAND_H