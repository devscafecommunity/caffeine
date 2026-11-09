#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

namespace Caffeine::ECS {
using namespace Caffeine;

struct Position3D { Vec3 position; };
struct Rotation3D  { Vec4 quaternion = Vec4(0.0f, 0.0f, 0.0f, 1.0f); };
struct Scale3D     { Vec3 scale = Vec3(1.0f, 1.0f, 1.0f); };

}  // namespace Caffeine::ECS
