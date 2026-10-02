#pragma once

#include "navigation/NavVolume.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/World.hpp"

namespace Caffeine::Navigation {

inline NavVolume* findVolume(ECS::World& world) {
    ECS::ComponentQuery query;
    query.with<NavVolume>();
    NavVolume* found = nullptr;
    world.forEach<NavVolume>(query, [&](ECS::Entity, NavVolume& volume) {
        if (!found) found = &volume;
    });
    return found;
}

inline Vec3 readAgentPosition(ECS::World& world, ECS::Entity entity, bool& is2d) {
    is2d = false;
    if (const ECS::Position3D* position = world.get<ECS::Position3D>(entity)) {
        return position->position;
    }
    if (const ECS::Transform* transform = world.get<ECS::Transform>(entity)) {
        return transform->position;
    }
    if (const ECS::Position2D* position = world.get<ECS::Position2D>(entity)) {
        is2d = true;
        return Vec3(position->x, 0.0f, position->y);
    }
    return {};
}

inline void writeAgentPosition(ECS::World& world, ECS::Entity entity, const Vec3& position, bool is2d) {
    if (!is2d) {
        if (ECS::Position3D* component = world.get<ECS::Position3D>(entity)) {
            component->position = position;
            return;
        }
        if (ECS::Transform* transform = world.get<ECS::Transform>(entity)) {
            transform->position = position;
            return;
        }
    }
    if (ECS::Position2D* component = world.get<ECS::Position2D>(entity)) {
        component->x = position.x;
        component->y = is2d ? position.z : position.y;
    }
}

inline void steerAgent(ECS::World& world, ECS::Entity entity, NavAgent& agent, NavVolume* volume, f32 dt) {
    if (agent.mode == NavMode::Follow && agent.followEntity != u32_max) {
        ECS::Entity target(agent.followEntity, &world);
        if (target.isValid()) {
            bool ignored = false;
            agent.destination = readAgentPosition(world, target, ignored);
            agent.hasDestination = true;
        }
    } else if (agent.mode == NavMode::Patrol && agent.patrolCount > 0) {
        agent.patrolIndex %= agent.patrolCount;
        agent.destination = agent.patrol[agent.patrolIndex];
        agent.hasDestination = true;
    }

    if (!agent.hasDestination || agent.mode == NavMode::Idle || !volume) return;

    bool is2d = false;
    Vec3 position = readAgentPosition(world, entity, is2d);
    if (agent.planDirty || (agent.destination - agent.plannedDestination).lengthSquared() > 0.01f) {
        agent.path.clear();
        agent.pathCursor = 0;
        volume->findPath(position, agent.destination, agent.path);
        agent.plannedDestination = agent.destination;
        agent.planDirty = false;
    }
    if (agent.path.empty()) return;

    while (agent.pathCursor < agent.path.size()) {
        const Vec3 target = agent.path[agent.pathCursor];
        Vec3 delta = target - position;
        if (is2d) delta.y = 0.0f;
        const f32 distance = delta.length();
        if (distance <= agent.arriveRadius) {
            ++agent.pathCursor;
            if (agent.pathCursor >= agent.path.size() && agent.mode == NavMode::Patrol &&
                agent.patrolCount > 0) {
                agent.patrolIndex = (agent.patrolIndex + 1) % agent.patrolCount;
                agent.planDirty = true;
            }
            continue;
        }
        const f32 step = std::min(distance, std::max(agent.speed, 0.0f) * dt);
        position = position + delta * (step / distance);
        writeAgentPosition(world, entity, position, is2d);
        return;
    }
}

inline void updateNavigation(ECS::World& world, f32 dt) {
    NavVolume* volume = findVolume(world);
    ECS::ComponentQuery query;
    query.with<NavAgent>();
    world.forEach<NavAgent>(query, [&](ECS::Entity entity, NavAgent& agent) {
        steerAgent(world, entity, agent, volume, dt);
    });
}

}  // namespace Caffeine::Navigation
