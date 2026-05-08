#pragma once

#include "../core/Types.hpp"
#include "../math/Vec2.hpp"
#include "../math/Vec3.hpp"

namespace Caffeine::Render {

using namespace Caffeine;

struct Rect2D {
    Vec2 position { 0.0f, 0.0f };
    Vec2 size     { 1280.0f, 720.0f };
};

struct SpriteVertex {
    Vec3 position;
    Vec2 texcoord;
    u32  tint;
};

}  // namespace Caffeine::Render
