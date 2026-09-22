/**
 * @file Components.hpp
 * @brief Predefined game components for rapid 2D game development
 * @copyright Copyright (c) 2025 Caffeine Engine
 */

#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include <string>
#include <vector>

namespace Caffeine::ECS {

struct Transform {
    Vec3 position = {0.0f, 0.0f, 0.0f};
    Vec3 rotation = {0.0f, 0.0f, 0.0f};
    Vec3 scale    = {1.0f, 1.0f, 1.0f};
};

struct Position2D {
    f32 x = 0.0f;
    f32 y = 0.0f;

    Position2D() = default;
    Position2D(f32 x_, f32 y_) : x(x_), y(y_) {}
};

struct Velocity2D {
    f32 x = 0.0f;
    f32 y = 0.0f;

    Velocity2D() = default;
    Velocity2D(f32 x_, f32 y_) : x(x_), y(y_) {}
};

struct Scale2D {
    f32 x = 1.0f;
    f32 y = 1.0f;

    Scale2D() = default;
    Scale2D(f32 x_, f32 y_) : x(x_), y(y_) {}
};

struct Rotation {
    f32 angle = 0.0f;

    Rotation() = default;
    explicit Rotation(f32 angle_) : angle(angle_) {}
};

struct Health {
    u32 current = 100;
    u32 max     = 100;

    Health() = default;
    Health(u32 current_, u32 max_) : current(current_), max(max_) {}
};

struct Acceleration2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Sprite {
    std::string name;
    u32 frameIndex = 0;
};

struct Tag { };

struct ParticleEmitterComponent {
    int maxParticles = 100;
    f32 emissionRate = 10.0f;
    f32 lifetime = 2.0f;

    Vec2 velocityMin = {-10.0f, -10.0f};
    Vec2 velocityMax = {10.0f, 10.0f};

    u32 startColor = 0xFFFFFFFF;
    u32 endColor = 0x00000000;

    f32 startSize = 1.0f;
    f32 endSize = 0.0f;

    struct Particle {
        Vec2 position;
        Vec2 velocity;
        f32 life;
        f32 maxLife;
        u32 color;
        f32 size;
    };

    std::vector<Particle> activeParticles;
};

struct PersistentComponent {
    bool dontDestroyOnLoad = true;
};

struct DisabledTag {};

}  // namespace Caffeine::ECS

#include "ecs/Components3D.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/SkyboxComponents.hpp"
#include "ecs/TerrainComponents.hpp"
