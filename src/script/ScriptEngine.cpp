#include "script/ScriptEngine.hpp"
#include "script/ScriptTypes.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "input/InputManager.hpp"
#include "events/EventBus.hpp"
#include "debug/LogSystem.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "containers/HashMap.hpp"
#include "containers/Vector.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {

// ============================================================================
// Internal implementation (Pimpl) — all sol2 types are here
// ============================================================================

struct ScriptEngine::Impl {
    sol::state m_lua;

    ECS::World* m_world = nullptr;
    Input::InputManager* m_input = nullptr;
    Events::EventBus* m_events = nullptr;

    HashMap<std::string, sol::environment> m_envs;

    struct LuaEventEntry {
        std::string eventName;
        u32 id;
        sol::protected_function callback;
    };
    Vector<LuaEventEntry> m_luaEvents;
    u32 m_nextEventHandlerId = 1;
};

namespace {

HashMap<std::string, Input::Key> makeKeyMap() {
    HashMap<std::string, Input::Key> map;
    map.set("Space", Input::Key::Space); map.set("Return", Input::Key::Return);
    map.set("Escape", Input::Key::Escape); map.set("Backspace", Input::Key::Backspace);
    map.set("Tab", Input::Key::Tab);
    map.set("Up", Input::Key::Up); map.set("Down", Input::Key::Down);
    map.set("Left", Input::Key::Left); map.set("Right", Input::Key::Right);
    map.set("A", Input::Key::A); map.set("B", Input::Key::B);
    map.set("C", Input::Key::C); map.set("D", Input::Key::D);
    map.set("E", Input::Key::E); map.set("F", Input::Key::F);
    map.set("G", Input::Key::G); map.set("H", Input::Key::H);
    map.set("I", Input::Key::I); map.set("J", Input::Key::J);
    map.set("K", Input::Key::K); map.set("L", Input::Key::L);
    map.set("M", Input::Key::M); map.set("N", Input::Key::N);
    map.set("O", Input::Key::O); map.set("P", Input::Key::P);
    map.set("Q", Input::Key::Q); map.set("R", Input::Key::R);
    map.set("S", Input::Key::S); map.set("T", Input::Key::T);
    map.set("U", Input::Key::U); map.set("V", Input::Key::V);
    map.set("W", Input::Key::W); map.set("X", Input::Key::X);
    map.set("Y", Input::Key::Y); map.set("Z", Input::Key::Z);
    map.set("0", Input::Key::Num0); map.set("1", Input::Key::Num1);
    map.set("2", Input::Key::Num2); map.set("3", Input::Key::Num3);
    map.set("4", Input::Key::Num4); map.set("5", Input::Key::Num5);
    map.set("6", Input::Key::Num6); map.set("7", Input::Key::Num7);
    map.set("8", Input::Key::Num8); map.set("9", Input::Key::Num9);
    map.set("LShift", Input::Key::LShift); map.set("RShift", Input::Key::RShift);
    map.set("LCtrl", Input::Key::LCtrl); map.set("RCtrl", Input::Key::RCtrl);
    map.set("LAlt", Input::Key::LAlt); map.set("RAlt", Input::Key::RAlt);
    return map;
}

HashMap<std::string, Input::Axis> makeAxisMap() {
    HashMap<std::string, Input::Axis> map;
    map.set("Horizontal", Input::Axis::MoveX);
    map.set("Vertical", Input::Axis::MoveY);
    map.set("LookX", Input::Axis::LookX);
    map.set("LookY", Input::Axis::LookY);
    return map;
}

// ============================================================================
// Binding registration helpers
// ============================================================================

void registerWorldBindings(sol::state& lua, ECS::World* world) {
    lua["caffeine"]["world"] = lua.create_table();
    sol::table wt = lua["caffeine"]["world"];

    wt["create"] = [world]() -> u32 {
        ECS::Entity e = world->create("LuaEntity");
        return e.id();
    };

    wt["destroy"] = [world](u32 entityId) {
        world->destroy(ECS::Entity(entityId, world));
    };

    wt["hasComponent"] = [world](u32 entityId, const std::string& type) -> bool {
        ECS::Entity e(entityId, world);
        if (type == "Transform")  return e.has<ECS::Transform>();
        if (type == "Velocity2D") return e.has<ECS::Velocity2D>();
        if (type == "Sprite")     return e.has<ECS::Sprite>();
        if (type == "Health")     return e.has<ECS::Health>();
        if (type == "RigidBody2D") return e.has<Physics2D::RigidBody2D>();
        if (type == "Collider2D") return e.has<Physics2D::Collider2D>();
        return false;
    };

    wt["getTransform"] = [&lua, world](u32 entityId) -> sol::table {
        ECS::Entity e(entityId, world);
        sol::table t = lua.create_table();
        auto* transform = e.get<ECS::Transform>();
        t["x"] = transform ? transform->position.x : 0.0f;
        t["y"] = transform ? transform->position.y : 0.0f;
        t["z"] = transform ? transform->position.z : 0.0f;
        t["rotation"] = transform ? transform->rotation.z : 0.0f;
        t["scaleX"] = transform ? transform->scale.x : 1.0f;
        t["scaleY"] = transform ? transform->scale.y : 1.0f;
        return t;
    };

    wt["setTransform"] = [world](u32 entityId, sol::table t) {
        ECS::Entity e(entityId, world);
        auto& transform = e.getOrAdd<ECS::Transform>();
        transform.position.x = t["x"].get_or(0.0f);
        transform.position.y = t["y"].get_or(0.0f);
        transform.position.z = t["z"].get_or(0.0f);
        transform.rotation.z = t["rotation"].get_or(0.0f);
        transform.scale.x = t["scaleX"].get_or(1.0f);
        transform.scale.y = t["scaleY"].get_or(1.0f);
    };

    wt["addTransform"] = wt["setTransform"];

    wt["getVelocity"] = [&lua, world](u32 entityId) -> sol::table {
        ECS::Entity e(entityId, world);
        sol::table t = lua.create_table();
        auto* v = e.get<ECS::Velocity2D>();
        t["x"] = v ? v->x : 0.0f;
        t["y"] = v ? v->y : 0.0f;
        return t;
    };

    wt["setVelocity"] = [world](u32 entityId, sol::table t) {
        ECS::Entity e(entityId, world);
        auto& v = e.getOrAdd<ECS::Velocity2D>();
        v.x = t["x"].get_or(0.0f);
        v.y = t["y"].get_or(0.0f);
    };

    wt["getRigidBody2D"] = [&lua, world](u32 entityId) -> sol::table {
        ECS::Entity e(entityId, world);
        sol::table t = lua.create_table();
        auto* rb = e.get<Physics2D::RigidBody2D>();
        if (rb) {
            t["mass"] = rb->mass;
            t["restitution"] = rb->restitution;
            t["friction"] = rb->friction;
            t["linearDamping"] = rb->linearDamping;
            t["isKinematic"] = rb->isKinematic;
            t["lockRotation"] = rb->lockRotation;
        }
        return t;
    };

    wt["setRigidBody2D"] = [world](u32 entityId, sol::table t) {
        ECS::Entity e(entityId, world);
        auto& rb = e.getOrAdd<Physics2D::RigidBody2D>();
        rb.mass = t["mass"].get_or(1.0f);
        rb.restitution = t["restitution"].get_or(0.3f);
        rb.friction = t["friction"].get_or(0.5f);
        rb.linearDamping = t["linearDamping"].get_or(0.0f);
        rb.isKinematic = t["isKinematic"].get_or(false);
        rb.lockRotation = t["lockRotation"].get_or(true);
    };

    wt["getSprite"] = [&lua, world](u32 entityId) -> sol::table {
        ECS::Entity e(entityId, world);
        sol::table t = lua.create_table();
        auto* s = e.get<ECS::Sprite>();
        if (s) {
            t["name"] = s->name;
            t["frameIndex"] = s->frameIndex;
        }
        return t;
    };

    wt["setSprite"] = [world](u32 entityId, sol::table t) {
        ECS::Entity e(entityId, world);
        auto& s = e.getOrAdd<ECS::Sprite>();
        s.name = t["name"].get_or(std::string());
        s.frameIndex = t["frameIndex"].get_or(0u);
    };

    wt["addSprite"] = wt["setSprite"];

    wt["addParticleEmitter"] = [world](u32 entityId, sol::table t) {
        ECS::Entity e(entityId, world);
        auto& p = e.getOrAdd<ECS::ParticleEmitterComponent>();
        p.maxParticles = t["maxParticles"].get_or(100);
        p.emissionRate = t["emissionRate"].get_or(10.0f);
        p.lifetime = t["lifetime"].get_or(2.0f);
        p.startSize = t["startSize"].get_or(1.0f);
        p.endSize = t["endSize"].get_or(0.0f);

        if (t["startColor"].valid()) {
            sol::table sc = t["startColor"];
            u8 r = static_cast<u8>(sc["r"].get_or(1.0f) * 255);
            u8 g = static_cast<u8>(sc["g"].get_or(1.0f) * 255);
            u8 b = static_cast<u8>(sc["b"].get_or(1.0f) * 255);
            u8 a = static_cast<u8>(sc["a"].get_or(1.0f) * 255);
            p.startColor = (r << 24) | (g << 16) | (b << 8) | a;
        }
        if (t["endColor"].valid()) {
            sol::table ec = t["endColor"];
            u8 r = static_cast<u8>(ec["r"].get_or(1.0f) * 255);
            u8 g = static_cast<u8>(ec["g"].get_or(1.0f) * 255);
            u8 b = static_cast<u8>(ec["b"].get_or(1.0f) * 255);
            u8 a = static_cast<u8>(ec["a"].get_or(0.0f) * 255);
            p.endColor = (r << 24) | (g << 16) | (b << 8) | a;
        }

        e.getOrAdd<ECS::Transform>();
    };
}

void registerInputBindings(sol::state& lua, Input::InputManager* input) {
    // Build key/axis name mappings (static, built once)
    static const HashMap<std::string, Input::Key> s_keyMap = makeKeyMap();
    static const HashMap<std::string, Input::Axis> s_axisMap = makeAxisMap();

    sol::table it = lua["caffeine"]["input"];

    it["isKeyDown"] = [input](const std::string& keyName) -> bool {
        auto* key = s_keyMap.get(keyName);
        if (!key) return false;
        return input->isKeyDown(*key);
    };

    it["getAxis"] = [input](const std::string& axisName) -> f32 {
        auto* axis = s_axisMap.get(axisName);
        if (!axis) return 0.0f;
        return input->axisState(*axis).value;
    };

    it["mousePosition"] = [input]() -> std::pair<f32, f32> {
        auto pos = input->mousePosition();
        return {pos.x, pos.y};
    };

    it["isMouseButtonDown"] = [input](u32 btn) -> bool {
        Input::MouseButton mb;
        switch (btn) {
            case 1:  mb = Input::MouseButton::Left; break;
            case 2:  mb = Input::MouseButton::Middle; break;
            case 3:  mb = Input::MouseButton::Right; break;
            default: return false;
        }
        return input->isMouseButtonDown(mb);
    };
}

void registerDebugBindings(sol::state& lua) {
    sol::table dt = lua["caffeine"]["debug"];

    dt["log"] = [](const std::string& msg) {
        Debug::LogSystem::instance().log(
            Debug::LogLevel::Info, "Lua", "%s", msg.c_str());
    };

    dt["warn"] = [](const std::string& msg) {
        Debug::LogSystem::instance().log(
            Debug::LogLevel::Warn, "Lua", "%s", msg.c_str());
    };

    dt["error"] = [](const std::string& msg) {
        Debug::LogSystem::instance().log(
            Debug::LogLevel::Error, "Lua", "%s", msg.c_str());
    };
}

void registerMathBindings(sol::state& lua) {
    sol::table mt = lua["caffeine"]["math"];

    mt["vec2"] = [&lua](f32 x, f32 y) -> sol::table {
        sol::table t = lua.create_table();
        t["x"] = x;
        t["y"] = y;
        return t;
    };

    mt["add"] = [&lua](sol::table a, sol::table b) -> sol::table {
        sol::table t = lua.create_table();
        t["x"] = a["x"].get_or(0.0f) + b["x"].get_or(0.0f);
        t["y"] = a["y"].get_or(0.0f) + b["y"].get_or(0.0f);
        return t;
    };

    mt["sub"] = [&lua](sol::table a, sol::table b) -> sol::table {
        sol::table t = lua.create_table();
        t["x"] = a["x"].get_or(0.0f) - b["x"].get_or(0.0f);
        t["y"] = a["y"].get_or(0.0f) - b["y"].get_or(0.0f);
        return t;
    };

    mt["length"] = [](sol::table v) -> f32 {
        f32 x = v["x"].get_or(0.0f);
        f32 y = v["y"].get_or(0.0f);
        return std::sqrt(x * x + y * y);
    };

    mt["normalize"] = [&lua](sol::table v) -> sol::table {
        f32 x = v["x"].get_or(0.0f);
        f32 y = v["y"].get_or(0.0f);
        f32 len = std::sqrt(x * x + y * y);
        sol::table t = lua.create_table();
        if (len > 0.0001f) {
            t["x"] = x / len;
            t["y"] = y / len;
        } else {
            t["x"] = 0.0f;
            t["y"] = 0.0f;
        }
        return t;
    };

    mt["dot"] = [](sol::table a, sol::table b) -> f32 {
        return a["x"].get_or(0.0f) * b["x"].get_or(0.0f)
             + a["y"].get_or(0.0f) * b["y"].get_or(0.0f);
    };

    mt["mul"] = [&lua](sol::table a, sol::table b) -> sol::table {
        sol::table t = lua.create_table();
        t["x"] = a["x"].get_or(0.0f) * b["x"].get_or(0.0f);
        t["y"] = a["y"].get_or(0.0f) * b["y"].get_or(0.0f);
        return t;
    };

    mt["scale"] = [&lua](sol::table v, f32 s) -> sol::table {
        sol::table t = lua.create_table();
        t["x"] = v["x"].get_or(0.0f) * s;
        t["y"] = v["y"].get_or(0.0f) * s;
        return t;
    };
}

} // anonymous namespace

