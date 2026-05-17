#pragma once
#include "core/Types.hpp"
#include <string>

namespace Caffeine::ECS {
using namespace Caffeine;

struct MeshRenderer {
    std::string meshPath;
    std::string materialPath;
    bool castShadows = true;
    bool receiveShadows = true;
};

struct SkinnedMeshRenderer {
    std::string meshPath;
    std::string materialPath;
    std::string skeletonPath;
    bool castShadows = true;
    bool receiveShadows = true;
};

}  // namespace Caffeine::ECS
