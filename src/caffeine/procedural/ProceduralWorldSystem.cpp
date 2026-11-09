#include "procedural/ProceduralWorldSystem.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/ProceduralComponents.hpp"
#include "procedural/ProceduralStreamer.hpp"
#include "script/ScriptTypes.hpp"

namespace Caffeine::Procedural {

void ProceduralWorldSystem::reset() {
    ProceduralStreamer::instance().reset();
}

void ProceduralWorldSystem::update(ECS::World& world) {
    ECS::ComponentQuery q;
    q.with<ECS::ProceduralWorldComponent>();

    world.forEach<ECS::ProceduralWorldComponent>(q,
        [&](ECS::Entity entity, ECS::ProceduralWorldComponent& settings) {
            if (!settings.enabled) return;

            if (world.has<Script::ScriptComponent>(entity)) {
                return;
            }

            i32 cx = 0;
            i32 cz = 0;
            ProceduralStreamer::focusChunkFromEntity(world, settings, cx, cz);
            ProceduralStreamer::instance().streamAround(world, entity, settings, cx, cz);
        });
}

}  // namespace Caffeine::Procedural
