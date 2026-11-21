#pragma once
#include "core/Types.hpp"
#include "math/Vec4.hpp"
#include <string>
#include <vector>

namespace Caffeine::ECS {
using namespace Caffeine;

struct MeshRendererComponent {
    std::string meshPath;
    std::string materialPath;
    bool castShadows = true;
    bool receiveShadows = true;
    /// Run-time material modifiers, changeable from scripts (fields Tint, Wetness, Snow, Age).
    /// They combine with the material's own: tint multiplies, the amounts add and clamp to 0..1.
    Vec4 modTint = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    f32  modWetness = 0.0f;
    f32  modSnow = 0.0f;
    f32  modAge = 0.0f;
    /// Micromesh: draw the mesh as a cluster hierarchy cut on the GPU every frame (see
    /// doppio/docs/micromeshes.md). Needs one material and no vertex colours; other meshes draw normally.
    bool micromesh = false;
    /// The error the cut may have, in screen pixels at the object's distance.
    f32  micromeshPixels = 1.5f;
    /// Micromesh occlusion: also hide the clusters that last frame's depth buffer shows to be behind something.
    /// Single-phase (last frame's picture): a cluster at a new silhouette edge can show one frame late.
    bool micromeshOcclusion = false;
};

struct SkinnedMeshRendererComponent {
    std::string meshPath;
    std::string materialPath;
    std::string skeletonPath;
    bool castShadows = true;
    bool receiveShadows = true;
};

enum class MeshPrimitive : u8 {
    Custom,
    Cube,
    Sphere,
    Capsule,
    Cylinder,
    Plane,
    Cone,
    Pyramid,
    Torus,
};

/// Curved primitives use smooth vertex normals; flat primitives keep per-face shading.
inline bool meshPrimitiveUsesSmoothShading(MeshPrimitive primitive) {
    switch (primitive) {
        case MeshPrimitive::Sphere:
        case MeshPrimitive::Capsule:
        case MeshPrimitive::Cylinder:
        case MeshPrimitive::Cone:
        case MeshPrimitive::Torus:
            return true;
        case MeshPrimitive::Custom:
        case MeshPrimitive::Cube:
        case MeshPrimitive::Plane:
        case MeshPrimitive::Pyramid:
        default:
            return false;
    }
}

struct MeshFilterComponent {
    MeshPrimitive primitive   = MeshPrimitive::Cube;
    std::string   customMeshPath;
    std::string   customTexturePath;
    std::string   customNormalPath;
    std::string   customMaterialPath;
    /// Material slots per sub-mesh: `a.mat;b.mat;;c.mat`, slot N is sub-mesh N (an OBJ `usemtl` group, a glTF
    /// primitive). An empty slot, and every sub-mesh past the list, uses `customMaterialPath`. Empty = one
    /// material for the whole mesh.
    std::string   slotMaterials;
    f32           shininess   = 32.0f;
};

/// Splits `MeshFilterComponent::slotMaterials` at `;` (entries trimmed, empty ones kept so slots keep their place).
inline std::vector<std::string> splitMaterialSlots(const std::string& text) {
    std::vector<std::string> slots;
    if (text.empty()) return slots;
    usize start = 0;
    while (true) {
        const usize end = text.find(';', start);
        std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const usize first = item.find_first_not_of(" \t");
        const usize last = item.find_last_not_of(" \t");
        slots.push_back(first == std::string::npos ? std::string() : item.substr(first, last - first + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return slots;
}

}  // namespace Caffeine::ECS
