#pragma once

#include "ecs/PostProcessComponents.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace Caffeine::ECS {

/// Reflection table over PostProcessComponent, addressed as "effect.field"
/// (e.g. "bloom.intensity", "antiAliasing.mode"). Shared by the Lua API, serialisation
/// helpers and tools so every tunable is reachable without per-field glue.
struct PostProcessField {
    enum class Kind : u8 { Bool, Float, UInt };
    std::string_view effect;
    std::string_view name;
    Kind kind;
    std::size_t offset;
    f32 minValue;
    f32 maxValue;
};

#define CF_PP_FIELD(effect, field, kind, lo, hi)                                                   \
    PostProcessField {                                                                             \
        #effect, #field, PostProcessField::Kind::kind, offsetof(PostProcessComponent, effect.field), \
            lo, hi                                                                                 \
    }

inline std::span<const PostProcessField> postProcessFields() {
    static const PostProcessField kFields[] = {
        CF_PP_FIELD(ambientOcclusion, enabled, Bool, 0, 1),
        CF_PP_FIELD(ambientOcclusion, intensity, Float, 0, 4),
        CF_PP_FIELD(ambientOcclusion, radius, Float, 0.01f, 8),
        CF_PP_FIELD(ambientOcclusion, bias, Float, 0, 0.5f),
        CF_PP_FIELD(antiAliasing, enabled, Bool, 0, 1),
        CF_PP_FIELD(antiAliasing, mode, UInt, 0, 1),
        CF_PP_FIELD(antiAliasing, sharpness, Float, 0, 1),
        CF_PP_FIELD(autoExposure, enabled, Bool, 0, 1),
        CF_PP_FIELD(autoExposure, minExposure, Float, 0.01f, 16),
        CF_PP_FIELD(autoExposure, maxExposure, Float, 0.01f, 64),
        CF_PP_FIELD(autoExposure, adaptationSpeed, Float, 0.01f, 20),
        CF_PP_FIELD(autoExposure, compensation, Float, -8, 8),
        CF_PP_FIELD(bloom, enabled, Bool, 0, 1),
        CF_PP_FIELD(bloom, intensity, Float, 0, 4),
        CF_PP_FIELD(bloom, threshold, Float, 0, 16),
        CF_PP_FIELD(bloom, scatter, Float, 0, 1),
        CF_PP_FIELD(chromaticAberration, enabled, Bool, 0, 1),
        CF_PP_FIELD(chromaticAberration, intensity, Float, 0, 1),
        CF_PP_FIELD(colorGrading, enabled, Bool, 0, 1),
        CF_PP_FIELD(colorGrading, exposure, Float, 0, 16),
        CF_PP_FIELD(colorGrading, contrast, Float, 0, 4),
        CF_PP_FIELD(colorGrading, saturation, Float, 0, 4),
        CF_PP_FIELD(colorGrading, temperature, Float, -1, 1),
        CF_PP_FIELD(colorGrading, tint, Float, -1, 1),
        CF_PP_FIELD(deferredFog, enabled, Bool, 0, 1),
        CF_PP_FIELD(deferredFog, density, Float, 0, 1),
        CF_PP_FIELD(deferredFog, start, Float, 0, 100000),
        CF_PP_FIELD(deferredFog, end, Float, 0, 100000),
        CF_PP_FIELD(deferredFog, colorR, Float, 0, 1),
        CF_PP_FIELD(deferredFog, colorG, Float, 0, 1),
        CF_PP_FIELD(deferredFog, colorB, Float, 0, 1),
        CF_PP_FIELD(depthOfField, enabled, Bool, 0, 1),
        CF_PP_FIELD(depthOfField, focusDistance, Float, 0.01f, 100000),
        CF_PP_FIELD(depthOfField, aperture, Float, 0, 4),
        CF_PP_FIELD(depthOfField, focalLength, Float, 1, 600),
        CF_PP_FIELD(depthOfField, maxBlur, Float, 0, 4),
        CF_PP_FIELD(grain, enabled, Bool, 0, 1),
        CF_PP_FIELD(grain, intensity, Float, 0, 1),
        CF_PP_FIELD(grain, size, Float, 0.5f, 8),
        CF_PP_FIELD(lensDistortion, enabled, Bool, 0, 1),
        CF_PP_FIELD(lensDistortion, intensity, Float, -1, 1),
        CF_PP_FIELD(motionBlur, enabled, Bool, 0, 1),
        CF_PP_FIELD(motionBlur, intensity, Float, 0, 1),
        CF_PP_FIELD(motionBlur, maxVelocity, Float, 0, 4),
        CF_PP_FIELD(screenSpaceReflections, enabled, Bool, 0, 1),
        CF_PP_FIELD(screenSpaceReflections, intensity, Float, 0, 1),
        CF_PP_FIELD(screenSpaceReflections, maxRoughness, Float, 0, 1),
        CF_PP_FIELD(screenSpaceReflections, maxSteps, UInt, 4, 256),
        CF_PP_FIELD(vignette, enabled, Bool, 0, 1),
        CF_PP_FIELD(vignette, intensity, Float, 0, 1),
        CF_PP_FIELD(vignette, smoothness, Float, 0, 1),
    };
    return kFields;
}

