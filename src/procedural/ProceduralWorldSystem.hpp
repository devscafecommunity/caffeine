#pragma once

#include "ecs/World.hpp"

namespace Caffeine::Procedural {

class ProceduralWorldSystem {
public:
    static void update(ECS::World& world);
    static void reset();
};

}  // namespace Caffeine::Procedural
