#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Effects {

/// World effects sit on an entity. Camera effects follow the viewer but are not the post stack.
enum class EffectSpace : u8 { World = 0, Camera = 1 };

/// TwoD stays on the sprite plane. ThreeD billboards in the scene. Both does each in its view.
enum class EffectDomain : u8 { Both = 0, TwoD = 1, ThreeD = 2 };

enum class EffectKind : u8 {
    Particles = 0,
    VolumetricLight = 1,
    ReflectiveSurface = 2,
    MaterialShade = 3,
    Fog = 4
};

/// Look of a fog volume. Dust, mist and smoke share the fog simulator.
enum class EffectFogStyle : u8 { Fog = 0, Dust = 1, Mist = 2, Smoke = 3 };

/// Volumetric light format. Shafts travel along the entity forward (-Z).
enum class VolumetricShape : u8 {
    Sphere = 0,
    Cone = 1,
    Box = 2,
    Cylinder = 3,
    Window = 4
};

/// Performance spends fewer reflection steps and a tighter roughness gate.
enum class EffectQuality : u8 { Performance = 0, Balanced = 1, Quality = 2 };

struct EffectComponent {
    u8 enabled = 1;
    u8 space = static_cast<u8>(EffectSpace::World);
    u8 domain = static_cast<u8>(EffectDomain::Both);
    u8 kind = static_cast<u8>(EffectKind::Particles);
    u8 quality = static_cast<u8>(EffectQuality::Balanced);
    /// 0 inherit the scene mode, 1 probe, 2 planar, 3 screen-space.
    u8 reflectMode = 0;
    /// Fog style when kind is Fog. See EffectFogStyle.
    /// Volumetric lights set this to 1 after the volume mesh has been created.
    u8 pad0 = 0;
    /// Volumetric light format when kind is VolumetricLight. See VolumetricShape.
    u8 pad1 = 0;
    char materialPath[260] = {};

    i32 maxParticles = 128;
    f32 rate = 24.0f;
    f32 lifetime = 1.4f;
    Vec3 velocityMin{-0.35f, 0.2f, -0.35f};
    Vec3 velocityMax{0.35f, 1.6f, 0.35f};
    Vec3 gravity{0.0f, -1.4f, 0.0f};
    f32 drag = 0.2f;
    f32 startSize = 0.16f;
    f32 endSize = 0.02f;
    Vec4 startColor{1.0f, 0.72f, 0.28f, 1.0f};
    Vec4 endColor{0.12f, 0.1f, 0.08f, 0.0f};
    u32 seed = 1;
    f32 emitCarry = 0.0f;

    f32 density = 0.18f;
    f32 anisotropy = 0.35f;
    f32 radius = 3.0f;
    Vec3 lightColor{1.0f, 0.93f, 0.78f};
    f32 intensity = 2.5f;

    f32 reflection = 0.0f;
    f32 roughness = 0.12f;
    f32 metallic = 0.85f;
    Vec3 tint{1.0f, 1.0f, 1.0f};
    f32 emissionBoost = 0.0f;
    f32 roughnessBias = 0.0f;
};

