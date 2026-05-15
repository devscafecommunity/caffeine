#include "script/ScriptSystem.hpp"
#include "script/ScriptTypes.hpp"
#include "ecs/World.hpp"
#include "ecs/ComponentQuery.hpp"
#include "debug/LogSystem.hpp"

namespace Caffeine::Script {

ScriptSystem::ScriptSystem(ScriptEngine* engine)
    : m_engine(engine) {}

void ScriptSystem::onUpdate(ECS::World& world, f32 dt) {
    if (!m_engine) return;
    processLuaScripts(world, dt);
    processNativeScripts(world, dt);
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

        m_engine->callOnUpdate(path, entity, dt);
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

} // namespace Caffeine::Script
