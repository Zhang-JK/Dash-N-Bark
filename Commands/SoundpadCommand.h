//
// Created by laojk on 2/1/26.
//

#ifndef DASH_N_BARK_SOUNDPADCOMMAND_H
#define DASH_N_BARK_SOUNDPADCOMMAND_H

#include "CommandBase.h"

#include <dpp/unicode_emoji.h>

class SoundpadCommand : public CommandBase {
public:
    SoundpadCommand() = delete;

    SoundpadCommand(std::shared_ptr<ToolInterface> tool_interface)
        : CommandBase(std::move(tool_interface)) {}

    void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) override {
        /* Create a message */
        dpp::message msg(event.command.channel_id, "this text has a button");

        /* Add an action row, and then a button within the action row. */
        msg.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_label("Click me!")
                    .set_type(dpp::cot_button)
                    .set_emoji(dpp::unicode_emoji::smile)
                    .set_style(dpp::cos_danger)
                    .set_id("soundpad::test")
            )
        );

        /* Reply to the user with our message. */
        event.reply(msg);
    }

    void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) override {
        event.reply("You clicked: " + event.custom_id);
    }

};

#endif //DASH_N_BARK_SOUNDPADCOMMAND_H