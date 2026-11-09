#include "script/AssetTextLoader.hpp"
#include "script/NativeModuleHost.hpp"
#include "ecs/World.hpp"
#include "ecs/MapComponents.hpp"
#include "input/InputManager.hpp"
#include "render/Overlay2D.hpp"
#include "runtime/GameStateStack.hpp"
#include "runtime/PlayClock.hpp"
#include "scene/PickQuery.hpp"
#include "math/Vec3.hpp"
#include "scene/PlayMode2D.hpp"
#include "ui/UISystem.hpp"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Caffeine::Script {

namespace {

Render::OverlayColor colorFromTable(sol::table t) {
    Render::OverlayColor c;
    c.r = static_cast<u8>(std::clamp(t["r"].get_or(1.0f), 0.0f, 1.0f) * 255.0f);
    c.g = static_cast<u8>(std::clamp(t["g"].get_or(1.0f), 0.0f, 1.0f) * 255.0f);
    c.b = static_cast<u8>(std::clamp(t["b"].get_or(1.0f), 0.0f, 1.0f) * 255.0f);
    c.a = static_cast<u8>(std::clamp(t["a"].get_or(1.0f), 0.0f, 1.0f) * 255.0f);
    return c;
}

Vec2 vec2FromTable(sol::table t) {
    return {t["x"].get_or(0.0f), t["y"].get_or(0.0f)};
}

Render::OverlaySpace spaceFromString(const std::string& s) {
    if (s == "screen") return Render::OverlaySpace::Screen;
    return Render::OverlaySpace::World;
}

std::string moduleNameToPath(const std::string& name) {
    std::string rel = name;
    for (char& ch : rel) {
        if (ch == '.') ch = '/';
    }
    return "scripts/" + rel + ".lua";
}

}  // namespace

