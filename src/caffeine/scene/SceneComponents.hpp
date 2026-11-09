#pragma once

#include "ecs/Entity.hpp"
#include "math/Mat4.hpp"
#include "core/Types.hpp"

namespace Caffeine::Scene {

using namespace Caffeine;

struct Parent {
    ECS::Entity parent = ECS::Entity::INVALID;
    bool        dirty  = true;
};

struct WorldTransform {
    Mat4 matrix = Mat4::identity();
};

struct EntityLayer {
    u8 layer = 0;
};

}