// ============================================================================
// ScriptEngine implementation
// ============================================================================

ScriptEngine::ScriptEngine()
    : m_impl(std::make_unique<Impl>()) {}

ScriptEngine::~ScriptEngine() = default;

bool ScriptEngine::init(const InitParams& params) {
    m_impl->m_world = params.world;
    m_impl->m_input = params.input;
    m_impl->m_events = params.events;

    auto& lua = m_impl->m_lua;

    // Open only safe libraries
    lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );

    // Override print to route through log system
    lua["print"] = [](sol::this_state s, sol::variadic_args args) {
        lua_State* L = s;
        std::string msg;
        for (auto v : args) {
            if (!msg.empty()) msg += "\t";
            v.push();
            size_t len;
            const char* str = luaL_tolstring(L, -1, &len);
            if (str) {
                msg.append(str, len);
                lua_pop(L, 1);
            }
        }
        Debug::LogSystem::instance().log(
            Debug::LogLevel::Info, "Lua", "%s", msg.c_str());
    };

    // Create root caffeine table + all sub-tables
    lua["caffeine"] = lua.create_table();
    lua["caffeine"]["world"] = lua.create_table();
    lua["caffeine"]["input"] = lua.create_table();
    lua["caffeine"]["events"] = lua.create_table();
    lua["caffeine"]["debug"] = lua.create_table();
    lua["caffeine"]["math"]  = lua.create_table();
    lua["caffeine"]["particles"] = lua.create_table();

    registerWorldBindings(lua, m_impl->m_world);
    registerInputBindings(lua, m_impl->m_input);

    // Event bindings (inline: needs access to Impl internals)
    {
        sol::table et = lua["caffeine"]["events"];
        auto* impl = m_impl.get();

        et["on"] = [impl](const std::string& eventName,
                          sol::protected_function callback) -> u32 {
            u32 handle = impl->m_nextEventHandlerId++;
            impl->m_luaEvents.pushBack(
                {eventName, handle, std::move(callback)});
            return handle;
        };

        et["emit"] = [impl](const std::string& eventName,
                            sol::variadic_args args) {
            for (usize i = 0; i < impl->m_luaEvents.size(); ++i) {
                auto& entry = impl->m_luaEvents[i];
                if (entry.eventName != eventName) continue;
                auto result = entry.callback(args);
                if (!result.valid()) {
                    sol::error err = result;
                    CF_ERROR("Lua", "Event '%s' handler error: %s",
                             eventName.c_str(), err.what());
                }
            }
        };

        et["off"] = [impl](u32 handle) {
            for (usize i = 0; i < impl->m_luaEvents.size(); ++i) {
                if (impl->m_luaEvents[i].id != handle) continue;
                if (i < impl->m_luaEvents.size() - 1) {
                    impl->m_luaEvents[i] = impl->m_luaEvents.back();
                }
                impl->m_luaEvents.popBack();
                return;
            }
        };
    }

    registerDebugBindings(lua);
    registerMathBindings(lua);

    {
        sol::table pt = lua["caffeine"]["particles"];
        auto* impl = m_impl.get();

        pt["emit"] = [impl](u32 entityId, int count) {
            if (!impl->m_world) return;
            ECS::Entity e(entityId, impl->m_world);
            if (!e.isValid()) return;
            auto* emitter = e.get<ECS::ParticleEmitterComponent>();
            if (!emitter) return;
            for (int i = 0; i < count && emitter->activeParticles.size() < static_cast<size_t>(emitter->maxParticles); ++i) {
                ECS::ParticleEmitterComponent::Particle p;
                auto* transform = e.get<ECS::Transform>();
                p.position = transform ? Vec2{transform->position.x, transform->position.y} : Vec2{0, 0};
                p.velocity.x = static_cast<float>(rand() % 200 - 100) / 10.0f;
                p.velocity.y = static_cast<float>(rand() % 200 - 100) / 10.0f;
                p.life = emitter->lifetime;
                p.maxLife = emitter->lifetime;
                p.color = emitter->startColor;
                p.size = emitter->startSize;
                emitter->activeParticles.push_back(p);
            }
        };
    }

    lua["dofile"] = sol::nil;
    lua["load"] = sol::nil;
    lua["loadfile"] = sol::nil;
    lua["os"] = sol::nil;
    lua["io"] = sol::nil;
    lua["package"] = sol::nil;
    lua["debug"] = sol::nil;

    lua.safe_script(R"(
        os = {}
        os.clock = os.clock or function()
            local t = os.date('!*t')
            return t.hour * 3600 + t.min * 60 + t.sec
        end
    )");

    return true;
}