void registerRuntimeScriptBindings(sol::state& lua, ECS::World** worldPtr, Input::InputManager** inputPtr,
                                   UI::UISystem** uiPtr, AssetTextLoader* assetLoader,
                                   const std::string* searchRoot,
                                   const std::function<bool(const std::string& path, std::string* err)>&
                                       loadModuleFn) {
    lua["caffeine"]["time"] = lua.create_table();
    sol::table tt = lua["caffeine"]["time"];
    tt["delta"] = []() { return Runtime::PlayClock::instance().deltaTime(); };
    tt["unscaledDelta"] = []() { return Runtime::PlayClock::instance().unscaledDeltaTime(); };
    tt["time"] = []() { return Runtime::PlayClock::instance().time(); };
    tt["fixedDelta"] = []() { return Runtime::PlayClock::instance().fixedDeltaTime(); };
    tt["timeScale"] = []() { return Runtime::PlayClock::instance().timeScale(); };
    tt["setTimeScale"] = [](f32 s) { Runtime::PlayClock::instance().setTimeScale(s); };
    tt["paused"] = []() { return Runtime::PlayClock::instance().paused(); };
    tt["setPaused"] = [](bool p) { Runtime::PlayClock::instance().setPaused(p); };
    tt["frame"] = []() { return static_cast<double>(Runtime::PlayClock::instance().frame()); };
    tt["seed"] = []() { return Runtime::PlayClock::instance().randomSeed(); };
    tt["setSeed"] = [](u32 seed) { Runtime::PlayClock::instance().setRandomSeed(seed); };

    lua["caffeine"]["gameState"] = lua.create_table();
    sol::table gst = lua["caffeine"]["gameState"];
    gst["push"] = [](const std::string& id) { Runtime::GameStateStack::instance().push(id); };
    gst["pop"] = []() { return Runtime::GameStateStack::instance().pop(); };
    gst["top"] = []() { return Runtime::GameStateStack::instance().top(); };
    gst["pause"] = []() { return Runtime::GameStateStack::instance().pauseTop(); };
    gst["resume"] = []() { return Runtime::GameStateStack::instance().resumeTop(); };

    lua["caffeine"]["pick"] = lua.create_table();
    sol::table pt = lua["caffeine"]["pick"];
    pt["world"] = [worldPtr](f32 wx, f32 wy) -> u32 {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return 0;
        ECS::Entity e = Scene::pickEntityAtWorld(*world, {wx, wy});
        return e.isValid() ? e.id() : 0;
    };
    pt["screen"] = [worldPtr](f32 sx, f32 sy) -> u32 {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return 0;
        const Vec2 w = Scene::PlayRenderContext::instance().screenToWorld({sx, sy});
        ECS::Entity e = Scene::pickEntityAtWorld(*world, w);
        return e.isValid() ? e.id() : 0;
    };
    pt["atMouse"] = [worldPtr, inputPtr]() -> u32 {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        Input::InputManager* input = inputPtr ? *inputPtr : nullptr;
        if (!world || !input) return 0;
        const auto pos = input->mousePosition();
        const Vec2 w = Scene::PlayRenderContext::instance().screenToWorld(pos);
        ECS::Entity e = Scene::pickEntityAtWorld(*world, w);
        return e.isValid() ? e.id() : 0;
    };
    pt["screenToWorld"] = [&lua](f32 sx, f32 sy) -> sol::table {
        sol::table t = lua.create_table();
        const Vec2 w = Scene::PlayRenderContext::instance().screenToWorld({sx, sy});
        t["x"] = w.x;
        t["y"] = w.y;
        return t;
    };
    pt["register"] = [](sol::protected_function fn) -> u32 {
        return Scene::PickQueryRegistry::instance().registerScriptPicker(
            [fn](Vec2 worldPos, u32& outId, f32& outDistSq) -> bool {
                sol::protected_function_result result = fn(worldPos.x, worldPos.y);
                if (!result.valid()) return false;
                sol::object ret = result;
                if (ret.is<bool>() && !ret.as<bool>()) return false;
                if (ret.is<u32>()) {
                    outId = ret.as<u32>();
                    outDistSq = 0.0f;
                    return true;
                }
                if (ret.is<int>()) {
                    const int v = ret.as<int>();
                    if (v <= 0) return false;
                    outId = static_cast<u32>(v);
                    outDistSq = 0.0f;
                    return true;
                }
                return false;
            });
    };
    pt["unregister"] = [](u32 handle) {
        Scene::PickQueryRegistry::instance().unregisterPicker(handle);
    };
    pt["query"] = [&lua, worldPtr](f32 wx, f32 wy) -> sol::table {
        sol::table t = lua.create_table();
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return t;
        const Scene::PickHit hit = Scene::PickQueryRegistry::instance().pickAtWorld(*world, {wx, wy});
        if (hit.kind == Scene::PickHit::Kind::Entity) {
            t["kind"] = "entity";
            t["entityId"] = hit.entity.id();
        } else if (hit.kind == Scene::PickHit::Kind::Custom) {
            t["kind"] = "custom";
            t["customId"] = hit.customId;
        } else {
            t["kind"] = "none";
        }
        return t;
    };
    pt["raycast"] = [worldPtr, searchRoot](sol::table origin, sol::table dir) -> u32 {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return 0;
        const Vec3 o{origin["x"].get_or(0.0f), origin["y"].get_or(0.0f), origin["z"].get_or(0.0f)};
        const Vec3 d{dir["x"].get_or(0.0f), dir["y"].get_or(0.0f), dir["z"].get_or(1.0f)};
        const std::string root = searchRoot ? *searchRoot : std::string();
        ECS::Entity hit = Scene::raycastSelectEntity(o, d.normalized(), *world, root);
        return hit.isValid() ? hit.id() : 0;
    };

    lua["caffeine"]["draw2d"] = lua.create_table();
    sol::table dt = lua["caffeine"]["draw2d"];
    dt["beginFrame"] = []() { Render::Overlay2D::beginFrame(); };
    dt["line"] = [](sol::table a, sol::table b, sol::table color, sol::optional<f32> thickness,
                     sol::optional<std::string> space) {
        Render::Overlay2D::line(vec2FromTable(a), vec2FromTable(b), colorFromTable(color),
                                  thickness.value_or(1.0f),
                                  spaceFromString(space.value_or("world")));
    };
    dt["circle"] = [](sol::table center, f32 radius, sol::table color, sol::optional<f32> thickness,
                      sol::optional<std::string> space) {
        Render::Overlay2D::circle(vec2FromTable(center), radius, colorFromTable(color),
                                  thickness.value_or(1.0f), 32,
                                  spaceFromString(space.value_or("world")));
    };
    dt["rect"] = [](sol::table min, sol::table max, sol::table color, sol::optional<f32> thickness,
                    sol::optional<std::string> space) {
        Render::Overlay2D::rect(vec2FromTable(min), vec2FromTable(max), colorFromTable(color),
                                thickness.value_or(1.0f), spaceFromString(space.value_or("world")));
    };
    dt["poly"] = [](sol::table points, sol::table color, sol::optional<bool> closed,
                    sol::optional<f32> thickness, sol::optional<std::string> space) {
        std::vector<Vec2> verts;
        points.for_each([&](sol::object, sol::object value) {
            if (value.is<sol::table>()) verts.push_back(vec2FromTable(value.as<sol::table>()));
        });
        if (verts.size() < 2) return;
        Render::Overlay2D::polyline(verts.data(), static_cast<u32>(verts.size()), colorFromTable(color),
                                    thickness.value_or(1.0f), closed.value_or(false),
                                    spaceFromString(space.value_or("world")));
    };

    lua["caffeine"]["assets"] = lua.create_table();
    sol::table at = lua["caffeine"]["assets"];
    at["loadText"] = [&lua, assetLoader](const std::string& path) -> sol::object {
        if (!assetLoader) return sol::lua_nil;
        std::string text;
        std::string err;
        if (!assetLoader->loadCached(path, text, &err)) {
            return sol::make_object(lua, err);
        }
        return sol::make_object(lua, text);
    };

    lua["caffeine"]["native"] = lua.create_table();
    sol::table nt = lua["caffeine"]["native"];
    nt["load"] = [](const std::string& name) {
        std::string err;
        return NativeModuleHost::instance().load(name, &err);
    };
    nt["call"] = [&lua](const std::string& module, const std::string& fn,
                        sol::optional<std::string> argsJson) -> sol::object {
        std::string err;
        std::string result;
        const std::string args = argsJson.value_or("{}");
        if (!NativeModuleHost::instance().call(module, fn, args, result, &err)) {
            return sol::make_object(lua, err);
        }
        return sol::make_object(lua, result);
    };

    lua["caffeine"]["map"] = lua.create_table();
    sol::table mt = lua["caffeine"]["map"];
    mt["onChunkRequest"] = [&lua](sol::protected_function fn) -> u32 {
        sol::table et = lua["caffeine"]["events"];
        return et["on"]("mapChunkRequest", fn);
    };

    sol::table uit = lua["caffeine"]["ui"];
    uit["createCanvas"] = [uiPtr, worldPtr](sol::optional<f32> w, sol::optional<f32> h) -> u32 {
        UI::UISystem* ui = uiPtr ? *uiPtr : nullptr;
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!ui || !world) return 0;
        ECS::Entity e = ui->createCanvas(*world, {w.value_or(1280.0f), h.value_or(720.0f)});
        return e.id();
    };
    uit["createLabel"] = [uiPtr, worldPtr](u32 parentId, const std::string& text, sol::table pos) -> u32 {
        UI::UISystem* ui = uiPtr ? *uiPtr : nullptr;
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!ui || !world) return 0;
        ECS::Entity e = ui->createLabel(*world, parentId, text.c_str(), vec2FromTable(pos));
        return e.id();
    };
    uit["createButton"] = [uiPtr, worldPtr](u32 parentId, const std::string& text, sol::table pos,
                                            sol::optional<f32> w, sol::optional<f32> h) -> u32 {
        UI::UISystem* ui = uiPtr ? *uiPtr : nullptr;
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!ui || !world) return 0;
        ECS::Entity e = ui->createButton(*world, parentId, text.c_str(), vec2FromTable(pos),
                                         {w.value_or(120.0f), h.value_or(40.0f)});
        return e.id();
    };
    uit["hitTest"] = [uiPtr, worldPtr](f32 sx, f32 sy) -> u32 {
        UI::UISystem* ui = uiPtr ? *uiPtr : nullptr;
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!ui || !world) return 0;
        ECS::Entity e = ui->hitTest(*world, {sx, sy});
        return e.isValid() ? e.id() : 0;
    };

    if (loadModuleFn) {
        lua["require"] = [loadModuleFn](const std::string& name) -> sol::object {
            std::string err;
            const std::string path = moduleNameToPath(name);
            if (!loadModuleFn(path, &err)) {
                throw sol::error("require('" + name + "') failed: " + err);
            }
            return sol::lua_nil;
        };
        lua["caffeine"]["require"] = lua["require"];
    }
}

}  // namespace Caffeine::Script
