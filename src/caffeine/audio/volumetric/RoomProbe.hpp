#pragma once

// Reads the room a point is in by shooting rays round it: how big it is, how open to the sky, how reflective its walls
// are, and where far surfaces would send back an echo. The mixer turns the answer into a reverb (its tail, its level)
// and a few discrete echoes, so a shot sounds different in a bathroom, a hall and an open field.

#include "audio/volumetric/AcousticWorld.hpp"

#include <array>

namespace Caffeine::Audio {

struct EchoTap {
    f32 delay = 0.0f;  ///< seconds after the direct sound
    f32 gain = 0.0f;
};

struct RoomEstimate {
    f32 openness = 1.0f;          ///< 0 shut in .. 1 nothing in reach (an open field)
    f32 skyExposure = 1.0f;       ///< share of the upward rays that reach the sky: 0 under a roof
    f32 meanDistance = 100.0f;    ///< metres to the surfaces round the point
    f32 reflectivity = 0.0f;      ///< mean reflectivity of what the rays hit
    f32 rt60 = 0.0f;              ///< seconds the reverb tail takes to fall 60 dB
    f32 wet = 0.0f;               ///< 0..1 how much reverb the room adds
    f32 damping = 0.5f;           ///< 0 bright tail .. 1 dull tail
    std::array<EchoTap, 4> echoes{};
    u32 echoCount = 0;
};

constexpr u32 kProbeRays = 32;
constexpr f32 kProbeReach = 300.0f;

/// Rays from the point: a ring round the horizon, a ring tilted up and the zenith, on a fixed pattern so the answer
/// does not shimmer from frame to frame.
inline const std::array<Vec3, kProbeRays>& probeDirections() {
    static const std::array<Vec3, kProbeRays> dirs = [] {
        std::array<Vec3, kProbeRays> out{};
        u32 n = 0;
        for (u32 i = 0; i < 12; ++i) {  // the horizon ring
            const f32 a = 6.28318530718f * static_cast<f32>(i) / 12.0f;
            out[n++] = Vec3(std::cos(a), 0.0f, std::sin(a));
        }
        for (u32 i = 0; i < 10; ++i) {  // 40 degrees up
            const f32 a = 6.28318530718f * (static_cast<f32>(i) + 0.5f) / 10.0f;
            out[n++] = Vec3(std::cos(a) * 0.766f, 0.643f, std::sin(a) * 0.766f);
        }
        for (u32 i = 0; i < 8; ++i) {  // 70 degrees up
            const f32 a = 6.28318530718f * static_cast<f32>(i) / 8.0f;
            out[n++] = Vec3(std::cos(a) * 0.342f, 0.94f, std::sin(a) * 0.342f);
        }
        out[n++] = Vec3(0.0f, 1.0f, 0.0f);
        out[n++] = Vec3(0.0f, 0.0f, 0.0f);  // unused slot, skipped below
        return out;
    }();
    return dirs;
}

inline RoomEstimate probeRoom(const AcousticWorld& world, const Vec3& point) {
    RoomEstimate room;
    const auto& dirs = probeDirections();
    u32 escaped = 0, rays = 0, upward = 0, upwardEscaped = 0;
    f32 distanceSum = 0.0f, reflectSum = 0.0f;
    u32 hits = 0;
    struct Far {
        f32 distance;
        f32 reflectivity;
    };
    std::array<Far, kProbeRays> far{};
    u32 farCount = 0;
    for (const Vec3& d : dirs) {
        if (d.lengthSquared() < 0.5f) continue;
        const RayHit hit = world.raycast(point, d, kProbeReach);
        ++rays;
        const bool up = d.y > 0.3f;
        if (up) ++upward;
        if (!hit.hit) {
            ++escaped;
            if (up) ++upwardEscaped;
            continue;
        }
        ++hits;
        distanceSum += hit.distance;
        reflectSum += hit.reflectivity;
        if (hit.distance > 25.0f && d.y < 0.5f) far[farCount++] = {hit.distance, hit.reflectivity};
    }
    room.openness = rays ? static_cast<f32>(escaped) / static_cast<f32>(rays) : 1.0f;
    room.skyExposure = upward ? static_cast<f32>(upwardEscaped) / static_cast<f32>(upward) : 1.0f;
    room.meanDistance = hits ? distanceSum / static_cast<f32>(hits) : kProbeReach;
    room.reflectivity = hits ? reflectSum / static_cast<f32>(hits) : 0.0f;
    // Sabine, with the mean free path taken as twice the mean wall distance and the absorption from the walls.
    const f32 absorption = std::clamp(1.0f - room.reflectivity, 0.06f, 0.9f);
    const f32 enclosed = 1.0f - room.openness;
    room.rt60 = std::clamp(0.08f * room.meanDistance / absorption, 0.15f, 6.0f) * (0.25f + 0.75f * enclosed);
    // Little reverb outdoors, lots in a shut room; hard walls make more of it.
    room.wet = std::clamp(enclosed * enclosed * (0.35f + 0.65f * room.reflectivity), 0.0f, 1.0f);
    room.damping = std::clamp(1.0f - room.reflectivity, 0.1f, 0.9f);
    // Echoes: the nearest of the far, well-reflecting surfaces send a distinct repeat 2d/c later (a wall met
    // obliquely is farther along the ray than it is from the point, so the nearest hit is the truest).
    std::sort(far.begin(), far.begin() + farCount, [](const Far& a, const Far& b) { return a.distance < b.distance; });
    for (u32 i = 0; i < farCount && room.echoCount < room.echoes.size(); ++i) {
        const f32 spread = 1.0f / (1.0f + far[i].distance / 40.0f);
        bool distinct = true;  // two walls at nearly the same distance are one echo
        for (u32 k = 0; k < room.echoCount; ++k)
            if (std::fabs(room.echoes[k].delay - 2.0f * far[i].distance / 343.0f) < 0.05f) distinct = false;
        if (!distinct) continue;
        room.echoes[room.echoCount++] = {2.0f * far[i].distance / 343.0f, far[i].reflectivity * spread * 0.9f};
    }
    return room;
}

}  // namespace Caffeine::Audio