void ScriptEngine::shutdown() {
    m_impl->m_lua.collect_garbage();
    m_impl->m_envs.clear();
    m_impl->m_luaEvents.clear();
}

bool ScriptEngine::loadScript(const std::string& path, std::string* outError) {
    auto& lua = m_impl->m_lua;

    sol::environment env(lua, sol::create, lua.globals());
    auto result = lua.safe_script_file(path, env, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        if (outError) *outError = err.what();
        CF_ERROR("Script", "Failed to load %s: %s", path.c_str(), err.what());
        return false;
    }

    m_impl->m_envs.set(path, std::move(env));
    CF_INFO("Script", "Loaded script: %s", path.c_str());
    return true;
}

bool ScriptEngine::loadString(const std::string& code,
                              const std::string& virtualPath,
                              std::string* outError) {
    auto& lua = m_impl->m_lua;

    sol::environment env(lua, sol::create, lua.globals());
    auto result = lua.safe_script(code, env, sol::script_pass_on_error, virtualPath);
    if (!result.valid()) {
        sol::error err = result;
        if (outError) *outError = err.what();
        return false;
    }

    m_impl->m_envs.set(virtualPath, std::move(env));
    return true;
}

bool ScriptEngine::reloadScript(const std::string& path, std::string* outError) {
    // Re-load from disk — overwrites the stored chunk
    return loadScript(path, outError);
}

