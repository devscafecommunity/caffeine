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
    Plane
};

struct MeshFilterComponent {
    MeshPrimitive primitive   = MeshPrimitive::Cube;
    std::string   customMeshPath;
};

}  // namespace Caffeine::ECS
