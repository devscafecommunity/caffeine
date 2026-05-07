/**
 * @file Components.hpp
 * @brief Predefined game components for rapid 2D game development
 * @copyright Copyright (c) 2025 Caffeine Engine
 */

#pragma once

#include "core/Types.hpp"
#include <string>

namespace Caffeine::ECS {

struct Position2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Velocity2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Acceleration2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Rotation {
    f32 angle = 0.0f;
};

struct Scale2D {
    f32 x = 1.0f;
    f32 y = 1.0f;
};

struct Sprite {
    std::string name;
    u32 frameIndex = 0;
};

struct Health {
    u32 current = 100;
    u32 max = 100;
};

struct Tag { };

}
