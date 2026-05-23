#pragma once
#include "core/Types.hpp"
#include <string>

namespace Caffeine::ECS {
using namespace Caffeine;

struct PrefabInstance {
    std::string prefabPath;
    u32 rootEntityId = 0;
};

}  // namespace Caffeine::ECS
