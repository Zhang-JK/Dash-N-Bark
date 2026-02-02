//
// Created by laojk on 2026-01-15.
//

#ifndef DASH_N_BARK_COMMANDBASE_H
#define DASH_N_BARK_COMMANDBASE_H
#include <memory>

#include <dpp/dpp.h>
#include <spdlog/spdlog.h>
#include "../ToolInterface.h"

class CommandBase {
public:
    explicit CommandBase(std::shared_ptr<ToolInterface> tool_interface)
        : tool_interface_(std::move(tool_interface)) {
        assert(tool_interface_ != nullptr && "ToolInterface pointer cannot be nullCommandBase()");
    }

    virtual ~CommandBase() = default;

    virtual void execute(const dpp::slashcommand_t &event, std::shared_ptr<dpp::cluster> bot) = 0;
    virtual void button(const dpp::button_click_t &event, std::shared_ptr<dpp::cluster> bot) = 0;

protected:
    static std::vector<std::string> parseButtonId(const std::string &button_id, const std::string &delimiter = "::") {
        std::vector<std::string> parts;
        if (delimiter.empty()) {
            return parts;
        }
        size_t start = 0;
        size_t end = button_id.find(delimiter, start);
        while (end != std::string::npos) {
            parts.emplace_back(button_id.substr(start, end - start));
            start = end + delimiter.size();
            end = button_id.find(delimiter, start);
        }
        if (start <= button_id.size()) {
            parts.emplace_back(button_id.substr(start));
        }
        return parts;
    }

    std::shared_ptr<ToolInterface> tool_interface_;
};


#endif //DASH_N_BARK_COMMANDBASE_H