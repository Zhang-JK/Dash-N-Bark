//
// Created by laojk on 2026-01-15.
//

#ifndef DASH_N_BARK_COMMANDBASE_H
#define DASH_N_BARK_COMMANDBASE_H
#include <memory>

#include <dpp/dpp.h>
#include "../ToolInterface.h"

class CommandBase {
public:
    explicit CommandBase(std::shared_ptr<ToolInterface> tool_interface)
        : tool_interface_(std::move(tool_interface)) {}

    virtual ~CommandBase() = default;

    virtual void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) = 0;

    void operator()(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) {
        execute(event, std::move(bot));
    }
private:
    std::shared_ptr<ToolInterface> tool_interface_;
};


#endif //DASH_N_BARK_COMMANDBASE_H