#include <charconv>
#include <cmath>

#include "game/control_commands.h"

namespace pt::game::control {

std::string JsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    char text[32];
    if (value == std::floor(value) && std::abs(value) < 9007199254740992.0) {
        const auto result = std::to_chars(text, text + sizeof(text), static_cast<long long>(value));
        return std::string(text, result.ptr);
    }
    const auto result = std::to_chars(text, text + sizeof(text), value);
    return std::string(text, result.ptr);
}

void JsonWriter::Key(const char* key) {
    if (!open_.empty()) {
        if (!open_.back().second) {
            text_ += ',';
        }
        open_.back().second = false;
    }
    if (key && !open_.empty() && open_.back().first == '}') {
        text_ += ControlServer::JsonString(key);
        text_ += ':';
    }
}

void JsonWriter::Number(const char* key, double value) {
    Key(key);
    text_ += JsonNumber(value);
}

void JsonWriter::Bool(const char* key, bool value) {
    Key(key);
    text_ += value ? "true" : "false";
}

void JsonWriter::String(const char* key, std::string_view value) {
    Key(key);
    text_ += ControlServer::JsonString(value);
}

void JsonWriter::BeginObject(const char* key) {
    Key(key);
    text_ += '{';
    open_.emplace_back('}', true);
}

void JsonWriter::BeginArray(const char* key) {
    Key(key);
    text_ += '[';
    open_.emplace_back(']', true);
}

void JsonWriter::End() {
    if (open_.empty()) {
        return;
    }
    text_ += open_.back().first;
    open_.pop_back();
}

std::vector<std::string_view> Words(std::string_view line) {
    std::vector<std::string_view> words;
    size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
            ++at;
        }
        const size_t start = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t') {
            ++at;
        }
        if (at > start) {
            words.push_back(line.substr(start, at - start));
        }
    }
    return words;
}

std::optional<float> ParseNumber(std::string_view word) {
    if (!word.empty() && word.front() == '+') {
        word.remove_prefix(1);
    }
    double value = 0.0;
    const auto result = std::from_chars(word.data(), word.data() + word.size(), value);
    if (word.empty() || result.ec != std::errc() || result.ptr != word.data() + word.size() || !std::isfinite(value) ||
        std::abs(value) > 1.0e9) {
        return std::nullopt;
    }
    return static_cast<float>(value);
}

std::optional<bool> ParseSwitch(std::string_view word) {
    if (word == "on" || word == "1" || word == "true") {
        return true;
    }
    if (word == "off" || word == "0" || word == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::optional<float>> ParseNumberOrOff(std::string_view word) {
    if (word == "off") {
        return std::optional<float>();
    }
    if (const auto value = ParseNumber(word)) {
        return std::optional<float>(*value);
    }
    return std::nullopt;
}

const std::vector<std::string>& CommandList() {
    static const std::vector<std::string> commands = {
        "help",
        "status",
        "stages",
        "state",
        "player",
        "place <x> <y> <z> [yaw]",
        "turn <yaw> [pitch]",
        "hover <height>|here|off",
        "gravity on|off",
        "collision on|off",
        "input on|off",
        "walkspeed <factor>",
        "light",
        "light reset",
        "light <name> <value> ... (innerRange outerRange temperature lumen lightSize umbraAngle penumbraAngle attenuationExponent "
        "dimmer powerScale r g b; enable on|off)",
        "exposure",
        "exposure reset",
        "exposure <min|max|compensation|ev> <value>|off ...",
        "camera",
        "camera off",
        "camera <x|y|z|yaw|pitch|roll|fov> <value> ... [body on|off]",
        "demospeed <factor>",
        "entity <stage> <name>",
        "entity <stage> <name> <enable|visible|geom> on|off ...",
        "entities <stage> [prefix|*] [limit]",
        "floor <name>",
        "loop <index>|<floor> [pass]",
    };
    return commands;
}

std::string Info(std::string_view version) {
    JsonWriter out;
    out.BeginObject(nullptr);
    out.Number("protocol", ControlServer::kProtocol);
    out.String("game", "pt-port");
    out.String("version", version);
    out.BeginArray("commands");
    for (const std::string& command : CommandList()) {
        out.String(nullptr, command);
    }
    out.End();
    out.End();
    return out.Text();
}

}
