#include "game/debug_control.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "engine/core/log.h"
#include "game/game.h"

namespace pt::game::debug {
namespace {

constexpr float kPi = 3.14159265358979f;

struct HandyField {
    const char* key;
    float HandyLightParameters::*member;
};
constexpr HandyField kHandyFields[] = {
    {"innerRange", &HandyLightParameters::inner_range}, {"outerRange", &HandyLightParameters::outer_range},
    {"temperature", &HandyLightParameters::temperature}, {"lumen", &HandyLightParameters::lumen},
    {"lightSize", &HandyLightParameters::light_size}, {"umbraAngle", &HandyLightParameters::umbra_angle},
    {"penumbraAngle", &HandyLightParameters::penumbra_angle}, {"attenuationExponent", &HandyLightParameters::attenuation_exponent},
    {"dimmer", &HandyLightParameters::dimmer}, {"powerScale", &HandyLightParameters::power_scale},
};

struct FoundEntity {
    Stage* stage = nullptr;
    const StageData* file = nullptr;
    const fox2::Entity* entity = nullptr;
};

FoundEntity FindEntity(Game& game, std::string_view stage_label, std::string_view name) {
    FoundEntity found;
    found.stage = game.Stages().Find(std::string(stage_label));
    if (!found.stage) {
        return found;
    }
    for (const auto& file : found.stage->files) {
        if (const fox2::Entity* e = file->file->ByShortName(name)) {
            found.file = file.get();
            found.entity = e;
            break;
        }
    }
    return found;
}

void WriteEntity(const FoundEntity& found, FieldWriter& out) {
    const BodyState& body = found.stage->Body(found.entity);
    out.BeginObject(nullptr);
    out.String("stage", found.stage->label);
    out.String("name", found.file->file->EntityName(*found.entity));
    out.String("class", found.entity->class_name);
    out.Bool("enable", body.enable);
    out.Bool("visible", body.visible);
    out.Bool("geom", body.geom_active);
    out.Vector("position", glm::vec3(found.stage->ToWorld(found.file->file->WorldTransform(*found.entity))[3]));
    out.End();
}

}

void DescribeStatus(Game& game, FieldWriter& out) {
    out.BeginObject(nullptr);
    out.String("floor", game.Floor().CurrentFloorName());
    out.Number("floorIndex", game.Floor().Index());
    out.Number("loop", game.Floor().LoopCount());
    out.Number("step", game.Controller().Step());
    out.Number("requestedStep", game.Controller().RequestedStep());
    out.Number("frame", static_cast<double>(game.Frame()));
    out.Number("time", game.Time());
    out.Bool("paused", game.Paused());
    out.Bool("streetWalk", game.StreetWalkActive());
    out.Bool("gamePlus", game.GamePlus());
    out.Number("demoSpeed", game.Demos().time_scale);
    out.BeginArray("demos");
    for (const PlayingDemo& demo : game.Demos().Playing()) {
        out.String(nullptr, demo.demo_id);
    }
    out.End();
    out.End();
}

void DescribeStages(Game& game, FieldWriter& out) {
    out.BeginArray(nullptr);
    game.Stages().ForEachStage([&](Stage& stage) {
        out.BeginObject(nullptr);
        out.String("label", stage.label);
        out.Number("id", stage.id);
        out.String("package", stage.package_path);
        out.Bool("active", stage.active);
        out.Bool("resident", stage.resident);
        out.Vector("origin", glm::vec3(stage.file_to_world[3]));
        out.End();
    });
    out.End();
}

void DescribePlayer(Game& game, FieldWriter& out) {
    const Player& p = game.GetPlayer();
    const glm::vec3 feet = p.Feet();
    out.BeginObject(nullptr);
    out.Number("x", feet.x);
    out.Number("y", feet.y);
    out.Number("z", feet.z);
    out.Number("yaw", p.yaw);
    out.Number("pitch", p.pitch);
    out.Number("foxYaw", p.BodyFoxYaw());
    out.Vector("eye", p.Eye());
    out.Bool("spawned", p.spawned);
    out.Bool("grounded", p.controller.grounded);
    out.Number("verticalVelocity", p.controller.vertical_velocity);
    out.Bool("handyLight", p.handy_light.enable);
    if (p.hold_height) {
        out.Number("hover", *p.hold_height);
    }
    out.Bool("gravity", p.gravity);
    out.Bool("collision", p.collision);
    out.Bool("input", p.takes_input);
    out.Number("walkSpeed", p.walk_speed);
    out.End();
}

void DescribeHandyLight(Game& game, FieldWriter& out) {
    const HandyLightParameters& h = game.Parameters().handy_light;
    out.BeginObject(nullptr);
    for (const HandyField& field : kHandyFields) {
        out.Number(field.key, h.*field.member);
    }
    out.BeginObject("color");
    out.Number("r", h.color[0]);
    out.Number("g", h.color[1]);
    out.Number("b", h.color[2]);
    out.End();
    const glm::vec3& tint = game.Effects().handy_light_color;
    out.BeginObject("floorTint");
    out.Number("r", tint.r);
    out.Number("g", tint.g);
    out.Number("b", tint.b);
    out.End();
    out.Bool("enable", game.GetPlayer().handy_light.enable);
    out.Bool("changed", game.Debug().handy_light_default.has_value());
    out.End();
}

void DescribeExposure(Game& game, FieldWriter& out) {
    const Game::DebugState& debug = game.Debug();
    out.BeginObject(nullptr);
    out.Number("row", game.FloorLightingRow());
    if (const LightingRow* row = game.Parameters().Lighting(game.FloorLightingRow())) {
        out.Number("min", debug.min_ev.value_or(row->min_exposure));
        out.Number("max", debug.max_ev.value_or(row->max_exposure));
        out.Number("compensation", debug.ev_compensation.value_or(row->exposure_compensation));
        out.BeginObject("floor");
        out.Number("min", row->min_exposure);
        out.Number("max", row->max_exposure);
        out.Number("compensation", row->exposure_compensation);
        out.End();
    }
    out.Bool("overridden", debug.min_ev || debug.max_ev || debug.ev_compensation);
    if (game.Effects().ev_pinned) {
        out.Number("ev", game.Effects().pinned_ev);
    }
    out.End();
}

void DescribeCamera(Game& game, FieldWriter& out) {
    const Camera camera = game.ViewCamera();
    out.BeginObject(nullptr);
    out.Number("x", camera.position.x);
    out.Number("y", camera.position.y);
    out.Number("z", camera.position.z);
    out.Number("yaw", camera.yaw);
    out.Number("pitch", camera.pitch);
    out.Number("roll", camera.roll);
    out.Number("fov", glm::degrees(camera.fov_y));
    out.Vector("forward", camera.Forward());
    out.Bool("held", game.Debug().camera);
    out.End();
}

bool DescribeEntity(Game& game, std::string_view stage_label, std::string_view name, FieldWriter& out) {
    const FoundEntity found = FindEntity(game, stage_label, name);
    if (!found.entity) {
        return false;
    }
    WriteEntity(found, out);
    return true;
}

bool DescribeEntities(Game& game, std::string_view stage_label, std::string_view prefix, size_t limit, FieldWriter& out) {
    Stage* stage = game.Stages().Find(std::string(stage_label));
    if (!stage) {
        return false;
    }
    out.BeginArray(nullptr);
    size_t count = 0;
    for (const auto& file : stage->files) {
        for (const fox2::Entity& e : file->file->Entities()) {
            if (count >= limit) {
                break;
            }
            const std::string full = file->file->EntityName(e);
            const size_t bar = full.rfind('|');
            const std::string_view short_name = bar == std::string::npos ? std::string_view(full) : std::string_view(full).substr(bar + 1);
            if (short_name.empty() || !short_name.starts_with(prefix)) {
                continue;
            }
            WriteEntity({stage, file.get(), &e}, out);
            ++count;
        }
    }
    out.End();
    return true;
}

void PlacePlayer(Game& game, const glm::vec3& feet, std::optional<float> yaw) {
    Player& p = game.GetPlayer();
    const float view_yaw = yaw.value_or(p.yaw);
    const float pitch = p.pitch;
    p.Warp(feet, view_yaw - kPi);
    p.FollowCamera(view_yaw, pitch);
    if (p.hold_height) {
        p.hold_height = feet.y;
    }
    LogDebug("debug: player placed at ({:.3f} {:.3f} {:.3f})", feet.x, feet.y, feet.z);
}

void TurnPlayer(Game& game, float yaw, std::optional<float> pitch) {
    Player& p = game.GetPlayer();
    p.FollowCamera(yaw, pitch.value_or(p.pitch));
}

void SetHover(Game& game, std::optional<float> height) {
    Player& p = game.GetPlayer();
    p.hold_height = height;
    p.controller.vertical_velocity = 0.0f;
}

void SetGravity(Game& game, bool on) {
    Player& p = game.GetPlayer();
    p.gravity = on;
    p.controller.vertical_velocity = 0.0f;
}

void SetCollision(Game& game, bool on) {
    Player& p = game.GetPlayer();
    p.collision = on;
    p.controller.vertical_velocity = 0.0f;
}

void SetPlayerInput(Game& game, bool on) {
    game.GetPlayer().takes_input = on;
}

float SetWalkSpeed(Game& game, float factor) {
    Player& p = game.GetPlayer();
    factor = std::clamp(factor, 0.05f, 20.0f);
    const PlayerParameters& base = game.Parameters().player;
    const std::pair<SpeedRange*, const SpeedRange*> ranges[] = {
        {&p.params.front, &base.front}, {&p.params.left, &base.left}, {&p.params.right, &base.right}, {&p.params.back, &base.back}};
    for (const auto& [to, from] : ranges) {
        to->min = from->min * factor;
        to->max = from->max * factor;
    }
    p.walk_speed = factor;
    return factor;
}

bool SetHandyLightValue(Game& game, std::string_view key, float value) {
    HandyLightParameters& h = game.Parameters().handy_light;
    float* slot = nullptr;
    for (const HandyField& field : kHandyFields) {
        if (key == field.key) {
            slot = &(h.*field.member);
        }
    }
    if (key == "r" || key == "g" || key == "b") {
        slot = &h.color[key == "r" ? 0 : key == "g" ? 1 : 2];
    }
    if (!slot || !std::isfinite(value)) {
        return false;
    }
    if (!game.Debug().handy_light_default) {
        game.Debug().handy_light_default = h;
    }
    *slot = value;
    return true;
}

void SetHandyLightOn(Game& game, bool on) {
    game.GetPlayer().handy_light.enable = on;
}

void ResetHandyLight(Game& game) {
    if (game.Debug().handy_light_default) {
        game.Parameters().handy_light = *game.Debug().handy_light_default;
        game.Debug().handy_light_default.reset();
    }
}

bool SetExposureOverride(Game& game, std::string_view key, std::optional<float> value) {
    Game::DebugState& debug = game.Debug();
    std::optional<float>* slot = key == "min" ? &debug.min_ev : key == "max" ? &debug.max_ev : key == "compensation" ? &debug.ev_compensation : nullptr;
    if (!slot || (value && !std::isfinite(*value))) {
        return false;
    }
    *slot = value;
    return true;
}

void PinExposure(Game& game, std::optional<float> ev) {
    if (ev && std::isfinite(*ev)) {
        game.Effects().ev_pinned = true;
        game.Effects().pinned_ev = *ev;
    } else {
        game.Effects().ev_pinned = false;
    }
}

void ResetExposure(Game& game) {
    Game::DebugState& debug = game.Debug();
    debug.min_ev.reset();
    debug.max_ev.reset();
    debug.ev_compensation.reset();
}

void HoldCamera(Game& game, Camera camera, bool body) {
    camera.pitch = std::clamp(camera.pitch, -1.55f, 1.55f);
    camera.fov_y = std::clamp(camera.fov_y, glm::radians(5.0f), glm::radians(150.0f));
    game.SetCameraOverride(camera);
    game.SetShowBody(body);
    game.SetDetachedView(true);
    game.Debug().camera = true;
}

void ReleaseCamera(Game& game) {
    if (!game.Debug().camera) {
        return;
    }
    game.SetCameraOverride(std::nullopt);
    game.SetShowBody(false);
    game.SetDetachedView(false);
    game.Debug().camera = false;
}

float SetDemoSpeed(Game& game, float factor) {
    DemoSystem& demos = game.Demos();
    const float before = demos.time_scale;
    if (std::isfinite(factor)) {
        demos.time_scale = std::clamp(factor, 0.0f, 16.0f);
    }
    return before;
}

bool SetEntityFlag(Game& game, std::string_view stage_label, std::string_view name, BodyField field, bool on) {
    const FoundEntity found = FindEntity(game, stage_label, name);
    if (!found.entity) {
        return false;
    }
    BodyState& body = found.stage->Body(found.entity);
    bool& slot = field == BodyField::Enable ? body.enable : field == BodyField::Visible ? body.visible : body.geom_active;
    if (slot != on) {
        slot = on;
        game.Scripts().OnBodyChanged(*found.stage, *found.entity, field);
    }
    return true;
}

}
