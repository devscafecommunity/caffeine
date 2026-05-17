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

}  // namespace Caffeine::ECS
