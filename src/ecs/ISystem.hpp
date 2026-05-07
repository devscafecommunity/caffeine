#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

class World;

class ISystem {
public:
    virtual ~ISystem() = default;
    
    virtual void onUpdate(World& world, f32 deltaTime) = 0;
};

}