struct SimParticle {
    Vec3 position;
    Vec3 velocity;
    f32 life = 0.0f;
    f32 maxLife = 1.0f;
    f32 size = 0.1f;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

inline u32 reflectionSteps(EffectQuality quality) {
    switch (quality) {
        case EffectQuality::Performance: return 8;
        case EffectQuality::Quality: return 48;
        case EffectQuality::Balanced: break;
    }
    return 24;
}

inline u32 reflectionSteps(u8 budget) {
    if (budget <= 1) return reflectionSteps(EffectQuality::Performance);
    if (budget >= 3) return reflectionSteps(EffectQuality::Quality);
    return reflectionSteps(EffectQuality::Balanced);
}

inline f32 reflectionRoughnessGate(EffectQuality quality) {
    switch (quality) {
        case EffectQuality::Performance: return 0.35f;
        case EffectQuality::Quality: return 0.75f;
        case EffectQuality::Balanced: break;
    }
    return 0.55f;
}

inline void configureParticle(EffectComponent& effect, EffectDomain domain) {
    effect.enabled = 1;
    effect.kind = static_cast<u8>(EffectKind::Particles);
    effect.domain = static_cast<u8>(domain);
    effect.space = static_cast<u8>(EffectSpace::World);
    if (domain == EffectDomain::TwoD) {
        effect.velocityMin = Vec3(-0.4f, 0.15f, 0.0f);
        effect.velocityMax = Vec3(0.4f, 1.1f, 0.0f);
        effect.gravity = Vec3(0.0f, -2.0f, 0.0f);
        effect.startSize = 0.12f;
        effect.endSize = 0.02f;
        effect.startColor = Vec4(1.0f, 0.86f, 0.42f, 1.0f);
        effect.endColor = Vec4(1.0f, 0.42f, 0.12f, 0.0f);
    } else {
        effect.velocityMin = Vec3(-0.35f, 0.4f, -0.35f);
        effect.velocityMax = Vec3(0.35f, 1.8f, 0.35f);
        effect.gravity = Vec3(0.0f, -1.4f, 0.0f);
        effect.startSize = 0.18f;
        effect.endSize = 0.02f;
        effect.startColor = Vec4(1.0f, 0.72f, 0.28f, 1.0f);
        effect.endColor = Vec4(0.12f, 0.1f, 0.08f, 0.0f);
    }
}

inline VolumetricShape volumetricShapeOf(const EffectComponent& effect) {
    return static_cast<VolumetricShape>(std::min<u8>(effect.pad1, 4));
}

inline void setVolumetricGrid(EffectComponent& effect, u32 columns, u32 rows) {
    columns = std::clamp(columns, 1u, 8u);
    rows = std::clamp(rows, 1u, 8u);
    effect.seed = columns | (rows << 8);
}

inline void volumetricGridOf(const EffectComponent& effect, u32& columns, u32& rows) {
    columns = std::clamp(effect.seed & 0xFFu, 1u, 8u);
    const u32 storedRows = (effect.seed >> 8) & 0xFFu;
    rows = storedRows == 0 ? 1u : std::clamp(storedRows, 1u, 8u);
}

inline const char* volumetricShapeName(VolumetricShape shape) {
    switch (shape) {
        case VolumetricShape::Cone: return "Cone";
        case VolumetricShape::Box: return "Box";
        case VolumetricShape::Cylinder: return "Cylinder";
        case VolumetricShape::Window: return "Window";
        case VolumetricShape::Sphere: break;
    }
    return "Sphere";
}

inline void configureVolumetricLight(EffectComponent& effect,
                                     VolumetricShape shape = VolumetricShape::Sphere) {
    effect.enabled = 1;
    effect.kind = static_cast<u8>(EffectKind::VolumetricLight);
    effect.domain = static_cast<u8>(EffectDomain::ThreeD);
    effect.space = static_cast<u8>(EffectSpace::World);
    effect.pad1 = static_cast<u8>(shape);
    effect.seed = 1;
    effect.lightColor = Vec3(1.0f, 0.93f, 0.78f);
    effect.intensity = 2.5f;
    effect.density = 0.22f;
    effect.anisotropy = 0.45f;
    effect.radius = 3.0f;
    effect.startSize = 0.8f;
    effect.endSize = 0.8f;
    switch (shape) {
        case VolumetricShape::Cone:
            effect.radius = 8.0f;
            effect.startSize = 0.2f;
            effect.endSize = 1.6f;
            effect.anisotropy = 0.62f;
            effect.density = 0.28f;
            break;
        case VolumetricShape::Box:
            effect.radius = 7.0f;
            effect.startSize = 1.8f;
            effect.endSize = 2.6f;
            effect.anisotropy = 0.7f;
            effect.density = 0.24f;
            effect.intensity = 3.0f;
            break;
        case VolumetricShape::Cylinder:
            effect.radius = 6.0f;
            effect.startSize = 0.7f;
            effect.endSize = 0.7f;
            effect.anisotropy = 0.55f;
            break;
        case VolumetricShape::Window:
            effect.radius = 9.0f;
            effect.startSize = 1.8f;
            effect.endSize = 3.4f;
            setVolumetricGrid(effect, 4, 6);
            effect.anisotropy = 0.75f;
            effect.density = 0.32f;
            effect.intensity = 3.4f;
            effect.lightColor = Vec3(1.0f, 0.90f, 0.70f);
            break;
        case VolumetricShape::Sphere:
            break;
    }
}

inline void configureFog(EffectComponent& effect, EffectDomain domain, EffectFogStyle style) {
    effect.enabled = 1;
    effect.kind = static_cast<u8>(EffectKind::Fog);
    effect.domain = static_cast<u8>(domain);
    effect.space = static_cast<u8>(EffectSpace::World);
    effect.pad0 = static_cast<u8>(style);
    effect.anisotropy = 0.08f;
    switch (style) {
        case EffectFogStyle::Dust:
            effect.density = 0.28f;
            effect.radius = domain == EffectDomain::TwoD ? 1.4f : 2.2f;
            effect.lightColor = Vec3(0.62f, 0.50f, 0.34f);
            effect.intensity = 0.7f;
            effect.anisotropy = 0.2f;
            break;
        case EffectFogStyle::Mist:
            effect.density = 0.08f;
            effect.radius = domain == EffectDomain::TwoD ? 3.5f : 8.0f;
            effect.lightColor = Vec3(0.84f, 0.88f, 0.92f);
            effect.intensity = 0.45f;
            break;
        case EffectFogStyle::Smoke:
            effect.density = 0.4f;
            effect.radius = domain == EffectDomain::TwoD ? 1.1f : 2.0f;
            effect.lightColor = Vec3(0.22f, 0.22f, 0.24f);
            effect.intensity = 0.85f;
            effect.anisotropy = 0.45f;
            break;
        case EffectFogStyle::Fog:
            effect.density = 0.14f;
            effect.radius = domain == EffectDomain::TwoD ? 2.8f : 6.0f;
            effect.lightColor = Vec3(0.72f, 0.78f, 0.84f);
            effect.intensity = 0.55f;
            break;
    }
}

inline f32 henyeyGreenstein(f32 cosTheta, f32 anisotropy) {
    const f32 g = std::clamp(anisotropy, -0.9f, 0.9f);
    const f32 g2 = g * g;
    const f32 denom = std::max(1.0f + g2 - 2.0f * g * cosTheta, 1.0e-4f);
    return (1.0f - g2) / (4.0f * 3.14159265f * std::pow(denom, 1.5f));
}

/// One scattering sample inside a spherical volume. Zero outside the radius.
inline f32 volumetricScatter(const Vec3& point, const Vec3& lightPos, const Vec3& viewDir,
                             f32 radius, f32 density, f32 anisotropy, f32 intensity) {
    const Vec3 toLight = lightPos - point;
    const f32 dist = toLight.length();
    if (dist > std::max(radius, 0.0f) || dist < 1.0e-4f || density <= 0.0f) return 0.0f;
    const f32 falloff = 1.0f - dist / radius;
    const Vec3 lightDir = toLight / dist;
    const f32 phase = henyeyGreenstein(viewDir.normalized().dot(lightDir), anisotropy);
    const f32 transmittance = std::exp(-density * dist);
    return std::max(intensity, 0.0f) * density * falloff * falloff * phase * transmittance;
}

}  // namespace Caffeine::Effects
