#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/platform/control_server.h"
#include "game/debug_control.h"

namespace pt::game {

class Game;

/* The control channel's commands (docs/control.md): one line, words split by spaces, the command first. A fixed set:
   each command reads or changes one thing through game/debug_control, and no command runs code it is sent. */
namespace control {

ControlServer::Reply Run(Game& game, std::string_view line);
/* the JSON object a good hello is answered with: protocol, game, version and the command list */
std::string Info(std::string_view version);

/* the pieces Run is made of, kept apart so the tests reach them without a game */
class JsonWriter : public debug::FieldWriter {
public:
    void Number(const char* key, double value) override;
    void Bool(const char* key, bool value) override;
    void String(const char* key, std::string_view value) override;
    void BeginObject(const char* key) override;
    void BeginArray(const char* key) override;
    void End() override;
    const std::string& Text() const { return text_; }

private:
    void Key(const char* key);
    std::string text_;
    std::vector<std::pair<char, bool>> open_;
};

std::string JsonNumber(double value);
std::vector<std::string_view> Words(std::string_view line);
std::optional<float> ParseNumber(std::string_view word);
std::optional<bool> ParseSwitch(std::string_view word);
/* "off" (none) or a number; nullopt when it is neither */
std::optional<std::optional<float>> ParseNumberOrOff(std::string_view word);
const std::vector<std::string>& CommandList();

}
}
