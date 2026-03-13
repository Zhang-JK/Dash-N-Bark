//
// Created by laojk on 3/8/26.
//

#ifndef DASH_N_BARK_PARROTCOMMAND_H
#define DASH_N_BARK_PARROTCOMMAND_H

#include "CommandBase.h"
#include "Audio-Mixer/VoiceChanger.h"

class ParrotCommand : public CommandBase {
public:
    ParrotCommand() = delete;

    ParrotCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        dpp::guild *g = dpp::find_guild(event.command.guild_id);
        if (!g) {
            event.reply("Guild not found!");
            return;
        }

        auto target_user = std::get<dpp::snowflake>(event.get_parameter("target"));
        auto parroting_duration = std::holds_alternative<int64_t>(event.get_parameter("duration"))
                ? std::get<int64_t>(event.get_parameter("duration")) : 30;
        
        auto voice_preset_str = std::holds_alternative<std::string>(event.get_parameter("voice_preset"))
                ? std::get<std::string>(event.get_parameter("voice_preset")) : "little_girl";
        
        AudioMixer::VoiceChanger::VoicePreset voice_preset = AudioMixer::VoiceChanger::VoicePreset::LittleGirl;
        auto preset_map = AudioMixer::VoiceChanger::getPresetMap();
        auto it = preset_map.find(voice_preset_str);
        if (it != preset_map.end()) {
            voice_preset = it->second;
        }

        // check if target is bot itself
        if (target_user == bot->me.id) {
            event.reply("😰 what u want? an echo that will ruin my ears??");
            return;
        }

        // check if the user is in a voice channel
        auto target_user_vc = g->voice_members.find(target_user);
        auto users_vc_state = g->voice_members.find(event.command.usr.id);
        if (target_user_vc == g->voice_members.end()
            || users_vc_state == g->voice_members.end()
            || target_user_vc->second.channel_id != users_vc_state->second.channel_id) {
            event.reply("Both you and the target user must be in and in the same voice channel!");
            return;
        }

        if (!joinVoiceChannel(event)) {
            return;
        }

        auto rec_result = tool_interface_->initRecordingService(target_user.str(), parroting_duration, voice_preset);
        if (!rec_result.success) {
            event.edit_original_response(dpp::message("Failed to start recording: " + rec_result.message));
            return;
        }
        tool_interface_->playAudioFromFile("system/se-rec.pcm", AudioMixer::AudioMixer::AUDIO_EFFECT, 0.2f);

        dpp::user* target_user_obj = dpp::find_user(target_user);
        std::string target_display_name = target_user_obj
            ? target_user_obj->format_username()
            : ("<@" + target_user.str() + ">");

        event.edit_original_response(dpp::message("Parroting " + target_display_name + " with " 
                    + voice_preset_str + " voice for " + std::to_string(parroting_duration) + " seconds!"));
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        // No button interaction for this command
    }

};

#endif //DASH_N_BARK_PARROTCOMMAND_H