#pragma once

#include "editor/EditorContext.hpp"
#include "effects/EffectSystem.hpp"
#include "effects/EffectTypes.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"

namespace Caffeine::Editor {

inline ECS::Entity spawnEffectObject(ECS::World& world, EditorContext& ctx, const char* name,
                                    const Effects::EffectComponent& effect, bool twoD) {
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity entity = world.create();
    setEntityName(world, entity, name);
    if (twoD) {
        world.add<ECS::Transform>(entity);
    } else {
        world.add<ECS::Position3D>(entity);
        world.add<ECS::Rotation3D>(entity);
        world.add<ECS::Scale3D>(entity);
    }
    world.add<Effects::EffectComponent>(entity, effect);
    if (Effects::EffectComponent* stored = world.get<Effects::EffectComponent>(entity)) {
        if (stored->kind == static_cast<u8>(Effects::EffectKind::VolumetricLight)) {
            Effects::ensureVolumetricMesh(world, entity, *stored);
        }
    }
    ctx.selectEntity(entity);
    ctx.markDirty();
    ctx.endUndo(world);
    return entity;
}

}  // namespace Caffeine::Editor
