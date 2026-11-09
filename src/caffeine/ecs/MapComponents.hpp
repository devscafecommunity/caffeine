#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"

namespace Caffeine::ECS {

/// Generic 2D map surface: logical bounds and chunk grid (no geo/HTTP baked in).
struct MapSurface2DComponent {
    Vec2 regionMin{0.0f, 0.0f};
    Vec2 regionMax{100.0f, 100.0f};
    f32 chunkSize = 32.0f;
    /// When set, scripts may listen for chunk requests via events.
    bool enableChunkRequests = false;
};

}  // namespace Caffeine::ECS
