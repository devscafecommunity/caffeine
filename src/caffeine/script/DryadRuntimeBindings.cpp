// Dryad natives for time, input and world queries. They mirror Lua's `caffeine.time`,
// `caffeine.input` and `caffeine.world`, so a project can pick either language.
//
//   caffeine_time_delta() / _unscaled_delta() / _total() / _fixed_delta() / _scale() / _frame()
//   caffeine_time_set_scale(s) / caffeine_time_paused() / caffeine_time_set_paused(b)
//   caffeine_input_key_down(name)           "Space", "W", "LShift" ...
//   caffeine_input_axis(name)               "Horizontal", "Vertical", "LookX", "LookY"
//   caffeine_input_action_down(name) / caffeine_input_action_pressed(name)
//   caffeine_input_mouse_x() / _mouse_y() / _mouse_dx() / _mouse_dy()
//   caffeine_input_mouse_down(button)       1 left, 2 middle, 3 right
//   caffeine_world_create([name]) -> entity id (0 when there is no world)
//   caffeine_world_destroy(entity) / caffeine_world_is_alive(entity)
//   caffeine_world_distance(a, b)           metres; 1e9 when either has no position

#include "debug/LogSystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/World.hpp"
#include "input/InputManager.hpp"
#include "runtime/PlayClock.hpp"
#include "script/ScriptInputNames.hpp"

#include "dryad/compiler/interpreter.h"
#include "dryad/runtime/value.h"

#include <cmath>

namespace Caffeine::Script {
namespace {

using Args = std::vector<dryad::Value>;

double numberOf(const dryad::Value& v) {
    if (v.is_integer()) return static_cast<double>(v.as_integer());
    if (v.is_float()) return v.as_float();
    if (v.is_boolean()) return v.as_boolean() ? 1.0 : 0.0;
    return 0.0;
}

std::string textOf(const Args& args, size_t i) {
    return i < args.size() && args[i].is_string() ? args[i].as_string() : std::string();
}

bool positionOf(ECS::World* world, u32 id, Vec3& out) {
    if (!world) return false;
    ECS::Entity e(id, world);
    if (!e.isValid() || !world->isEntityAlive(e)) return false;
    if (auto* p3 = e.get<ECS::Position3D>()) { out = p3->position; return true; }
    if (auto* t = e.get<ECS::Transform>()) { out = t->position; return true; }
    return false;
}

}  // namespace

void bindRuntimeNatives(dryad::Interpreter& interp, ECS::World** worldPtr,
                        Input::InputManager** inputPtr) {
    interp.define_native("caffeine_time_delta", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().deltaTime())); });
    interp.define_native("caffeine_time_unscaled_delta", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().unscaledDeltaTime())); });
    interp.define_native("caffeine_time_total", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().time())); });
    interp.define_native("caffeine_time_fixed_delta", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().fixedDeltaTime())); });
    interp.define_native("caffeine_time_scale", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().timeScale())); });
    interp.define_native("caffeine_time_frame", [](const Args&) {
        return dryad::Value(static_cast<int64_t>(Runtime::PlayClock::instance().frame())); });
    interp.define_native("caffeine_time_set_scale", [](const Args& a) {
        if (!a.empty()) Runtime::PlayClock::instance().setTimeScale(static_cast<f32>(numberOf(a[0])));
        return dryad::Value(); });
    interp.define_native("caffeine_time_paused", [](const Args&) {
        return dryad::Value(Runtime::PlayClock::instance().paused()); });
    interp.define_native("caffeine_time_set_paused", [](const Args& a) {
        if (!a.empty()) Runtime::PlayClock::instance().setPaused(numberOf(a[0]) != 0.0);
        return dryad::Value(); });

    auto input = [inputPtr]() -> Input::InputManager* { return inputPtr ? *inputPtr : nullptr; };

    interp.define_native("caffeine_input_key_down", [input](const Args& a) {
        Input::Key key;
        Input::InputManager* im = input();
        return dryad::Value(im && lookupScriptKey(textOf(a, 0), key) && im->isKeyDown(key)); });
    interp.define_native("caffeine_input_axis", [input](const Args& a) {
        Input::Axis axis;
        Input::InputManager* im = input();
        if (!im || !lookupScriptAxis(textOf(a, 0), axis)) return dryad::Value(0.0);
        return dryad::Value(static_cast<double>(im->axisState(axis).value)); });
    interp.define_native("caffeine_input_action_down", [input](const Args& a) {
        Input::Action action;
        Input::InputManager* im = input();
        return dryad::Value(im && lookupScriptAction(textOf(a, 0), action) && im->actionState(action).pressed); });
    interp.define_native("caffeine_input_action_pressed", [input](const Args& a) {
        Input::Action action;
        Input::InputManager* im = input();
        return dryad::Value(im && lookupScriptAction(textOf(a, 0), action) && im->actionState(action).justPressed); });
    interp.define_native("caffeine_input_mouse_x", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mousePosition().x) : 0.0); });
    interp.define_native("caffeine_input_mouse_y", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mousePosition().y) : 0.0); });
    interp.define_native("caffeine_input_mouse_dx", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mouseDelta().x) : 0.0); });
    interp.define_native("caffeine_input_mouse_dy", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mouseDelta().y) : 0.0); });
    interp.define_native("caffeine_input_mouse_down", [input](const Args& a) {
        Input::MouseButton button;
        Input::InputManager* im = input();
        const auto index = a.empty() ? 0u : static_cast<unsigned>(numberOf(a[0]));
        return dryad::Value(im && lookupScriptMouseButton(index, button) && im->isMouseButtonDown(button)); });

    interp.define_native("caffeine_world_create", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return dryad::Value(static_cast<int64_t>(0));
        const std::string name = textOf(a, 0);
        ECS::Entity e = world->create(name.empty() ? "DryadEntity" : name.c_str());
        return dryad::Value(static_cast<int64_t>(e.id())); });
    interp.define_native("caffeine_world_destroy", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (world && !a.empty()) {
            ECS::Entity e(static_cast<u32>(numberOf(a[0])), world);
            if (e.isValid() && world->isEntityAlive(e)) world->destroy(e);
        }
        return dryad::Value(); });
    interp.define_native("caffeine_world_is_alive", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world || a.empty()) return dryad::Value(false);
        ECS::Entity e(static_cast<u32>(numberOf(a[0])), world);
        return dryad::Value(e.isValid() && world->isEntityAlive(e)); });
    interp.define_native("caffeine_world_distance", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        Vec3 p, q;
        if (a.size() < 2 || !positionOf(world, static_cast<u32>(numberOf(a[0])), p) ||
            !positionOf(world, static_cast<u32>(numberOf(a[1])), q)) {
            return dryad::Value(1.0e9);
        }
        const double dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
        return dryad::Value(std::sqrt(dx * dx + dy * dy + dz * dz)); });
}

}  // namespace Caffeine::Script
