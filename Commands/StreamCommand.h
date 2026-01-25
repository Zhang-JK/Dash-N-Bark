//
// Created by laojk on 1/25/26.
//

#ifndef DASH_N_BARK_STREAMCOMMAND_H
#define DASH_N_BARK_STREAMCOMMAND_H

#include "CommandBase.h"

class StreamCommand : public CommandBase {
public:
    StreamCommand() = delete;

    StreamCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            return;
        }

        auto url = std::get<std::string>(event.get_parameter("url"));
        int volume = 100;
        if (std::holds_alternative<int64_t>(event.get_parameter("volume"))) {
            volume = static_cast<int>(std::get<int64_t>(event.get_parameter("volume")));
        }
        spdlog::debug("Got user requested url {} volume {}", url.c_str(), volume);

        auto tool_res = tool_interface_->fetchAudioFromUrl(url);
        if (!tool_res.success) {
            event.reply("Failed to fetch with error code " + std::to_string(tool_res.error_code) + ": " + tool_res.message);
            return;
        }
        AudioMixer::AudioClip clip(tool_res.data.path.value(), AudioMixer::AudioBuffer::PCM_16BIT_STEREO_48K);

        dpp::voiceconn* vc_bot = event.from()->get_voice(event.command.guild_id);
        if (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready()) {
            event.reply("Bot does not in any channel. Try to join");
            if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
                event.edit_response("You don't seem to be in a voice channel!");
                return;
            }
            // wait for ready
            auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(3000);
            do {
                vc_bot = event.from()->get_voice(event.command.guild_id);
                if (std::chrono::steady_clock::now() - start > timeout) {
                    event.edit_response("Timeout waiting for voice client to become ready.");
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (!vc_bot || !vc_bot->voiceclient || !vc_bot->voiceclient->is_ready());
        }

        vc_bot->voiceclient->set_send_audio_type(dpp::discord_voice_client::satype_live_audio);
        event.edit_response("Streaming " + tool_res.data.title);
        // hack testing for 180s
        const int iterations = 3000;
        std::chrono::duration<double, std::milli> total_ms(0);
        for (int i=0; i < iterations; i++) {
            auto start_stream = std::chrono::steady_clock::now();
            vc_bot->voiceclient->send_audio_raw(reinterpret_cast<uint16_t *>(clip.getData() + 11520*i), 11520);
            auto end_stream = std::chrono::steady_clock::now();
            auto elapsed = end_stream - start_stream;
            total_ms += std::chrono::duration<double, std::milli>(elapsed);
            spdlog::debug("Stream iteration {} elapsed: {} ms", i, elapsed.count() / 1000000.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(58));
        }
        double avg_ms = total_ms.count() / iterations;
        spdlog::info("Average stream elapsed: {} ms over {} iterations", avg_ms, iterations);
    }
};

#endif //DASH_N_BARK_STREAMCOMMAND_H