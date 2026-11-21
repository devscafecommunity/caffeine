#pragma once

// The biome table (photoreal roadmap, phase 1): a small rule list that turns terrain masks into the 0..1 density of
// each scatter class at a point. "Trees where it is gentle, dry and above the beach, grass everywhere but on rock,
// rocks on steep ground and beside water" are three rules, written as data, not code.
//
// A class density is `base * product of its terms`; a term reads one mask and is a trapezoid:
//
//        1 |        ______
//          |       /      \
//        0 |______/        \_____      below `rise0` 0, up to 1 at `rise1`, 1 until `fall1`, down to 0 at `fall0`
//            rise0 rise1  fall1 fall0
//
// A table is plain text, one class per `class` line and its terms below it (`#` starts a comment):
//
//     class 2 base 0.9            # trees
//       slope     0.00 0.00 0.15 0.35
//       wetness   0.00 0.00 0.30 0.60
//       height    0.22 0.28 1.00 1.00
//
// `densityFunction` is the callback `Scatter::ScatterField::density` wants. A class without a rule has density 1;
// a term that names an unknown mask gives 0 (nothing is placed there rather than everywhere), and `missingMasks`
// lists those names so an editor can say so.

#include "core/Types.hpp"
#include "terrain/TerrainMasks.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Caffeine::Terrain {

struct BiomeTerm {
    std::string mask;
    f32 rise0 = 0.0f;
    f32 rise1 = 0.0f;
    f32 fall1 = 1.0f;
    f32 fall0 = 1.0f;

    /// The trapezoid at a mask value, 0..1.
    f32 weight(f32 value) const;
};

struct BiomeRule {
    u32 classId = 0;
    f32 base = 1.0f;
    std::vector<BiomeTerm> terms;
};

class BiomeTable {
public:
    /// Adds a rule; a second rule for the same class replaces the first.
    void set(const BiomeRule& rule);
    bool remove(u32 classId);
    const std::vector<BiomeRule>& rules() const { return m_rules; }
    const BiomeRule* find(u32 classId) const;

    /// The density of `classId` at (x, z) metres from the terrain's corner.
    f32 density(const TerrainMasks& masks, const TerrainFrame& frame, u32 classId, f32 x, f32 z) const;
    /// For `Scatter::ScatterField::density`. The table, the masks and the frame must outlive the function.
    std::function<f32(u32, f32, f32)> densityFunction(const TerrainMasks& masks, const TerrainFrame& frame) const;

    /// Mask names the rules use that `masks` does not have (each once, sorted).
    std::vector<std::string> missingMasks(const TerrainMasks& masks) const;

    std::string toText() const;
    /// Parses the text format; on a syntax error returns false and says which line in `error`.
    static bool fromText(const std::string& text, BiomeTable& out, std::string* error = nullptr);

private:
    std::vector<BiomeRule> m_rules;
};

}  // namespace Caffeine::Terrain
