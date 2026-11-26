#pragma once
#include "core/Types.hpp"
#include <string>

namespace Caffeine::ECS {
using namespace Caffeine;

/// One entity drawing a whole instance bake (`.cinst`, World Composer): every instance of the bake, its models
/// taken from a models file (see world/WorldInstanceDraw.hpp for its format). Instance positions are metres from
/// the terrain corner and the entity's transform applies to them all, so the entity sits at the terrain's corner.
/// Reflection name "World Instances".
struct WorldInstancesComponent {
    std::string bakePath;    ///< `Bake`: the `.cinst`, resolved like a Mesh Filter's `Mesh`
    std::string modelsPath;  ///< `Models`: the models text file, resolved the same way
    f32 drawDistance = 0.0f; ///< `Draw distance`: metres; chunks farther away are skipped; 0 = unlimited
    bool castShadows = true; ///< `Cast shadows`: cutout parts never cast
};

}  // namespace Caffeine::ECS
