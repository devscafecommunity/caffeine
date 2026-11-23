#pragma once

// 3D spatial audio as pure functions: where a sound is relative to the listener becomes a gain, a pan and a delay.
// No device and no SDL, so every rule is a unit test. AudioService applies the result to a playing source.

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Audio {

inline constexpr f32 kSpeedOfSoundMps = 343.0f;

/// Where the player's ears are: a position and the way they face (right-handed, +Y up: right = forward x up).
struct Listener3D {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

enum class Rolloff : u8 {
    Inverse,  ///< gain = ref / (ref + k * (d - ref)): half the gain for every doubling of the distance (the default)
    Linear,   ///< falls straight to zero at the maximum distance
};

struct SpatialParams {
    f32 gain = 1.0f;      ///< 0..1
    f32 pan = 0.0f;       ///< -1 (left) .. 1 (right)
    f32 distance = 0.0f;  ///< metres
    f32 delay = 0.0f;     ///< seconds the sound takes to arrive
};

/// The gain a source at `distance` metres has: 1 up to `refDistance`, then it falls by `rolloff` and reaches 0 at
/// `maxDistance` (a short fade over the last fifth, so a sound never cuts off).
inline f32 distanceGain(f32 distance, f32 refDistance, f32 maxDistance, Rolloff mode = Rolloff::Inverse,
                        f32 rolloff = 1.0f) {
    const f32 ref = std::max(refDistance, 0.01f);
    const f32 maxD = std::max(maxDistance, ref + 0.01f);
    if (distance <= ref) return 1.0f;
    if (distance >= maxD) return 0.0f;
    f32 gain;
    if (mode == Rolloff::Linear) {
        gain = 1.0f - (distance - ref) / (maxD - ref);
    } else {
        gain = ref / (ref + std::max(rolloff, 0.0f) * (distance - ref));
    }
    const f32 fadeStart = maxD - 0.2f * (maxD - ref);
    if (distance > fadeStart) gain *= (maxD - distance) / (maxD - fadeStart);
    return std::clamp(gain, 0.0f, 1.0f);
}

/// A source at `source` as the listener hears it.
inline SpatialParams spatialize(const Listener3D& listener, const Vec3& source, f32 refDistance, f32 maxDistance,
                                Rolloff mode = Rolloff::Inverse, f32 rolloff = 1.0f) {
    SpatialParams out;
    const Vec3 toSource = source - listener.position;
    out.distance = toSource.length();
    out.gain = distanceGain(out.distance, refDistance, maxDistance, mode, rolloff);
    out.delay = out.distance / kSpeedOfSoundMps;
    const Vec3 forward = listener.forward.normalized();
    const Vec3 right = forward.cross(listener.up.normalized()).normalized();
    if (out.distance > 1e-4f) {
        const f32 side = toSource.dot(right) / out.distance;
        // Right on top of the listener a sound has no side: the pan opens up over the reference distance.
        const f32 open = std::clamp(out.distance / std::max(refDistance, 0.01f), 0.0f, 1.0f);
        out.pan = std::clamp(side * open, -1.0f, 1.0f);
    }
    return out;
}

}  // namespace Caffeine::Audio
