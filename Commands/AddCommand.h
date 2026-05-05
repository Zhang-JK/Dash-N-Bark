//
// Created by laojk on 2026-02-02.
//

#ifndef DASH_N_BARK_ADDCOMMAND_H
#define DASH_N_BARK_ADDCOMMAND_H

#include "CommandBase.h"

#include <dpp/unicode_emoji.h>

class AddCommand : public CommandBase {
private:
    struct AddParams {
        std::string name;
        std::string user_id;
        std::string tag1;
        std::string tag2;
        bool pin;
    };
    std::optional<AddParams> params_;
    std::optional<AudioMixer::AudioClip> processing_clip_;


public:
    AddCommand() = delete;

    AddCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    // helper function
    static auto parseTime(const std::string &s) -> double {
        if (s.empty()) return -1.0;
        size_t pos = s.find(':');
        try {
            if (pos == std::string::npos) {
                // Allow digits and at most one decimal point
                bool has_dot = false;
                for (char c : s) {
                    if (c == '.') {
                        if (has_dot) return -1.0;
                        has_dot = true;
                    } else if (!std::isdigit(static_cast<unsigned char>(c))) {
                        return -1.0;
                    }
                }
                return std::stod(s);
            } else {
                std::string a = s.substr(0, pos);
                std::string b = s.substr(pos + 1);
                if (a.empty() || b.empty()) return -1.0;
                // Minutes part: digits only
                for (char c : a) if (!std::isdigit(static_cast<unsigned char>(c))) return -1.0;
                // Seconds part: digits with optional single decimal point
                bool has_dot = false;
                for (char c : b) {
                    if (c == '.') {
                        if (has_dot) return -1.0;
                        has_dot = true;
                    } else if (!std::isdigit(static_cast<unsigned char>(c))) {
                        return -1.0;
                    }
                }
                int A = std::stoi(a);
                double B = std::stod(b);
                if (B < 0.0 || B >= 60.0) return -1.0; // seconds part must be 0..59
                return A * 60.0 + B;
            }
        } catch (...) {
            return -1.0;
        }
    }

    static auto timeToBufferIndex(double time_sec, int sample_rate) -> size_t {
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

    exec::task<void> execute(dpp::slashcommand_t event, std::shared_ptr<dpp::cluster> bot) override {
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
        auto user_id = event.command.usr.id.str();

        if (url.empty() || name.empty()) {
            event.reply("URL and name are required parameters.");
            co_return;
        }
        double start_sec = parseTime(start);
        double end_sec = parseTime(end);
        if (start_sec < 0.0 || end_sec < 0.0) {
            event.reply("`start` and `end` must be in `{seconds}` or `{mm}:{ss}` format (e.g. `13.67` or `1:4.25`).");
            co_return;
        }
        if (end_sec <= start_sec) {
            event.reply("`end` must be greater than `start`.");
            co_return;
        }
        auto start_pos = timeToBufferIndex(start_sec, AudioMixer::BYTES_PER_SEC_DEFAULT);
        auto buffer_length = timeToBufferIndex(end_sec - start_sec, AudioMixer::BYTES_PER_SEC_DEFAULT);

        event.reply(dpp::ir_deferred_channel_message_with_source, "Fetching sound from URL...");
        auto fetch_res = tool_interface_->fetchSoundFromUrl(url);
        if (!fetch_res.success || !fetch_res.data) {
            event.edit_original_response(dpp::message("Failed to fetch sound for " + fetch_res.message +
                        " with err code " + std::to_string(fetch_res.error_code)));
            co_return;
        }
        if (fetch_res.data.value().getSize()  < start_pos + buffer_length) {
            event.edit_original_response(dpp::message("The fetched sound is shorter than the specified end time."));
            co_return;
        }
        if (processing_clip_) {
            processing_clip_.reset();
        }
        if (params_) {
            params_.reset();
        }
        processing_clip_ = std::optional(
            AudioMixer::AudioClip(std::move(fetch_res.data.value()), start_pos, buffer_length));
        params_ = AddParams{name, user_id, tag1, tag2, pin};

        char dur_buf[32];
        std::snprintf(dur_buf, sizeof(dur_buf), "%.3g", end_sec - start_sec);
        dpp::message msg(event.command.channel_id, "You are adding a new sound clip:\n"
                                                 "Name: " + name + "\n"
                                                 "Tags: " + tag1 + " " + tag2 + "\n"
                                                 "Pinned: " + (pin ? "Yes" : "No") + "\n"
                                                 "Duration: " + std::string(dur_buf) + " seconds");

        msg.add_component(assembleButton());
        event.edit_original_response(msg);
        co_return;
    }

    exec::task<void> button(dpp::button_click_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 3 || ids[0] != "add") {
            event.reply("Invalid button ID for add");
            co_return;
        }
        if (!processing_clip_ || !params_) {
            event.reply(dpp::ir_update_message,
                "No active add session, please use /add command to start a new session.");
            processing_clip_.reset();
            params_.reset();
            spdlog::warn("No active add session when handling button click");
            co_return;
        }
        if (ids[1] == "Preview") {
            if (!co_await joinVoiceChannel(event, true)) {
                co_return;
            }
            tool_interface_->playAudioClip(processing_clip_.value(), AudioMixer::AudioMixer::AUDIO_EFFECT);
            dpp::message msg(event.command.channel_id,
                    "Previewing the audio clip " + params_.value().name);
            msg.add_component(assembleButton());
            event.edit_original_response(msg);
        } else if (ids[1] == "Confirm") {
            auto add_res = tool_interface_->addToSoundpad(processing_clip_.value(),
                                                                                    params_.value().name,
                                                                                    params_.value().user_id,
                                                                                    params_.value().tag1,
                                                                                    params_.value().tag2,
                                                                                    params_.value().pin);
            if (!add_res.success) {
                event.reply(dpp::ir_update_message, "Failed to add to soundpad: " + add_res.message);
            } else {
                event.reply(dpp::ir_update_message, "Added " + params_.value().name +
                    " to soundpad, This session is done");
            }
            processing_clip_.reset();
            params_.reset();
        } else if (ids[1] == "Cancel") {
            processing_clip_.reset();
            params_.reset();
            event.reply(dpp::ir_update_message, "Operation cancelled, This session is done");
        } else {
            spdlog::info("Unknown action: {} {} {}", ids[0], ids[1], ids[2]);
            event.reply(dpp::ir_update_message, "Unknown action.");
        }
        co_return;
    }

};

#endif //DASH_N_BARK_ADDCOMMAND_H