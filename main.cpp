//
// Created by laojk on 2025-11-17.
//

#include <dpp/dpp.h>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <Audio-Mixer/AudioMixer.h>
#include "Audio-Mixer/SoundPadManager.h"

int main() {
    AudioMixer::SoundPadManager spm;
    if (!spm.initialize("../testdata/soundpad.db", "../testdata/sounds/")) {
        std::cerr << "Failed to initialize SoundPadManager" << std::endl;
        return 1;
    }

    // read token from config.json file
    nlohmann::json config;
    std::string token;
    try {
        std::ifstream cfg_file("../testdata/config.json");
        if (!cfg_file.is_open()) {
            std::cerr << "Failed to open `../testdata/config.json`" << std::endl;
            return 1;
        }
        config = nlohmann::json::parse(cfg_file);
        token = config.value("token", std::string());
        if (token.empty()) {
            std::cerr << "Token missing in `../testdata/config.json`" << std::endl;
            return 1;
        }
    } catch (const nlohmann::json::parse_error &e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "Error reading config: " << e.what() << std::endl;
        return 1;
    }

    AudioMixer::AudioClip clip1("../testdata/Robot.pcm", AudioMixer::AudioBuffer::PCM_16BIT_STEREO_44K);
    AudioMixer::AudioClip clip2("../testdata/test.pcm", AudioMixer::AudioBuffer::PCM_16BIT_STEREO_44K);
    auto mixer = AudioMixer::AudioMixer();

    // if (!spm.saveAudioClip(clip1, "Robot", "effect")) {
    //     std::cerr << "Failed to save audio clip 'Robot'" << std::endl;
    //     return 1;
    // }
    // if (!spm.saveAudioClip(clip2, "说的道理", "电棍", "effect")) {
    //     std::cerr << "Failed to save audio clip '说的道理'" << std::endl;
    //     return 1;
    // }

    uint8_t *robot = nullptr;
    uint8_t *otto = nullptr;
    size_t robot_size = 0;
    size_t otto_size = 0;
    std::ifstream input("../testdata/Robot.pcm", std::ios::in | std::ios::binary | std::ios::ate);
    if (input.is_open()) {
        robot_size = input.tellg();
        robot = new uint8_t[robot_size];
        input.seekg(0, std::ios::beg);
        input.read((char *) robot, robot_size);
        input.close();
    } else {
        throw std::runtime_error("Failed to open `../testdata/Robot.pcm`");
    }
    std::ifstream input2("../testdata/test.pcm", std::ios::in | std::ios::binary | std::ios::ate);
    if (input2.is_open()) {
        otto_size = input2.tellg();
        otto = new uint8_t[otto_size];
        input2.seekg(0, std::ios::beg);
        input2.read((char *) otto, otto_size);
        input2.close();
    } else {
        throw std::runtime_error("Failed to open `../testdata/test.pcm`");
    }
    if (otto_size % 4 != 0) {
        throw std::runtime_error("Invalid PCM file length, must be multiple of 4");
    }

    uint8_t *mixed = new uint8_t[std::max(robot_size, otto_size)];
    size_t mixed_size = std::max(robot_size, otto_size);
    for (size_t i = 0; i < mixed_size / 2; ++i) {
        int16_t sample1 = 0;
        int16_t sample2 = 0;
        if (i * 2 + 1 < robot_size) {
            sample1 = *(int16_t *)(robot + i * 2);
        }
        if (i * 2 + 1 < otto_size) {
            sample2 = *(int16_t *)(otto + i * 2);
        }
        int32_t mixed_sample = (static_cast<int32_t>(sample1) + static_cast<int32_t>(sample2)) / 2;
        if (mixed_sample > INT16_MAX) {
            mixed_sample = INT16_MAX;
        } else if (mixed_sample < INT16_MIN) {
            mixed_sample = INT16_MIN;
        }
        *(int16_t *)(mixed + i * 2) = static_cast<int16_t>(mixed_sample);
    }

    /* Setup the bot */
    dpp::cluster bot(token);

    bot.on_log(dpp::utility::cout_logger());

    /* The event is fired when someone issues your commands */
    bot.on_slashcommand([&bot, robot, robot_size, otto, otto_size, mixed, mixed_size, &mixer, &clip1, &clip2, &spm](const dpp::slashcommand_t &event) {
        /* Check which command they ran */
        if (event.command.get_command_name() == "join") {
            /* Get the guild */
            dpp::guild *g = dpp::find_guild(event.command.guild_id);

            /* Attempt to connect to a voice channel, returns false if we fail to connect. */
            if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
                event.reply("You don't seem to be in a voice channel!");
                return;
            }

            /* Tell the user we joined their channel. */
            event.reply("Joined your channel!");
        } else if (event.command.get_command_name() == "robot") {
            /* Get the voice channel the bot is in, in this current guild. */
            dpp::voiceconn *v = event.from()->get_voice(event.command.guild_id);

            /* If the voice channel was invalid, or there is an issue with it, then tell the user. */
            if (!v || !v->voiceclient || !v->voiceclient->is_ready()) {
                event.reply("There was an issue with getting the voice channel. Make sure I'm in a voice channel!");
                return;
            }

            /* Tell the bot to play the sound file 'Robot.pcm' in the current voice channel. */
            auto data = spm.loadAudioClip(1);
            if (!data.has_value()) {
                event.reply("Failed to load audio clip from database.");
                return;
            }
            v->voiceclient->send_audio_raw((uint16_t *) data.value()->getData(), data.value()->getSize());
            auto data2 = spm.loadAudioClip("说的道理");
            if (!data2.has_value()) {
                event.reply("Failed to load audio clip from database.");
                return;
            }
            v->voiceclient->send_audio_raw((uint16_t *) data2.value()->getData(), data2.value()->getSize());

            event.reply("Played robot.");
        } else if (event.command.get_command_name() == "play111") {
            /* Get the voice channel the bot is in, in this current guild. */
            dpp::voiceconn *v = event.from()->get_voice(event.command.guild_id);

            /* If the voice channel was invalid, or there is an issue with it, then tell the user. */
            if (!v || !v->voiceclient || !v->voiceclient->is_ready()) {
                event.reply("There was an issue with getting the voice channel. Make sure I'm in a voice channel!");
                return;
            }

            mixer.registerAudio(clip1);
            auto step1 = mixer.step();
            std::cout << "step1 size" << step1->getSize() << std::endl;
            v->voiceclient->send_audio_raw((uint16_t *)step1->getData(), step1->getSize());
            mixer.registerAudio(clip2,0.5f);
            auto step2 = mixer.step();
            std::cout << "step2 size" << step2->getSize() << std::endl;
            v->voiceclient->send_audio_raw((uint16_t *)step2->getData(), step2->getSize());
            mixer.registerAudio(clip2, 1.5f);
            auto step3 = mixer.step();
            std::cout << "step3 size" << step3->getSize() << std::endl;
            v->voiceclient->send_audio_raw((uint16_t *)step3->getData(), step3->getSize());
            while (auto step = mixer.step()) {
                std::cout << "step size" << step->getSize() << std::endl;
                v->voiceclient->send_audio_raw((uint16_t *)step->getData(), step->getSize());
            }
            event.reply("Played robot.");
        }
    });

    bot.on_ready([&bot](const dpp::ready_t &event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            /* Create a new command. */
            dpp::slashcommand joincommand("join", "Joins your voice channel.", bot.me.id);

            dpp::slashcommand robotcommand("robot", "Plays a robot noise in your voice channel.", bot.me.id);

            dpp::slashcommand playcommand("play111", "Plays mixed audio in your voice channel.", bot.me.id);

            bot.global_bulk_command_create({joincommand, robotcommand, playcommand});
        }
    });

    /* Start bot */
    bot.start(dpp::st_wait);

    delete [] robot;

    return 0;
}
