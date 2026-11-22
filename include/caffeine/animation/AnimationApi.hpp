#pragma once

#include "animation/AnimationComponents.hpp"
#include "animation/AnimationPlayer.hpp"
#include "animation/Rig2DSystem.hpp"
#include "animation/SkinLibrary.hpp"
#include "ecs/Components.hpp"

#include <algorithm>
#include <cstring>
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

namespace Caffeine::Animation {

/// C++ entry points. Lua `caffeine.animation.*` calls the same functions.

inline bool playSkinnedClip(ECS::World& world, ECS::Entity entity, const std::string& meshPath, i32 clipIndex) {
    if (!entity.isValid()) return false;
    SkinnedPose pose;
    if (SkinnedPose* existing = world.get<SkinnedPose>(entity)) pose = *existing;
    if (!meshPath.empty()) {
        std::memset(pose.meshPath, 0, sizeof(pose.meshPath));
        const size_t count = std::min(meshPath.size(), sizeof(pose.meshPath) - 1);
        std::memcpy(pose.meshPath, meshPath.data(), count);
        pose.loaded = false;
    }
    pose.clipIndex = clipIndex;
    pose.time = 0.0f;
    pose.playing = true;
    if (world.has<SkinnedPose>(entity)) *world.get<SkinnedPose>(entity) = pose;
    else world.add<SkinnedPose>(entity, pose);
    return true;
}

inline bool setSpriteSheet(ECS::World& world, ECS::Entity entity, u32 columns, u32 rows, u32 frameCount) {
    if (!entity.isValid()) return false;
    SpriteSheet sheet;
    sheet.columns = std::max(1u, columns);
    sheet.rows = std::max(1u, rows);
    sheet.frameCount = std::max(1u, frameCount);
    if (world.has<SpriteSheet>(entity)) *world.get<SpriteSheet>(entity) = sheet;
    else world.add<SpriteSheet>(entity, sheet);
    if (!world.has<ECS::Sprite>(entity)) world.add<ECS::Sprite>(entity);
    return true;
}

inline bool playClip(ECS::World& world, ECS::Entity entity, const std::string& path) {
    if (!entity.isValid()) return false;
    MotionClip clip;
    if (!loadMotionClip(path, clip)) return false;

    AnimationPlayer player;
    setPlayerPath(player, path);
    player.clip = std::move(clip);
    player.loaded = true;
    player.playing = true;
    player.loop = player.clip.loop;
    player.time = 0.0f;
    if (world.has<AnimationPlayer>(entity)) {
        *world.get<AnimationPlayer>(entity) = std::move(player);
    } else {
        world.add<AnimationPlayer>(entity, std::move(player));
    }
    return true;
}

inline bool setBool(ECS::World& world, ECS::Entity entity, const char* name, bool value) {
    PhaseShift* animator = world.get<PhaseShift>(entity);
    if (!animator || !name) return false;
    animator->setBool(name, value);
    return true;
}

inline bool setFloat(ECS::World& world, ECS::Entity entity, const char* name, f32 value) {
    PhaseShift* animator = world.get<PhaseShift>(entity);
    if (!animator || !name) return false;
    animator->setFloat(name, value);
    return true;
}

inline bool setTrigger(ECS::World& world, ECS::Entity entity, const char* name) {
    PhaseShift* animator = world.get<PhaseShift>(entity);
    if (!animator || !name) return false;
    animator->setTrigger(name);
    return true;
}

inline bool playState(ECS::World& world, ECS::Entity entity, const char* stateName) {
    PhaseShift* animator = world.get<PhaseShift>(entity);
    if (!animator || !stateName) return false;
    animator->previousState = animator->currentState;
    animator->currentState = stateName;
    animator->timeInState = 0.0f;
    animator->paused = false;
    return true;
}

/// Attaches a 2D rig (.rig2d) to an entity, or swaps the rig of an existing one. Parameters and clips reset.
inline bool attachRig2D(ECS::World& world, ECS::Entity entity, const std::string& rigPath) {
    if (!entity.isValid() || rigPath.empty()) return false;
    if (Rig2DPlayer* existing = world.get<Rig2DPlayer>(entity)) {
        setRig2DPath(existing->rigPath, sizeof(existing->rigPath), rigPath);
        existing->rigAttempted = false;
        existing->rigValid = false;
        existing->clipAttempted = false;
        existing->clipValid = false;
        existing->time = 0.0f;
        return true;
    }
    Rig2DPlayer player;
    setRig2DPath(player.rigPath, sizeof(player.rigPath), rigPath);
    world.add<Rig2DPlayer>(entity, std::move(player));
    return true;
}

/// Plays a parameter clip (.pclip) on the entity's rig from time zero. The rig must be attached first.
inline bool playRig2DClip(ECS::World& world, ECS::Entity entity, const std::string& clipPath) {
    Rig2DPlayer* player = entity.isValid() ? world.get<Rig2DPlayer>(entity) : nullptr;
    if (!player || clipPath.empty()) return false;
    setRig2DPath(player->clipPath, sizeof(player->clipPath), clipPath);
    player->clipAttempted = false;
    player->clipValid = false;
    player->time = 0.0f;
    player->playing = true;
    return true;
}

/// Smooths a float PhaseShift parameter over `smoothTime` seconds. Zero turns smoothing off.
inline bool setPhaseShiftSmoothing(ECS::World& world, ECS::Entity entity, const char* name, f32 smoothTime) {
    PhaseShift* phaseShift = entity.isValid() ? world.get<PhaseShift>(entity) : nullptr;
    if (!phaseShift || !name) return false;
    phaseShift->setSmoothing(name, smoothTime);
    return true;
}

}  // namespace Caffeine::Animation