bool ScriptEngine::isLoaded(const std::string& path) const {
    return m_impl->m_envs.get(path) != nullptr;
}

bool ScriptEngine::callOnCreate(const std::string& path, ECS::Entity entity) {
    auto* envPtr = m_impl->m_envs.get(path);
    if (!envPtr) return false;
    sol::protected_function fn = (*envPtr)["onCreate"];
    if (!fn.valid()) return false;

    auto result = fn(static_cast<u32>(entity.id()));
    if (!result.valid()) {
        sol::error err = result;
        CF_ERROR("Lua", "onCreate error: %s", err.what());
        return false;
    }
    return true;
}

bool ScriptEngine::callOnUpdate(const std::string& path, ECS::Entity entity,
                                 f32 dt) {
    auto* envPtr = m_impl->m_envs.get(path);
    if (!envPtr) return false;
    sol::protected_function fn = (*envPtr)["onUpdate"];
    if (!fn.valid()) return false;

    auto result = fn(static_cast<u32>(entity.id()), dt);
    if (!result.valid()) {
        sol::error err = result;
        CF_ERROR("Lua", "onUpdate error: %s", err.what());
        return false;
    }
    return true;
}

bool ScriptEngine::callOnDestroy(const std::string& path, ECS::Entity entity) {
    auto* envPtr = m_impl->m_envs.get(path);
    if (!envPtr) return false;
    sol::protected_function fn = (*envPtr)["onDestroy"];
    if (!fn.valid()) return false;

    auto result = fn(static_cast<u32>(entity.id()));
    if (!result.valid()) {
        sol::error err = result;
        CF_ERROR("Lua", "onDestroy error: %s", err.what());
        return false;
    }
    return true;
}

bool ScriptEngine::callOnCollision(const std::string& path, ECS::Entity entity,
                                    ECS::Entity other) {
    auto* envPtr = m_impl->m_envs.get(path);
    if (!envPtr) return false;
    sol::protected_function fn = (*envPtr)["onCollision"];
    if (!fn.valid()) return false;

    auto result = fn(static_cast<u32>(entity.id()),
                     static_cast<u32>(other.id()));
    if (!result.valid()) {
        sol::error err = result;
        CF_ERROR("Lua", "onCollision error: %s", err.what());
        return false;
    }
    return true;
}

} // namespace Caffeine::Script
