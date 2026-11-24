#pragma once

// The procedural stamp library of terrain editing (terrain editing roadmap, phase 1): about forty grey-scale shapes a brush
// can press into the ground: round and square tips, noises, rock strata and cliffs, dunes, craters, terraces, mounds and
// riverbeds. Each is a pure function of the position in the stamp, the seed and the size, made of arithmetic only (no
// trigonometry, no libm calls that may differ in the last digit), so a stamp is the same on every machine.
//
// A stamp is a square image 0..1 centred on the brush and spanning its diameter (`BrushStamp`). A *signed* stamp (a
// crater: a bowl inside a rim; a valley; a riverbed) uses 0.5 for "no change", above for raising and below for lowering.

#include "core/Types.hpp"
#include "terrain/edit/TerrainStroke.hpp"

#include <string>
#include <vector>

namespace Caffeine::Terrain {

/// One library entry and what a brush made from it starts with.
struct StampInfo {
    const char* id;        // stable: `.brush` files and agents name it
    const char* name;      // for menus
    const char* category;  // Basic, Noise, Rock, Dunes, Craters, Terraces, Shapes
    bool signedStamp;      // 0.5 is neutral
    TerrainBrushMode mode; // the mode a brush of it starts in
    f32 radiusM;
    f32 strength;          // metres per stamp (Raise, Lower, Noise) or 0..1 (Smooth, Flatten)
    f32 spacingRatio;      // of the radius
};

/// Every procedural stamp, in a stable order.
const std::vector<StampInfo>& proceduralStamps();
const StampInfo* findProceduralStamp(const std::string& id);

/// Fills `out` with the stamp `id` at `size` x `size` (4 to 1024). `seed` varies the noise-based ones. False for an
/// unknown id or a bad size.
bool generateProceduralStamp(const std::string& id, u32 size, u64 seed, BrushStamp& out);

}  // namespace Caffeine::Terrain
