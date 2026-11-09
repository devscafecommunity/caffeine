#pragma once
#include "core/Types.hpp"
#include <string>

namespace Caffeine::ECS {
using namespace Caffeine;

struct MeshRendererComponent {
    std::string meshPath;
    std::string materialPath;
    bool castShadows = true;
    bool receiveShadows = true;
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
    f32           shininess   = 32.0f;
};

}  // namespace Caffeine::ECS
