#pragma once
#include "core/Types.hpp"

namespace Caffeine::Editor {
using namespace Caffeine;

struct FrameStats {
    f64 deltaTime   = 0.0;
    f64 fps         = 0.0;
    u64 frameCount  = 0;
    f64 elapsedTime = 0.0;
};

}  // namespace Caffeine::Editor
