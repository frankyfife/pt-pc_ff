#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "engine/render/camera.h"
#include "game/lua_entity.h"

namespace pt::game {

class Game;

/* What the port's debug functions read and change (docs/lua_api.md, PtDebug; docs/control.md): the player, the camera, the
   flashlight, the exposure, the demos and the bodies of stage entities. PtDebug in the game's Lua state and the control
   channel's commands both go through here, so they behave the same. */
namespace debug {

/* Where a description goes: a Lua table (PtDebug) or JSON (the control channel). A null key adds to the array that is
   open; every Begin has its End. */
class FieldWriter {
public:
    virtual ~FieldWriter() = default;
    virtual void Number(const char* key, double value) = 0;
    virtual void Bool(const char* key, bool value) = 0;
    virtual void String(const char* key, std::string_view value) = 0;
    virtual void BeginObject(const char* key) = 0;
    virtual void BeginArray(const char* key) = 0;
    virtual void End() = 0;
    void Vector(const char* key, const glm::vec3& v) {
        BeginObject(key);
        Number("x", v.x);
        Number("y", v.y);
        Number("z", v.z);
        End();
    }
};

/* Each writes one complete value, an object (stages and entities: an array). */
void DescribeStatus(Game& game, FieldWriter& out);
void DescribeStages(Game& game, FieldWriter& out);
void DescribePlayer(Game& game, FieldWriter& out);
void DescribeHandyLight(Game& game, FieldWriter& out);
void DescribeExposure(Game& game, FieldWriter& out);
void DescribeCamera(Game& game, FieldWriter& out);
/* false when the stage or the entity is not there (nothing is written then) */
bool DescribeEntity(Game& game, std::string_view stage_label, std::string_view name, FieldWriter& out);
/* the entities of a stage whose short name starts with prefix, at most limit of them; false without the stage */
bool DescribeEntities(Game& game, std::string_view stage_label, std::string_view prefix, size_t limit, FieldWriter& out);

void PlacePlayer(Game& game, const glm::vec3& feet, std::optional<float> yaw);
void TurnPlayer(Game& game, float yaw, std::optional<float> pitch);
void SetHover(Game& game, std::optional<float> height);
void SetGravity(Game& game, bool on);
void SetCollision(Game& game, bool on);
void SetPlayerInput(Game& game, bool on);
float SetWalkSpeed(Game& game, float factor);

/* The flashlight values under the names ShParameterTables.lua uses; "r", "g" and "b" are the colour. false for a name
   it does not know. The first change keeps the values from before for ResetHandyLight. */
bool SetHandyLightValue(Game& game, std::string_view key, float value);
void SetHandyLightOn(Game& game, bool on);
void ResetHandyLight(Game& game);

/* "min", "max", "compensation" (EV): a value overrides the floor's own for every floor, none gives it back. */
bool SetExposureOverride(Game& game, std::string_view key, std::optional<float> value);
void PinExposure(Game& game, std::optional<float> ev);
void ResetExposure(Game& game);

/* Holds the view at camera (pitch kept within ±1.55, the field of view within 5 to 150 degrees), the body shown or not;
   ReleaseCamera gives the view back to the player when the debug functions hold it. */
void HoldCamera(Game& game, Camera camera, bool body);
void ReleaseCamera(Game& game);

/* 0 to 16; returns the speed before */
float SetDemoSpeed(Game& game, float factor);

/* Sets the enable, visible or geom flag of a stage entity's body the way a stage script does; false when the stage or
   the entity is not there. */
bool SetEntityFlag(Game& game, std::string_view stage_label, std::string_view name, BodyField field, bool on);

}
}
