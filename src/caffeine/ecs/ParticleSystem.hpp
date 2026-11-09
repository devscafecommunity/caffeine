#pragma once
#include "core/Types.hpp"
#include "ecs/ISystem.hpp"
#include "containers/Vector.hpp"

namespace Caffeine::ECS {

class ParticleSystem : public ISystem {
public:
    void onUpdate(World& world, f32 dt) override;
};

}