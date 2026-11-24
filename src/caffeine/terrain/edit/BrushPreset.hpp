#pragma once

// A brush as a document (terrain editing roadmap, phase 1): everything a stroke needs besides the path, with a name and a
// stamp that is either a procedural one of the library (`BrushStamps`) or a picture of the project.
//
// `.brush` is a small line-based text file, like `.mat` and the biome table (the core has no JSON reader, and a brush is a
// dozen numbers). One `key value...` per line, `#` starts a comment, unknown keys are skipped (a newer file still opens):
//
//     # caffeine brush 1
//     name Rock strata
//     category Rock
//     mode raise              raise lower smooth flatten noise ramp terrace pinch bulge stamp_absolute stamp_max
//                             stamp_min clone smudge erode_thermal erode_hydraulic
//     radius 14               metres
//     strength 1.2            metres per stamp (or a 0..1 share, by mode)
//     spacing 0               metres between stamps; 0 is a quarter of the radius
//     falloff smooth          smooth linear sharp constant
//     rotation 0              degrees
//     target_height -1        metres (flatten, ramp); negative: the ground under the first stamp
//     seed 0
//     stamp procedural strata 128      or: stamp image Textures/crack.png 128   or: stamp none
//     signed 0
//     step 2                  terrace step, metres
//     talus 35                degrees; iterations 4; droplets 24
//     offset 0 0              clone source, metres
//     jitter 0 0 0 0          position (of the radius), rotation (degrees), size, opacity
//     symmetry none 4 0 0     none mirror_x mirror_z mirror_both radial, radial count, centre x z
//     pressure 0              how much the pen's pressure scales the strength
//     protect slope 0 0 0.2 0.3        mask rise0 rise1 fall1 fall0 (repeat the line for more terms)

#include "core/Types.hpp"
#include "terrain/edit/TerrainStroke.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Caffeine::Terrain {

enum class StampSource : u8 { None, Procedural, Image };

struct BrushPreset {
    std::string name = "Brush";
    std::string category = "Custom";
    StampSource source = StampSource::None;
    /// The procedural stamp's id, or the project path of the picture.
    std::string stamp;
    u32 stampSize = 128;
    /// Every number of the brush; `brush.stamp` is filled by `resolveBrushStamp`, not stored.
    StrokeBrush brush;
};

/// The `.brush` text of a preset.
std::string brushToText(const BrushPreset& preset);
/// Parses a `.brush`. On a malformed line returns false and says which in `error`; unknown keys are skipped.
bool brushFromText(const std::string& text, BrushPreset& out, std::string* error = nullptr);

/// Makes the preset's stamp image: a procedural stamp is generated, a picture is loaded through `absolutePath` (which
/// turns the project path into a file; the asset manager does that in the engine) and converted to grey, then resampled
/// to `stampSize`. `brush.stamp` is empty when the preset has none. False (and `error`) when the stamp cannot be made.
bool resolveBrushStamp(BrushPreset& preset, const std::function<std::string(const std::string&)>& absolutePath,
                       std::string* error = nullptr);

/// The brushes the engine ships: one for every procedural stamp, and the tools without a stamp (smooth, flatten, noise,
/// terrace, ramp, pinch, bulge, clone, smudge, the erosions...), in a stable order.
const std::vector<BrushPreset>& builtinBrushes();

const char* brushModeName(TerrainBrushMode mode);
bool brushModeFromName(const std::string& name, TerrainBrushMode& out);

}  // namespace Caffeine::Terrain
