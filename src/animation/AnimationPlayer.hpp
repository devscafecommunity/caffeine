#pragma once

#include "animation/ClipAsset.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/World.hpp"

#include <cmath>
#include <cstring>

namespace Caffeine::Animation {

/// Plays a MotionClip on a sprite (2D) and/or a local transform (2D or 3D).
struct AnimationPlayer {
    char clipPath[260] = {};
    f32 time = 0.0f;
    f32 speed = 1.0f;
    bool playing = true;
    bool loop = true;
    bool loaded = false;
    MotionClip clip;
};

inline void tickAnimationPlayers(ECS::World& world, f32 dt) {
    ECS::ComponentQuery query;
    query.with<AnimationPlayer>();
    world.forEach<AnimationPlayer>(query, [&](ECS::Entity entity, AnimationPlayer& player) {
        if (!player.loaded && player.clipPath[0] != '\0') {
            player.loaded = loadMotionClip(player.clipPath, player.clip);
            if (player.loaded) player.loop = player.clip.loop;
        }
        if (!player.loaded || !player.playing) return;

        player.time += dt * player.speed;
        const f32 duration = motionClipDuration(player.clip);
        if (duration > 0.0f && player.time >= duration) {
            if (player.loop) {
                player.time = std::fmod(player.time, duration);
            } else {
                player.time = duration;
                player.playing = false;
            }
        }

        if (!player.clip.spriteFrames.empty()) {
            if (ECS::Sprite* sprite = world.get<ECS::Sprite>(entity)) {
                sprite->frameIndex = sampleSpriteFrame(player.clip, player.time);
            }
        }
        if (!player.clip.rotations.empty()) {
            const Quat sampled = sampleRotation(player.clip, player.time);
            if (ECS::Rotation3D* rotation = world.get<ECS::Rotation3D>(entity)) {
                rotation->quaternion = Vec4(sampled.x, sampled.y, sampled.z, sampled.w);
            }
        }
        if (!player.clip.positions.empty()) {
            const Vec3 sampled = samplePosition(player.clip, player.time);
            if (ECS::Position3D* position = world.get<ECS::Position3D>(entity)) {
                position->position = sampled;
            } else if (ECS::Transform* transform = world.get<ECS::Transform>(entity)) {
                transform->position = sampled;
            } else if (ECS::Position2D* position2d = world.get<ECS::Position2D>(entity)) {
                position2d->x = sampled.x;
                position2d->y = sampled.y;
            }
        }
    });
}

inline void setPlayerPath(AnimationPlayer& player, const std::string& path) {
    std::memset(player.clipPath, 0, sizeof(player.clipPath));
    if (path.empty()) return;
    const size_t count = std::min(path.size(), sizeof(player.clipPath) - 1);
    std::memcpy(player.clipPath, path.data(), count);
}

}  // namespace Caffeine::Animation
