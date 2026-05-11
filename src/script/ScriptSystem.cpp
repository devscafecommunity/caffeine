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

    ECS::ComponentQuery q;
    q.with<ScriptComponent>();

    // Phase 1: Collect entity data without processing scripts.
    // We must NOT call callOnCreate/callOnUpdate during forEach because those
    // can add components (e.g., Position2D via setTransform), triggering
    // archetype migration via World::add. This invalidates forEach's iteration
    // state (cached entityCount becomes stale, out-of-bounds access → SIGSEGV).
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

    // Phase 2: Process each entity outside forEach, safe to modify archetypes.
    for (auto& entry : entries) {
        ECS::Entity entity(entry.entityId, &world);
        if (!entity.isValid()) continue;

        const std::string& path = entry.scriptPath;

        // Load script if not yet loaded
        if (!m_engine->isLoaded(path)) {
            std::string err;
            if (!m_engine->loadScript(path, &err)) {
                CF_ERROR("Script", "Failed to load %s: %s",
                         path.c_str(), err.c_str());
                continue;
            }
        }

        // First encounter: call onCreate
        bool isNew = true;
        for (usize i = 0; i < m_initialized.size(); ++i) {
            if (m_initialized[i].id() == entity.id()) {
                isNew = false;
                break;
            }
        }

        if (isNew) {
            m_engine->callOnCreate(path, entity);
            m_initialized.pushBack(entity);
        }

        // Per-frame update
        m_engine->callOnUpdate(path, entity, dt);
    }
}

} // namespace Caffeine::Script
