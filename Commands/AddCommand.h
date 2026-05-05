//
// Created by laojk on 2026-02-02.
//

#ifndef DASH_N_BARK_ADDCOMMAND_H
#define DASH_N_BARK_ADDCOMMAND_H

#include "CommandBase.h"

#include <dpp/unicode_emoji.h>
#include <mutex>
#include <ctime>
#include <future>

class AddCommand : public CommandBase {
private:
    struct AddSession {
        std::optional<AudioMixer::AudioClip> clip;
        std::string name;
        std::string user_id;
        std::string tag1;
        std::string tag2;
        bool pin;
        std::time_t created_at;
        std::string invoker_name;
        std::string uid;
    };

    static constexpr int SESSION_EXPIRE_SECS = 180;  // 3 minutes

    std::optional<AddSession> active_session_;
    mutable std::mutex session_mutex_;

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

    static auto assembleButton(const std::string& uid) -> dpp::component {
        auto ts = std::to_string(static_cast<long long>(std::time(nullptr)));
        return dpp::component().set_type(dpp::cot_action_row)
            .add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Preview")
                    .set_emoji(dpp::unicode_emoji::smile)
                    .set_style(dpp::cos_secondary)
                    .set_id("add::Preview::" + ts + "::" + uid)
            )
            .add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Confirm")
                    .set_emoji(dpp::unicode_emoji::white_check_mark)
                    .set_style(dpp::cos_success)
                    .set_id("add::Confirm::" + ts + "::" + uid)
            )
            .add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Cancel")
                    .set_emoji(dpp::unicode_emoji::x)
                    .set_style(dpp::cos_danger)
                    .set_id("add::Cancel::" + ts + "::" + uid)
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

        // Offload blocking I/O to a separate thread
        auto tool = tool_interface_;
        auto future = std::async(std::launch::async, [tool, url]() {
            return tool->fetchSoundFromUrl(url);
        });

        // Yield the worker thread while waiting for I/O
        auto timed_sched = tool_interface_->getTimedScheduler();
        auto pool_sched = tool_interface_->getPoolScheduler();
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            co_await (exec::schedule_after(timed_sched, std::chrono::milliseconds(100))
                      | stdexec::continues_on(pool_sched));
        }

        auto fetch_res = future.get();
        if (!fetch_res.success || !fetch_res.data) {
            event.edit_original_response(dpp::message("Failed to fetch sound for " + fetch_res.message +
                        " with err code " + std::to_string(fetch_res.error_code)));
            co_return;
        }
        if (fetch_res.data.value().getSize()  < start_pos + buffer_length) {
            event.edit_original_response(dpp::message("The fetched sound is shorter than the specified end time."));
            co_return;
        }

        std::string uid = random_gen_id();
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            if (active_session_) {
                event.edit_original_response(dpp::message(
                    "Another /add session is already active (by " + active_session_->invoker_name +
                    "). Please wait for it to complete or expire."));
                co_return;
            }
            AddSession session;
            session.clip = std::optional(AudioMixer::AudioClip(std::move(fetch_res.data.value()), start_pos, buffer_length));
            session.name = name;
            session.user_id = user_id;
            session.tag1 = tag1;
            session.tag2 = tag2;
            session.pin = pin;
            session.created_at = std::time(nullptr);
            session.invoker_name = get_user_name_from_event(event);
            session.uid = uid;
            active_session_ = std::move(session);
        }

        char dur_buf[32];
        std::snprintf(dur_buf, sizeof(dur_buf), "%.3g", end_sec - start_sec);
        dpp::message msg(event.command.channel_id, "You are adding a new sound clip:\n"
                                                 "Name: " + name + "\n"
                                                 "Tags: " + tag1 + " " + tag2 + "\n"
                                                 "Pinned: " + (pin ? "Yes" : "No") + "\n"
                                                 "Duration: " + std::string(dur_buf) + " seconds");

        msg.add_component(assembleButton(uid));
        event.edit_original_response(msg);
        co_return;
    }

    exec::task<void> button(dpp::button_click_t event, std::shared_ptr<dpp::cluster> bot) override {
        auto ids = parseButtonId(event.custom_id);
        if (ids.size() != 4 || ids[0] != "add") {
            event.reply(dpp::ir_update_message, "Invalid button ID for add");
            co_return;
        }

        const std::string& cmd = ids[1];
        auto recv_ts = std::stoll(ids[2]);
        const std::string& uid = ids[3];
        auto now_ts = std::time(nullptr);

        if (now_ts - recv_ts > SESSION_EXPIRE_SECS) {
            event.reply(dpp::ir_update_message, "This add session has expired.");
            std::lock_guard<std::mutex> lock(session_mutex_);
            if (active_session_ && active_session_->uid == uid) {
                active_session_.reset();
            }
            co_return;
        }

        if (cmd == "Preview") {
            std::optional<AudioMixer::AudioClip> clip_copy;
            std::string name_copy;
            {
                std::lock_guard<std::mutex> lock(session_mutex_);
                if (!active_session_ || active_session_->uid != uid) {
                    event.reply(dpp::ir_update_message, "This add session is no longer valid.");
                    co_return;
                }
                clip_copy = active_session_->clip.value();
                name_copy = active_session_->name;
            }

            if (!co_await joinVoiceChannel(event, true)) {
                co_return;
            }
            tool_interface_->playAudioClip(clip_copy.value(), AudioMixer::AudioMixer::AUDIO_EFFECT);
            dpp::message msg(event.command.channel_id, "Previewing the audio clip " + name_copy);
            msg.add_component(assembleButton(uid));
            event.edit_original_response(msg);
        } else if (cmd == "Confirm") {
            std::optional<AudioMixer::AudioClip> clip_copy;
            std::string name, user_id, tag1, tag2;
            bool pin;
            {
                std::lock_guard<std::mutex> lock(session_mutex_);
                if (!active_session_ || active_session_->uid != uid) {
                    event.reply(dpp::ir_update_message, "This add session is no longer valid.");
                    co_return;
                }
                clip_copy = active_session_->clip.value();
                name = active_session_->name;
                user_id = active_session_->user_id;
                tag1 = active_session_->tag1;
                tag2 = active_session_->tag2;
                pin = active_session_->pin;
                active_session_.reset();
            }

            auto add_res = tool_interface_->addToSoundpad(clip_copy.value(), name, user_id, tag1, tag2, pin);
            if (!add_res.success) {
                event.reply(dpp::ir_update_message, "Failed to add to soundpad: " + add_res.message);
            } else {
                event.reply(dpp::ir_update_message, "Added " + name + " to soundpad. Session complete.");
            }
        } else if (cmd == "Cancel") {
            {
                std::lock_guard<std::mutex> lock(session_mutex_);
                if (active_session_ && active_session_->uid == uid) {
                    active_session_.reset();
                }
            }
            event.reply(dpp::ir_update_message, "Operation cancelled. Session complete.");
        } else {
            spdlog::info("Unknown action: {} {} {} {}", ids[0], ids[1], ids[2], ids[3]);
            event.reply(dpp::ir_update_message, "Unknown action.");
        }
        co_return;
    }

};

#endif //DASH_N_BARK_ADDCOMMAND_H
