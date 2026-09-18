#pragma once

#include "math/Vec4.hpp"

namespace Caffeine::Assets {

struct MaterialSurface {
    Vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
    f32  metallic = 0.0f;
    f32  roughness = 0.5f;
    bool valid = false;
};

}  // namespace Caffeine::Assets
