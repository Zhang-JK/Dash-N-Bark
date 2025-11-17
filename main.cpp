//
// Created by laojk on 2025-11-17.
//

#include <dpp/dpp.h>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

int main() {
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

    uint8_t *robot = nullptr;
    size_t robot_size = 0;
    std::ifstream input("../testdata/Robot.pcm", std::ios::in | std::ios::binary | std::ios::ate);
    if (input.is_open()) {
        robot_size = input.tellg();
        robot = new uint8_t[robot_size];
        input.seekg(0, std::ios::beg);
        input.read((char *) robot, robot_size);
        input.close();
    }

    /* Setup the bot */
    dpp::cluster bot(token);

    bot.on_log(dpp::utility::cout_logger());

    /* The event is fired when someone issues your commands */
    bot.on_slashcommand([&bot, robot, robot_size](const dpp::slashcommand_t &event) {
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
            v->voiceclient->send_audio_raw((uint16_t *) robot, robot_size);

            event.reply("Played robot.");
        }
    });

    bot.on_ready([&bot](const dpp::ready_t &event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            /* Create a new command. */
            dpp::slashcommand joincommand("join", "Joins your voice channel.", bot.me.id);

            dpp::slashcommand robotcommand("robot", "Plays a robot noise in your voice channel.", bot.me.id);

            bot.global_bulk_command_create({joincommand, robotcommand});
        }
    });

    /* Start bot */
    bot.start(dpp::st_wait);

    return 0;
}