#undef CF_PP_FIELD

inline const PostProcessField* findPostProcessField(std::string_view path) {
    const std::size_t dot = path.find('.');
    if (dot == std::string_view::npos) return nullptr;
    const std::string_view effect = path.substr(0, dot);
    const std::string_view name = path.substr(dot + 1);
    for (const PostProcessField& field : postProcessFields()) {
        if (field.effect == effect && field.name == name) return &field;
    }
    return nullptr;
}

/// Short aliases accepted wherever an effect name is expected.
inline std::string_view canonicalPostProcessEffect(std::string_view effect) {
    if (effect == "ao" || effect == "ssao") return "ambientOcclusion";
    if (effect == "aa") return "antiAliasing";
    if (effect == "chromatic") return "chromaticAberration";
    if (effect == "fog") return "deferredFog";
    if (effect == "dof") return "depthOfField";
    if (effect == "ssr") return "screenSpaceReflections";
    if (effect == "exposure") return "autoExposure";
    return effect;
}

inline std::optional<f32> getPostProcessValue(const PostProcessComponent& fx, const PostProcessField& field) {
    const auto* base = reinterpret_cast<const unsigned char*>(&fx) + field.offset;
    switch (field.kind) {
        case PostProcessField::Kind::Bool: return *reinterpret_cast<const bool*>(base) ? 1.0f : 0.0f;
        case PostProcessField::Kind::Float: return *reinterpret_cast<const f32*>(base);
        case PostProcessField::Kind::UInt: return static_cast<f32>(*reinterpret_cast<const u32*>(base));
    }
    return std::nullopt;
}

inline std::optional<f32> getPostProcessValue(const PostProcessComponent& fx, std::string_view path) {
    const PostProcessField* field = findPostProcessField(path);
    if (!field) return std::nullopt;
    return getPostProcessValue(fx, *field);
}

/// Writes a value clamped to the field range. Returns false for unknown paths.
inline bool setPostProcessValue(PostProcessComponent& fx, const PostProcessField& field, f32 value) {
    auto* base = reinterpret_cast<unsigned char*>(&fx) + field.offset;
    const f32 v = value < field.minValue ? field.minValue : (value > field.maxValue ? field.maxValue : value);
    switch (field.kind) {
        case PostProcessField::Kind::Bool: *reinterpret_cast<bool*>(base) = value != 0.0f; break;
        case PostProcessField::Kind::Float: *reinterpret_cast<f32*>(base) = v; break;
        case PostProcessField::Kind::UInt: *reinterpret_cast<u32*>(base) = static_cast<u32>(v + 0.5f); break;
    }
    return true;
}

inline bool setPostProcessValue(PostProcessComponent& fx, std::string_view path, f32 value) {
    const PostProcessField* field = findPostProcessField(path);
    return field && setPostProcessValue(fx, *field, value);
}

inline bool setPostProcessEffectEnabled(PostProcessComponent& fx, std::string_view effect, bool enabled) {
    const std::string_view name = canonicalPostProcessEffect(effect);
    for (const PostProcessField& field : postProcessFields()) {
        if (field.effect == name && field.name == "enabled") {
            return setPostProcessValue(fx, field, enabled ? 1.0f : 0.0f);
        }
    }
    return false;
}

inline std::optional<bool> isPostProcessEffectEnabled(const PostProcessComponent& fx, std::string_view effect) {
    const std::string_view name = canonicalPostProcessEffect(effect);
    for (const PostProcessField& field : postProcessFields()) {
        if (field.effect == name && field.name == "enabled") return *getPostProcessValue(fx, field) != 0.0f;
    }
    return std::nullopt;
}

/// Linear blend of every float field (bools/ints snap at t >= 0.5) — for volume-style transitions.
inline PostProcessComponent lerpPostProcess(const PostProcessComponent& a, const PostProcessComponent& b, f32 t) {
    PostProcessComponent out = t < 0.5f ? a : b;
    for (const PostProcessField& field : postProcessFields()) {
        if (field.kind != PostProcessField::Kind::Float) continue;
        const f32 va = *getPostProcessValue(a, field);
        const f32 vb = *getPostProcessValue(b, field);
        setPostProcessValue(out, field, va + (vb - va) * t);
    }
    return out;
}

}  // namespace Caffeine::ECS
