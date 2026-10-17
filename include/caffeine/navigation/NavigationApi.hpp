#pragma once

#include "navigation/NavigationSystem.hpp"

#include <cstring>
#include <string>

namespace Caffeine::Navigation {

inline NavAgent* ensureAgent(ECS::World& world, ECS::Entity entity) {
    if (!entity.isValid()) return nullptr;
    if (!world.has<NavAgent>(entity)) world.add<NavAgent>(entity);
    return world.get<NavAgent>(entity);
}

inline bool setDestination(ECS::World& world, ECS::Entity entity, const Vec3& destination) {
    NavAgent* agent = ensureAgent(world, entity);
    if (!agent) return false;
    agent->destination = destination;
    agent->hasDestination = true;
    agent->planDirty = true;
    if (agent->mode == NavMode::Idle) agent->mode = NavMode::Scripted;
    return true;
}

inline bool setMode(ECS::World& world, ECS::Entity entity, NavMode mode) {
    NavAgent* agent = ensureAgent(world, entity);
    if (!agent) return false;
    agent->mode = mode;
    agent->planDirty = true;
    return true;
}

inline NavMode modeFromName(const std::string& name) {
    if (name == "patrol") return NavMode::Patrol;
    if (name == "follow") return NavMode::Follow;
    if (name == "scripted") return NavMode::Scripted;
    return NavMode::Idle;
}

inline const char* modeName(NavMode mode) {
    switch (mode) {
        case NavMode::Patrol: return "patrol";
        case NavMode::Follow: return "follow";
        case NavMode::Scripted: return "scripted";
        case NavMode::Idle: return "idle";
    }
    return "idle";
}

inline bool addPatrolPoint(ECS::World& world, ECS::Entity entity, const Vec3& point) {
    NavAgent* agent = ensureAgent(world, entity);
    if (!agent || agent->patrolCount >= static_cast<u32>(kMaxPatrolPoints)) return false;
    agent->patrol[agent->patrolCount++] = point;
    agent->mode = NavMode::Patrol;
    agent->planDirty = true;
    return true;
}

inline bool setFollowTarget(ECS::World& world, ECS::Entity entity, u32 targetId) {
    NavAgent* agent = ensureAgent(world, entity);
    if (!agent) return false;
    agent->followEntity = targetId;
    agent->mode = NavMode::Follow;
    agent->planDirty = true;
    return true;
}

inline bool setBehaviorScript(ECS::World& world, ECS::Entity entity, const std::string& path) {
    NavAgent* agent = ensureAgent(world, entity);
    if (!agent) return false;
    std::memset(agent->behaviorScript, 0, sizeof(agent->behaviorScript));
    const size_t count = std::min(path.size(), sizeof(agent->behaviorScript) - 1);
    if (count > 0) std::memcpy(agent->behaviorScript, path.data(), count);
    return true;
}

}  // namespace Caffeine::Navigation
