#include "game/control_commands.h"

#include <algorithm>
#include <cmath>

#include "game/game.h"
#include "game/loop_browser.h"

namespace pt::game::control {
namespace {

using Reply = ControlServer::Reply;
using WordList = std::vector<std::string_view>;

Reply Error(std::string_view message) {
    return {false, ControlServer::JsonString(message)};
}

Reply Usage(std::string_view usage) {
    return Error("usage: " + std::string(usage));
}

Reply Describe(Game& game, void (*describe)(Game&, debug::FieldWriter&)) {
    JsonWriter out;
    describe(game, out);
    return {true, out.Text()};
}

Reply Help() {
    JsonWriter out;
    out.BeginArray(nullptr);
    for (const std::string& command : CommandList()) {
        out.String(nullptr, command);
    }
    out.End();
    return {true, out.Text()};
}

Reply Place(Game& game, const WordList& w) {
    if (w.size() != 4 && w.size() != 5) {
        return Usage("place <x> <y> <z> [yaw]");
    }
    const auto x = ParseNumber(w[1]);
    const auto y = ParseNumber(w[2]);
    const auto z = ParseNumber(w[3]);
    const auto yaw = w.size() == 5 ? ParseNumber(w[4]) : std::optional<float>();
    if (!x || !y || !z || (w.size() == 5 && !yaw)) {
        return Usage("place <x> <y> <z> [yaw]");
    }
    debug::PlacePlayer(game, glm::vec3(*x, *y, *z), yaw);
    return Describe(game, debug::DescribePlayer);
}

Reply Turn(Game& game, const WordList& w) {
    const auto yaw = w.size() >= 2 ? ParseNumber(w[1]) : std::nullopt;
    const auto pitch = w.size() == 3 ? ParseNumber(w[2]) : std::nullopt;
    if ((w.size() != 2 && w.size() != 3) || !yaw || (w.size() == 3 && !pitch)) {
        return Usage("turn <yaw> [pitch]");
    }
    debug::TurnPlayer(game, *yaw, pitch);
    return Describe(game, debug::DescribePlayer);
}

Reply Hover(Game& game, const WordList& w) {
    if (w.size() != 2) {
        return Usage("hover <height>|here|off");
    }
    if (w[1] == "off") {
        debug::SetHover(game, std::nullopt);
    } else if (w[1] == "here") {
        debug::SetHover(game, game.GetPlayer().Feet().y);
    } else if (const auto height = ParseNumber(w[1])) {
        debug::SetHover(game, *height);
    } else {
        return Usage("hover <height>|here|off");
    }
    return Describe(game, debug::DescribePlayer);
}

Reply PlayerSwitch(Game& game, const WordList& w, void (*set)(Game&, bool)) {
    const auto on = w.size() == 2 ? ParseSwitch(w[1]) : std::nullopt;
    if (!on) {
        return Usage(std::string(w[0]) + " on|off");
    }
    set(game, *on);
    return Describe(game, debug::DescribePlayer);
}

Reply WalkSpeed(Game& game, const WordList& w) {
    const auto factor = w.size() == 2 ? ParseNumber(w[1]) : std::nullopt;
    if (!factor) {
        return Usage("walkspeed <factor>");
    }
    debug::SetWalkSpeed(game, *factor);
    return Describe(game, debug::DescribePlayer);
}

/* name value pairs after the command (and after skip more words): every pair is checked before any is applied */
bool Pairs(const WordList& w, size_t skip, std::vector<std::pair<std::string_view, std::string_view>>& out) {
    if (w.size() <= skip || (w.size() - skip) % 2 != 0) {
        return false;
    }
    for (size_t i = skip; i + 1 < w.size(); i += 2) {
        out.emplace_back(w[i], w[i + 1]);
    }
    return true;
}

Reply Light(Game& game, const WordList& w) {
    if (w.size() == 1) {
        return Describe(game, debug::DescribeHandyLight);
    }
    if (w.size() == 2 && w[1] == "reset") {
        debug::ResetHandyLight(game);
        return Describe(game, debug::DescribeHandyLight);
    }
    std::vector<std::pair<std::string_view, std::string_view>> pairs;
    if (!Pairs(w, 1, pairs)) {
        return Usage("light <name> <value> ...");
    }
    std::optional<bool> enable;
    std::vector<std::pair<std::string_view, float>> values;
    for (const auto& [name, text] : pairs) {
        if (name == "enable") {
            enable = ParseSwitch(text);
            if (!enable) {
                return Usage("light enable on|off");
            }
            continue;
        }
        const auto value = ParseNumber(text);
        if (!value) {
            return Error("light: '" + std::string(text) + "' is not a number");
        }
        values.emplace_back(name, *value);
    }
    /* checked against a copy first, so an unknown name changes nothing */
    const HandyLightParameters before = game.Parameters().handy_light;
    const auto default_before = game.Debug().handy_light_default;
    for (const auto& [name, value] : values) {
        if (!debug::SetHandyLightValue(game, name, value)) {
            game.Parameters().handy_light = before;
            game.Debug().handy_light_default = default_before;
            return Error("light: no value named '" + std::string(name) + "'; help lists them");
        }
    }
    if (enable) {
        debug::SetHandyLightOn(game, *enable);
    }
    return Describe(game, debug::DescribeHandyLight);
}

Reply Exposure(Game& game, const WordList& w) {
    if (w.size() == 1) {
        return Describe(game, debug::DescribeExposure);
    }
    if (w.size() == 2 && w[1] == "reset") {
        debug::ResetExposure(game);
        debug::PinExposure(game, std::nullopt);
        return Describe(game, debug::DescribeExposure);
    }
    std::vector<std::pair<std::string_view, std::string_view>> pairs;
    if (!Pairs(w, 1, pairs)) {
        return Usage("exposure <min|max|compensation|ev> <value>|off ...");
    }
    std::vector<std::pair<std::string_view, std::optional<float>>> values;
    for (const auto& [name, text] : pairs) {
        const auto value = ParseNumberOrOff(text);
        if (!value || (name != "min" && name != "max" && name != "compensation" && name != "ev")) {
            return Usage("exposure <min|max|compensation|ev> <value>|off ...");
        }
        values.emplace_back(name, *value);
    }
    for (const auto& [name, value] : values) {
        if (name == "ev") {
            debug::PinExposure(game, value);
        } else {
            debug::SetExposureOverride(game, name, value);
        }
    }
    return Describe(game, debug::DescribeExposure);
}

Reply CameraCommand(Game& game, const WordList& w) {
    if (w.size() == 1) {
        return Describe(game, debug::DescribeCamera);
    }
    if (w.size() == 2 && w[1] == "off") {
        debug::ReleaseCamera(game);
        return Describe(game, debug::DescribeCamera);
    }
    std::vector<std::pair<std::string_view, std::string_view>> pairs;
    if (!Pairs(w, 1, pairs)) {
        return Usage("camera <x|y|z|yaw|pitch|roll|fov> <value> ... [body on|off]");
    }
    Camera camera = game.ViewCamera();
    bool body = true;
    for (const auto& [name, text] : pairs) {
        if (name == "body") {
            const auto on = ParseSwitch(text);
            if (!on) {
                return Usage("camera ... body on|off");
            }
            body = *on;
            continue;
        }
        const auto value = ParseNumber(text);
        float* slot = name == "x"       ? &camera.position.x
                      : name == "y"     ? &camera.position.y
                      : name == "z"     ? &camera.position.z
                      : name == "yaw"   ? &camera.yaw
                      : name == "pitch" ? &camera.pitch
                      : name == "roll"  ? &camera.roll
                                        : nullptr;
        if (!value || (!slot && name != "fov")) {
            return Usage("camera <x|y|z|yaw|pitch|roll|fov> <value> ... [body on|off]");
        }
        if (slot) {
            *slot = *value;
        } else {
            camera.fov_y = glm::radians(*value);
        }
    }
    debug::HoldCamera(game, camera, body);
    return Describe(game, debug::DescribeCamera);
}

Reply DemoSpeed(Game& game, const WordList& w) {
    const auto factor = w.size() == 2 ? ParseNumber(w[1]) : std::nullopt;
    if (!factor) {
        return Usage("demospeed <factor>");
    }
    debug::SetDemoSpeed(game, *factor);
    return Describe(game, debug::DescribeStatus);
}

Reply Entity(Game& game, const WordList& w) {
    if (w.size() < 3) {
        return Usage("entity <stage> <name> [<enable|visible|geom> on|off ...]");
    }
    std::vector<std::pair<BodyField, bool>> flags;
    if (w.size() > 3) {
        std::vector<std::pair<std::string_view, std::string_view>> pairs;
        if (!Pairs(w, 3, pairs)) {
            return Usage("entity <stage> <name> [<enable|visible|geom> on|off ...]");
        }
        for (const auto& [name, text] : pairs) {
            const auto on = ParseSwitch(text);
            const std::optional<BodyField> field = name == "enable"    ? std::optional(BodyField::Enable)
                                                   : name == "visible" ? std::optional(BodyField::Visible)
                                                   : name == "geom"    ? std::optional(BodyField::GeomActive)
                                                                       : std::nullopt;
            if (!on || !field) {
                return Usage("entity <stage> <name> [<enable|visible|geom> on|off ...]");
            }
            flags.emplace_back(*field, *on);
        }
    }
    for (const auto& [field, on] : flags) {
        if (!debug::SetEntityFlag(game, w[1], w[2], field, on)) {
            break;
        }
    }
    JsonWriter out;
    if (!debug::DescribeEntity(game, w[1], w[2], out)) {
        return Error("no entity '" + std::string(w[2]) + "' in a stage '" + std::string(w[1]) + "' (stages lists the stages)");
    }
    return {true, out.Text()};
}

Reply Entities(Game& game, const WordList& w) {
    if (w.size() < 2 || w.size() > 4) {
        return Usage("entities <stage> [prefix] [limit]");
    }
    size_t limit = 500;
    if (w.size() == 4) {
        const auto value = ParseNumber(w[3]);
        if (!value || *value < 0.0f) {
            return Usage("entities <stage> [prefix] [limit]");
        }
        limit = static_cast<size_t>(*value);
    }
    JsonWriter out;
    const std::string_view prefix = w.size() >= 3 && w[2] != "*" ? w[2] : std::string_view();
    if (!debug::DescribeEntities(game, w[1], prefix, std::min<size_t>(limit, 20000), out)) {
        return Error("no stage '" + std::string(w[1]) + "' (stages lists them)");
    }
    return {true, out.Text()};
}

Reply FloorCommand(Game& game, const WordList& w) {
    if (w.size() != 2) {
        return Usage("floor <name>");
    }
    /* checked first: SetFloorLevel takes an unknown name as the first floor */
    if (game.Floor().IndexOf(w[1]) < 0) {
        return Error("no floor '" + std::string(w[1]) + "' in the floor table");
    }
    game.Floor().SetFloorLevel(w[1]);
    return Describe(game, debug::DescribeStatus);
}

/* an entry of the loop browser: its index (0 to 17) or its floor, f050 with the pass (1 or 2) */
Reply Loop(Game& game, const WordList& w) {
    if (w.size() != 2 && w.size() != 3) {
        return Usage("loop <index>|<floor> [pass]");
    }
    int index = -1;
    if (const auto number = ParseNumber(w[1]); number && w.size() == 2) {
        index = *number == std::floor(*number) ? static_cast<int>(*number) : -1;
    } else {
        const auto pass = w.size() == 3 ? ParseNumber(w[2]) : std::optional<float>(1.0f);
        index = pass ? BrowseIndexOf(w[1], static_cast<int>(*pass)) : -1;
    }
    if (index < 0 || index >= static_cast<int>(kBrowseLoops.size())) {
        return Usage("loop <index>|<floor> [pass]: an index 0 to 17, or f000 ... f160, ending, street");
    }
    if (!game.BrowseUnlocked(index)) {
        return Error("that loop is still locked in this build (finish the game first, or start pt with --no-release-locks)");
    }
    if (!game.BrowseLoop(index)) {
        return Error("the loop browser cannot be used right now (only in play, in the ending and on the street walk)");
    }
    return Describe(game, debug::DescribeStatus);
}
}

ControlServer::Reply Run(Game& game, std::string_view line) {
    const WordList w = Words(line);
    if (w.empty()) {
        return Error("empty line");
    }
    const std::string_view c = w[0];
    if (c == "help") return Help();
    if (c == "status" && w.size() == 1) return Describe(game, debug::DescribeStatus);
    if (c == "stages" && w.size() == 1) return Describe(game, debug::DescribeStages);
    if (c == "player" && w.size() == 1) return Describe(game, debug::DescribePlayer);
    if (c == "state" && w.size() == 1) return {true, ControlServer::JsonString(game.DescribeSessionState())};
    if (c == "place") return Place(game, w);
    if (c == "turn") return Turn(game, w);
    if (c == "hover") return Hover(game, w);
    if (c == "gravity") return PlayerSwitch(game, w, debug::SetGravity);
    if (c == "collision") return PlayerSwitch(game, w, debug::SetCollision);
    if (c == "input") return PlayerSwitch(game, w, debug::SetPlayerInput);
    if (c == "walkspeed") return WalkSpeed(game, w);
    if (c == "light") return Light(game, w);
    if (c == "exposure") return Exposure(game, w);
    if (c == "camera") return CameraCommand(game, w);
    if (c == "demospeed") return DemoSpeed(game, w);
    if (c == "entity") return Entity(game, w);
    if (c == "entities") return Entities(game, w);
    if (c == "floor") return FloorCommand(game, w);
    if (c == "loop") return Loop(game, w);
    return Error("unknown command '" + std::string(c) + "' (or wrong arguments); help lists the commands");
}

}
