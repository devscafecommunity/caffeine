#pragma once

#include "assets/MeshTypes.hpp"
#include "effects/EffectTypes.hpp"
#include "ecs/World.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

#include <vector>

namespace Caffeine::Effects {

struct EffectSprite {
    Vec3 position;
    f32 size = 0.1f;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SurfaceShade {
    Vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
    Vec3 emission{0.0f, 0.0f, 0.0f};
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
    f32 reflectance = 0.04f;
    f32 reflection = 0.0f;
    i32 ssrSteps = -1;
    f32 ssrRoughness = -1.0f;
    bool planar = false;
};

void tickEffects(ECS::World& world, f32 dt, const Vec3& cameraPos);

const std::vector<SimParticle>* particlesFor(u32 entityId);

/// Sprites for the 2D overlay. `onlyTwoD` skips billboards the 3D pass already draws.
void collectOverlayEffects(ECS::World& world, bool onlyTwoD, std::vector<EffectSprite>& out);

/// Camera-facing quads. Colour is stored in the vertex tangent.
void buildEffectMesh(ECS::World& world, const Vec3& cameraPos, const Vec3& cameraRight,
                     const Vec3& cameraUp, Assets::Mesh3D& mesh);

void applyMaterialReflection(f32 reflection, u8 budget, SurfaceShade& shade);
void applyEffectShade(const EffectComponent& effect, SurfaceShade& shade);

}  // namespace Caffeine::Effects
