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
//   caffeine_math_clamp/clamp01/lerp/inverse_lerp/remap/smoothstep/approach/sign/radians/degrees
//   caffeine_math_min/max/abs/floor/ceil/round/sqrt/sin/cos/pow
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

#include "script/DryadNatives.hpp"
#include "dryad/compiler/interpreter.h"
#include "dryad/runtime/value.h"

#include <algorithm>
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
    defineNative(interp, "caffeine_time_delta", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().deltaTime())); });
    defineNative(interp, "caffeine_time_unscaled_delta", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().unscaledDeltaTime())); });
    defineNative(interp, "caffeine_time_total", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().time())); });
    defineNative(interp, "caffeine_time_fixed_delta", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().fixedDeltaTime())); });
    defineNative(interp, "caffeine_time_scale", [](const Args&) {
        return dryad::Value(static_cast<double>(Runtime::PlayClock::instance().timeScale())); });
    defineNative(interp, "caffeine_time_frame", [](const Args&) {
        return dryad::Value(static_cast<int64_t>(Runtime::PlayClock::instance().frame())); });
    defineNative(interp, "caffeine_time_set_scale", [](const Args& a) {
        if (!a.empty()) Runtime::PlayClock::instance().setTimeScale(static_cast<f32>(numberOf(a[0])));
        return dryad::Value(); });
    defineNative(interp, "caffeine_time_paused", [](const Args&) {
        return dryad::Value(Runtime::PlayClock::instance().paused()); });
    defineNative(interp, "caffeine_time_set_paused", [](const Args& a) {
        if (!a.empty()) Runtime::PlayClock::instance().setPaused(numberOf(a[0]) != 0.0);
        return dryad::Value(); });

    auto input = [inputPtr]() -> Input::InputManager* { return inputPtr ? *inputPtr : nullptr; };

    defineNative(interp, "caffeine_input_key_down", [input](const Args& a) {
        Input::Key key;
        Input::InputManager* im = input();
        return dryad::Value(im && lookupScriptKey(textOf(a, 0), key) && im->isKeyDown(key)); });
    defineNative(interp, "caffeine_input_axis", [input](const Args& a) {
        Input::Axis axis;
        Input::InputManager* im = input();
        if (!im || !lookupScriptAxis(textOf(a, 0), axis)) return dryad::Value(0.0);
        return dryad::Value(static_cast<double>(im->axisState(axis).value)); });
    defineNative(interp, "caffeine_input_action_down", [input](const Args& a) {
        Input::Action action;
        Input::InputManager* im = input();
        return dryad::Value(im && lookupScriptAction(textOf(a, 0), action) && im->actionState(action).pressed); });
    defineNative(interp, "caffeine_input_action_pressed", [input](const Args& a) {
        Input::Action action;
        Input::InputManager* im = input();
        return dryad::Value(im && lookupScriptAction(textOf(a, 0), action) && im->actionState(action).justPressed); });
    defineNative(interp, "caffeine_input_mouse_x", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mousePosition().x) : 0.0); });
    defineNative(interp, "caffeine_input_mouse_y", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mousePosition().y) : 0.0); });
    defineNative(interp, "caffeine_input_mouse_dx", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mouseDelta().x) : 0.0); });
    defineNative(interp, "caffeine_input_mouse_dy", [input](const Args&) {
        Input::InputManager* im = input();
        return dryad::Value(im ? static_cast<double>(im->mouseDelta().y) : 0.0); });
    defineNative(interp, "caffeine_input_mouse_down", [input](const Args& a) {
        Input::MouseButton button;
        Input::InputManager* im = input();
        const auto index = a.empty() ? 0u : static_cast<unsigned>(numberOf(a[0]));
        return dryad::Value(im && lookupScriptMouseButton(index, button) && im->isMouseButtonDown(button)); });

    // Scalar math, same results as Lua's caffeine.math plus the helpers Dryad lacks as operators.
    const auto num = [](const Args& a, size_t i) { return i < a.size() ? numberOf(a[i]) : 0.0; };
    defineNative(interp, "caffeine_math_clamp", [num](const Args& a) {
        const double lo = num(a, 1), hi = num(a, 2), v = num(a, 0);
        return dryad::Value(lo > hi ? std::clamp(v, hi, lo) : std::clamp(v, lo, hi)); });
    defineNative(interp, "caffeine_math_clamp01", [num](const Args& a) {
        return dryad::Value(std::clamp(num(a, 0), 0.0, 1.0)); });
    defineNative(interp, "caffeine_math_lerp", [num](const Args& a) {
        return dryad::Value(num(a, 0) + (num(a, 1) - num(a, 0)) * num(a, 2)); });
    defineNative(interp, "caffeine_math_inverse_lerp", [num](const Args& a) {
        const double lo = num(a, 0), hi = num(a, 1);
        return dryad::Value(std::abs(hi - lo) < 1.0e-9 ? 0.0 : (num(a, 2) - lo) / (hi - lo)); });
    defineNative(interp, "caffeine_math_remap", [num](const Args& a) {
        const double inLo = num(a, 1), inHi = num(a, 2);
        const double t = std::abs(inHi - inLo) < 1.0e-9 ? 0.0 : (num(a, 0) - inLo) / (inHi - inLo);
        return dryad::Value(num(a, 3) + (num(a, 4) - num(a, 3)) * t); });
    defineNative(interp, "caffeine_math_smoothstep", [num](const Args& a) {
        const double e0 = num(a, 0), e1 = num(a, 1), v = num(a, 2);
        if (std::abs(e1 - e0) < 1.0e-9) return dryad::Value(v < e0 ? 0.0 : 1.0);
        const double t = std::clamp((v - e0) / (e1 - e0), 0.0, 1.0);
        return dryad::Value(t * t * (3.0 - 2.0 * t)); });
    defineNative(interp, "caffeine_math_approach", [num](const Args& a) {
        const double cur = num(a, 0), target = num(a, 1), step = std::abs(num(a, 2));
        return dryad::Value(cur < target ? std::min(cur + step, target) : std::max(cur - step, target)); });
    defineNative(interp, "caffeine_math_sign", [num](const Args& a) {
        const double v = num(a, 0);
        return dryad::Value(v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0)); });
    defineNative(interp, "caffeine_math_radians", [num](const Args& a) {
        return dryad::Value(num(a, 0) * 3.14159265358979 / 180.0); });
    defineNative(interp, "caffeine_math_degrees", [num](const Args& a) {
        return dryad::Value(num(a, 0) * 180.0 / 3.14159265358979); });
    defineNative(interp, "caffeine_math_min", [num](const Args& a) {
        return dryad::Value(std::min(num(a, 0), num(a, 1))); });
    defineNative(interp, "caffeine_math_max", [num](const Args& a) {
        return dryad::Value(std::max(num(a, 0), num(a, 1))); });
    defineNative(interp, "caffeine_math_abs", [num](const Args& a) {
        return dryad::Value(std::abs(num(a, 0))); });
    defineNative(interp, "caffeine_math_floor", [num](const Args& a) {
        return dryad::Value(std::floor(num(a, 0))); });
    defineNative(interp, "caffeine_math_ceil", [num](const Args& a) {
        return dryad::Value(std::ceil(num(a, 0))); });
    defineNative(interp, "caffeine_math_round", [num](const Args& a) {
        return dryad::Value(std::round(num(a, 0))); });
    defineNative(interp, "caffeine_math_sqrt", [num](const Args& a) {
        return dryad::Value(std::sqrt(std::max(num(a, 0), 0.0))); });
    defineNative(interp, "caffeine_math_sin", [num](const Args& a) {
        return dryad::Value(std::sin(num(a, 0))); });
    defineNative(interp, "caffeine_math_cos", [num](const Args& a) {
        return dryad::Value(std::cos(num(a, 0))); });
    defineNative(interp, "caffeine_math_pow", [num](const Args& a) {
        const double r = std::pow(num(a, 0), num(a, 1));
        return dryad::Value(std::isfinite(r) ? r : 0.0); });

    defineNative(interp, "caffeine_world_create", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return dryad::Value(static_cast<int64_t>(0));
        const std::string name = textOf(a, 0);
        ECS::Entity e = world->create(name.empty() ? "DryadEntity" : name.c_str());
        return dryad::Value(static_cast<int64_t>(e.id())); });
    defineNative(interp, "caffeine_world_destroy", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (world && !a.empty()) {
            ECS::Entity e(static_cast<u32>(numberOf(a[0])), world);
            if (e.isValid() && world->isEntityAlive(e)) world->destroy(e);
        }
        return dryad::Value(); });
    defineNative(interp, "caffeine_world_is_alive", [worldPtr](const Args& a) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world || a.empty()) return dryad::Value(false);
        ECS::Entity e(static_cast<u32>(numberOf(a[0])), world);
        return dryad::Value(e.isValid() && world->isEntityAlive(e)); });
    defineNative(interp, "caffeine_world_distance", [worldPtr](const Args& a) {
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
