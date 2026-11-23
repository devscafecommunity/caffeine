#pragma once

#include "animation/AnimationComponents.hpp"
#include "animation/Rig2DAsset.hpp"
#include "animation/Skeleton2D.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "math/Mat4.hpp"
#include "math/Vec4.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Caffeine::Animation {

/// Plays a 2D rig on an entity. Each tick the rig parameters are filled in three layers:
///   1. defaults of the rig,
///   2. the parameter clip at `time`, if one is set,
///   3. the entity's PhaseShift float parameters with the same names (smoothed when smoothing is on).
/// The result is skinned into `skinned`, in rig vertex order.
struct Rig2DPlayer {
    char rigPath[260]  = {};
    char clipPath[260] = {};
    f32  time    = 0.0f;
    f32  speed   = 1.0f;
    bool playing = true;

    bool rigAttempted  = false;
    bool rigValid      = false;
    bool clipAttempted = false;
    bool clipValid     = false;

    Rig2D          rig;
    ParamClip2D    clip;
    ParamValues2D  values;
    std::vector<Vec2> skinned;
};

inline void setRig2DPath(char* dest, usize capacity, const std::string& path) {
    std::memset(dest, 0, capacity);
    if (path.empty()) return;
    const size_t count = std::min(path.size(), capacity - 1);
    std::memcpy(dest, path.data(), count);
}

/// Loads the rig and its clip on first use (each file is read once). Returns true when the rig can be evaluated.
inline bool ensureRig2DLoaded(Rig2DPlayer& player) {
    if (!player.rigAttempted) {
        player.rigAttempted = true;
        player.rigValid = player.rigPath[0] != '\0' && loadRig2D(player.rigPath, player.rig);
    }
    if (!player.rigValid) return false;
    if (!player.clipAttempted) {
        player.clipAttempted = true;
        player.clipValid = player.clipPath[0] != '\0' &&
                           loadParamClip2D(player.clipPath, player.clip, player.rig);
    }
    return true;
}

inline void tickRig2DPlayers(ECS::World& world, f32 dt) {
    ECS::ComponentQuery query;
    query.with<Rig2DPlayer>();
    world.forEach<Rig2DPlayer>(query, [&world, dt](ECS::Entity entity, Rig2DPlayer& player) {
        if (!ensureRig2DLoaded(player)) {
            player.skinned.clear();
            return;
        }

        if (player.clipValid && player.playing) {
            player.time += dt * player.speed;
            if (!player.clip.loop && player.clip.duration > 0.0f && player.time >= player.clip.duration) {
                player.time = player.clip.duration;
                player.playing = false;
            }
        }

        player.values = defaultParameters2D(player.rig);
        if (player.clipValid) {
            sampleParamClip2D(player.clip, player.time, player.values);
        }

        if (const PhaseShift* phaseShift = world.get<PhaseShift>(entity)) {
            for (usize i = 0; i < player.rig.parameters.size(); ++i) {
                const Parameter2D& p = player.rig.parameters[i];
                const PhaseShiftParameter* source = phaseShift->findParameter(p.name);
                if (source && source->type == ParameterType::Float) {
                    player.values[i] = phaseShift->getSmoothedFloat(p.name.cStr());
                }
            }
        }

        player.skinned = skinMesh2D(player.rig, player.values);
    });
}

/// Submits the player's skinned mesh to a renderer exposing
/// `submitMesh2D(transform, positions, uvs, indices, uvRect, tint)` (see BatchRenderer).
/// Does nothing before the first tick has produced a skinned mesh.
template <typename Renderer>
void submitRig2DMesh(Renderer& renderer, const Rig2DPlayer& player, const Mat4& transform, Vec4 uvRect,
                     u32 tint = 0xFFFFFFFF) {
    if (player.rig.vertices.empty() || player.skinned.size() != player.rig.vertices.size()) return;
    std::vector<Vec2> uvs;
    uvs.reserve(player.rig.vertices.size());
    for (const Vertex2D& v : player.rig.vertices) uvs.push_back(v.uv);
    renderer.submitMesh2D(transform, player.skinned, uvs, player.rig.indices, uvRect, tint);
}

}  // namespace Caffeine::Animation
