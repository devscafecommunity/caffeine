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

struct Transform {
    Vec3 position = {0.0f, 0.0f, 0.0f};
    Vec3 rotation = {0.0f, 0.0f, 0.0f};
    Vec3 scale    = {1.0f, 1.0f, 1.0f};
};

}  // namespace Caffeine::ECS

#include "ecs/Components3D.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
