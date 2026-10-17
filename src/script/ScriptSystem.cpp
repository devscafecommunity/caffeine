#include "script/ScriptSystem.hpp"
#include "script/ScriptTypes.hpp"
#include "script/CppScript.hpp"
#include "ecs/World.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "debug/LogSystem.hpp"
#include "render/Overlay2D.hpp"
#include "runtime/GameStateStack.hpp"
#include "runtime/PlayClock.hpp"
#include "scene/MapSurfaceSystem.hpp"
#include "scene/PickQuery.hpp"
#include "scene/PlayMode2D.hpp"
#include "scene/SpriteLod2DSystem.hpp"
#include "events/EventBus.hpp"

namespace Caffeine::Script {

ScriptSystem::ScriptSystem(ScriptEngine* engine, Events::EventBus* eventBus)
    : m_engine(engine), m_events(eventBus) {}

void ScriptSystem::resetPlayState() {
    m_initializedLua.clear();
    m_initializedNative.clear();
    m_initializedPostProcess.clear();
    m_warnedNoLua = false;
    Scene::PickQueryRegistry::instance().clear();
    Scene::resetMapSurfaceChunkRequestCache();
    Runtime::PlayClock::instance().reset();
    Runtime::GameStateStack::instance().clear();
    Render::Overlay2D::beginFrame();
}

void ScriptSystem::onUpdate(ECS::World& world, f32 dt) {
    if (!m_engine) return;
    Runtime::PlayClock::instance().advance(dt);
    Render::Overlay2D::beginFrame();
    if (m_events && Scene::PlayRenderContext::instance().has2D()) {
        const auto& cam = Scene::PlayRenderContext::instance().camera2D();
        Scene::applySpriteLod2D(world, cam.zoom());
        Scene::tickMapSurfaceChunkRequests(world, *m_events, &cam, true);
    }
    processLuaScripts(world, dt);
    processNativeScripts(world, dt);
    processCppScripts(world, dt);
    processPostProcessScripts(world, dt);
}

void ScriptSystem::processLuaScripts(ECS::World& world, f32 dt) {
    ECS::ComponentQuery q;
    q.with<ScriptComponent>();

    struct ScriptEntry {
        u32 entityId;
        std::string scriptPath;
    };
    Vector<ScriptEntry> entries;

    world.forEach<ScriptComponent>(q,
        [&entries](ECS::Entity entity, ScriptComponent& sc) {
            if (sc.scriptPath.empty()) return;
            entries.pushBack({entity.id(), sc.scriptPath});
        });

    if (entries.empty()) {
        if (!m_warnedNoLua) {
            CF_WARN("Script", "Play mode: no Lua ScriptComponent found on any entity");
            m_warnedNoLua = true;
        }
        return;
    }

    for (auto& entry : entries) {
        ECS::Entity entity(entry.entityId, &world);
        if (!entity.isValid()) continue;

        const std::string& path = entry.scriptPath;

        if (!m_engine->isLoaded(path)) {
            std::string err;
            if (!m_engine->loadScript(path, &err)) {
                CF_ERROR("Script", "Failed to load %s: %s",
                         path.c_str(), err.c_str());
                continue;
            }
        }

        bool isNew = true;
        for (usize i = 0; i < m_initializedLua.size(); ++i) {
            if (m_initializedLua[i].id() == entity.id()) {
                isNew = false;
                break;
            }
        }

        if (isNew) {
            m_engine->callOnCreate(path, entity);
            m_initializedLua.pushBack(entity);
        }

        m_engine->callOnUpdate(path, entity, Runtime::PlayClock::instance().deltaTime());

        const f32 fixedDt = Runtime::PlayClock::instance().fixedDeltaTime();
        const int fixedSteps = Runtime::PlayClock::instance().consumeFixedSteps();
        for (int step = 0; step < fixedSteps; ++step) {
            m_engine->callOnFixedUpdate(path, entity, fixedDt);
        }
    }
}

void ScriptSystem::processNativeScripts(ECS::World& world, f32 dt) {
    ECS::ComponentQuery q;
    q.with<NativeScriptComponent>();

    struct NativeScriptEntry {
        u32 entityId;
        NativeScriptComponent* script;
    };
    Vector<NativeScriptEntry> entries;

    world.forEach<NativeScriptComponent>(q,
        [&entries](ECS::Entity entity, NativeScriptComponent& nsc) {
            entries.pushBack({entity.id(), &nsc});
        });

    for (auto& entry : entries) {
        ECS::Entity entity(entry.entityId, &world);
        if (!entity.isValid()) continue;

        NativeScriptComponent* nsc = entry.script;
        if (!nsc) continue;

        if (!nsc->initialized) {
            if (nsc->onCreate) {
                nsc->onCreate(entity);
            }
            nsc->initialized = true;

            bool alreadyTracked = false;
            for (usize i = 0; i < m_initializedNative.size(); ++i) {
                if (m_initializedNative[i].id() == entity.id()) {
                    alreadyTracked = true;
                    break;
                }
            }
            if (!alreadyTracked) {
                m_initializedNative.pushBack(entity);
            }
        }

        if (nsc->onUpdate) {
            nsc->onUpdate(entity, dt);
        }
    }
}

void ScriptSystem::processCppScripts(ECS::World& world, f32 dt) {
    ECS::ComponentQuery q;
    q.with<CppScriptComponent>();

    struct Entry { u32 entityId; CppScriptComponent* script; };
    Vector<Entry> entries;

    world.forEach<CppScriptComponent>(q,
        [&entries](ECS::Entity entity, CppScriptComponent& csc) {
            if (!csc.className.empty()) entries.pushBack({entity.id(), &csc});
        });

    for (auto& entry : entries) {
        ECS::Entity entity(entry.entityId, &world);
        if (!entity.isValid()) continue;

        CppScriptComponent* csc = entry.script;
        if (!csc) continue;

        if (!csc->instance) {
            csc->instance = CppScriptRegistry::instance().create(csc->className);
            if (!csc->instance) {
                CF_ERROR("Script", "C++ script '%s' not registered", csc->className.c_str());
                continue;
            }
        }

        if (!csc->initialized) {
            csc->instance->onCreate(entity, world);
            csc->initialized = true;
        }

        csc->instance->onUpdate(entity, world, dt);
    }
}

void ScriptSystem::processPostProcessScripts(ECS::World& world, f32 dt) {
    ECS::ComponentQuery q;
    q.with<ECS::PostProcessComponent>();

    struct Entry {
        u32 entityId;
        std::string scriptPath;
    };
    Vector<Entry> entries;
    world.forEach<ECS::PostProcessComponent>(q, [&entries](ECS::Entity entity, ECS::PostProcessComponent& fx) {
        if (fx.customEffectScript[0] != '\0') entries.pushBack({entity.id(), fx.customEffectScript});
    });

    for (auto& entry : entries) {
        ECS::Entity entity(entry.entityId, &world);
        if (!entity.isValid()) continue;

        const std::string key = "postprocess:" + entry.scriptPath;
        if (!m_engine->isLoaded(key)) {
            std::string err;
            if (!m_engine->loadScriptAs(key, entry.scriptPath, &err)) {
                CF_ERROR("Script", "Failed to load post-process script %s: %s", entry.scriptPath.c_str(),
                         err.c_str());
                ECS::PostProcessComponent* fx = entity.get<ECS::PostProcessComponent>();
                if (fx) fx->customEffectScript[0] = '\0';
                continue;
            }
        }

        bool isNew = true;
        for (usize i = 0; i < m_initializedPostProcess.size(); ++i) {
            if (m_initializedPostProcess[i].id() == entity.id()) {
                isNew = false;
                break;
            }
        }
        if (isNew) {
            m_engine->callOnCreate(key, entity);
            m_initializedPostProcess.pushBack(entity);
        }
        m_engine->callOnPostProcess(key, entity, dt);
    }
}

}
