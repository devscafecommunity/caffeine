// ============================================================================
// @file    PhotonFarField.hpp
// @brief   Photon far field — clipmap/probe layout and CPU-side math (spec §8)
// @note    Public header (include/caffeine/photon).
//
//  P4 foundation. These functions define the single layout used by the runtime
//  and (later) the offline bake:
//    * 4 nested voxel clipmap levels: 64³ cells, c_0 = 0.25 m, extent 16·2^l m;
//    * 16³ probes per level, spacing p_l = 2^l m;
//    * 8×8 octahedral radiance tiles with a 1-texel border (10×10);
//    * RG16F distance moments (mean, mean²) and SH-L1 per probe;
//    * round-robin update U = (1, 2, 4, 4) with hysteresis h = 0.85;
//    * Chebyshev visibility and trilinear probe weights;
//    * anisotropic face radiance for voxel hits.
//
//  Keeping the math here (instead of duplicating it in GLSL) makes the layout
//  testable on CI without a GPU and gives the future bake the same contract.
//
//  OPEN ITEMS (P4 completion, tracked in PhotonCascades.hpp as well):
//   * conservative triangle voxelisation + dirty-slab rebuilds;
//   * E_dir from the host lights/shadows in the injection pass;
//   * S4/S5 scene validation on hardware (this header already provides the
//     seam metric and the layout math).
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Caffeine::Photon {

// ============================================================================
// Layout constants
// ============================================================================
struct FarFieldConfig {
    static constexpr u32 kLevels        = 4;
    static constexpr u32 kCellsPerAxis  = 64;
    static constexpr u32 kProbesPerAxis = 16;
    static constexpr f32 kBaseCell      = 0.25f;
    static constexpr u32 kOctahedralSide   = 8;
    static constexpr u32 kOctahedralBorder = 1;
    /// Interior 8 + 1 texel border on each side.
    static constexpr u32 kTileStride    = kOctahedralSide + 2 * kOctahedralBorder;  // 10
    static constexpr u32 kTileTexels    = kTileStride * kTileStride;                // 100
    /// Frames between two updates of a level (spec §8.3).
    static constexpr u32 kUpdatePeriods[kLevels] = {1, 2, 4, 4};
    static constexpr f32 kHysteresis    = 0.85f;
    static constexpr f32 kOccThreshold  = 0.5f;
    /// Twice the edge of the coarsest level (spec §8.5).
    static constexpr f32 kMaxTraceDistance = 256.0f;
    static constexpr f32 kChebyshevVarFloor = 1.0e-4f;
    static constexpr f32 kWeightEpsilon = 1.0e-3f;
};

/// One clipmap level, recomputed from the camera every frame.
struct FarFieldLevel {
    u32 level = 0;
    f32 cellSize = FarFieldConfig::kBaseCell;   ///< c_l
    f32 extent = 16.0f;                         ///< edge of the covered cube
    f32 halfExtent = 8.0f;
    f32 probeSpacing = 1.0f;                    ///< p_l = 2^l
    Vec3 clipmapOrigin{};                       ///< corner, snapped to c_l
    Vec3 probeOrigin{};                         ///< centre of probe (0,0,0), snapped to p_l
};

/// Selected query level plus the blend factor towards level+1 (outer 25%).
struct FarFieldQueryLevel {
    u32 level = 0;
    f32 blendToCoarser = 0.0f;
};

// ============================================================================
// Small helpers
// ============================================================================
inline f32 farFieldCellSize(u32 level) {
    return FarFieldConfig::kBaseCell * std::exp2(static_cast<f32>(level));
}

inline f32 farFieldExtent(u32 level) {
    return 16.0f * std::exp2(static_cast<f32>(level));
}

/// p_l = 4·c_l = 2^l metres.
inline f32 farFieldProbeSpacing(u32 level) {
    return std::exp2(static_cast<f32>(level));
}

inline Vec3 snapToGrid(const Vec3& value, f32 step) {
    if (step <= 0.0f) return value;
    return Vec3(std::floor(value.x / step) * step,
                std::floor(value.y / step) * step,
                std::floor(value.z / step) * step);
}

inline Vec3 snapToGrid(f32 x, f32 y, f32 z, f32 step) {
    return snapToGrid(Vec3(x, y, z), step);
}

/// Clipmap and probe lattice for one level.
/// The voxel origin snaps to the cell size (c_l); the probe lattice snaps to
/// the probe spacing (p_l) independently, as required by §20.2: snapping the
/// probes to c_l would make the lattice jitter when the camera moves.
inline FarFieldLevel computeFarFieldLevel(u32 level, const Vec3& cameraPos) {
    FarFieldLevel result;
    result.level = level;
    result.cellSize = farFieldCellSize(level);
    result.extent = farFieldExtent(level);
    result.halfExtent = result.extent * 0.5f;
    result.probeSpacing = farFieldProbeSpacing(level);
    const Vec3 corner = cameraPos - Vec3(result.halfExtent, result.halfExtent, result.halfExtent);
    result.clipmapOrigin = snapToGrid(corner, result.cellSize);
    // Probe (0,0,0) sits half a probe spacing inside the snapped lattice.
    result.probeOrigin = snapToGrid(corner, result.probeSpacing) +
                         Vec3(result.probeSpacing * 0.5f, result.probeSpacing * 0.5f,
                              result.probeSpacing * 0.5f);
    return result;
}

/// Interior bounds with a one-probe margin (spec §8.6 level selection).
inline void farFieldInterior(const FarFieldLevel& level, Vec3& minOut, Vec3& maxOut) {
    const f32 margin = level.probeSpacing;
    minOut = level.clipmapOrigin + Vec3(margin, margin, margin);
    const f32 edge = level.extent - margin;
    maxOut = level.clipmapOrigin + Vec3(edge, edge, edge);
}

inline bool farFieldInteriorContains(const FarFieldLevel& level, const Vec3& p) {
    Vec3 lo{}, hi{};
    farFieldInterior(level, lo, hi);
    return p.x >= lo.x && p.y >= lo.y && p.z >= lo.z &&
           p.x <= hi.x && p.y <= hi.y && p.z <= hi.z;
}

inline f32 smoothStep01(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// Smallest level whose interior contains P; the outer 25% blends to level+1.
inline FarFieldQueryLevel selectFarFieldLevel(const Vec3& p,
                                              const FarFieldLevel levels[FarFieldConfig::kLevels]) {
    for (u32 l = 0; l < FarFieldConfig::kLevels; ++l) {
        if (!farFieldInteriorContains(levels[l], p)) continue;

        Vec3 lo{}, hi{};
        farFieldInterior(levels[l], lo, hi);
        const Vec3 size = hi - lo;
        const Vec3 local((size.x > 0.0f ? (p.x - lo.x) / size.x : 0.5f),
                         (size.y > 0.0f ? (p.y - lo.y) / size.y : 0.5f),
                         (size.z > 0.0f ? (p.z - lo.z) / size.z : 0.5f));
        const f32 edgeDistance = std::min(std::min(local.x, 1.0f - local.x),
                                          std::min(std::min(local.y, 1.0f - local.y),
                                                   std::min(local.z, 1.0f - local.z)));
        FarFieldQueryLevel result;
        result.level = l;
        // Outer 25% of the interior (edgeDistance < 0.25).
        result.blendToCoarser = (l + 1 < FarFieldConfig::kLevels)
                                    ? 1.0f - smoothStep01(edgeDistance / 0.25f)
                                    : 0.0f;
        return result;
    }
    FarFieldQueryLevel fallback;
    fallback.level = FarFieldConfig::kLevels - 1;
    fallback.blendToCoarser = 0.0f;
    return fallback;
}

// ============================================================================
// Probe sampling
// ============================================================================
/// Octahedral encoding/decoding (same convention as the Photon shaders).
inline void octEncode(const Vec3& n, f32& u, f32& v) {
    const f32 l1 = std::max(std::abs(n.x) + std::abs(n.y) + std::abs(n.z), 1e-6f);
    f32 x = n.x / l1;
    f32 y = n.y / l1;
    if (n.z < 0.0f) {
        const f32 oldX = x;
        x = (1.0f - std::abs(y)) * (oldX >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - std::abs(oldX)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    u = x * 0.5f + 0.5f;
    v = y * 0.5f + 0.5f;
}

inline Vec3 octDecode(f32 u, f32 v) {
    f32 x = u * 2.0f - 1.0f;
    f32 y = v * 2.0f - 1.0f;
    Vec3 n(x, y, 1.0f - std::abs(x) - std::abs(y));
    if (n.z < 0.0f) {
        const f32 oldX = n.x;
        n.x = (1.0f - std::abs(n.y)) * (oldX >= 0.0f ? 1.0f : -1.0f);
        n.y = (1.0f - std::abs(oldX)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    }
    const f32 len = n.length();
    return len > 1e-6f ? n / len : Vec3(0.0f, 0.0f, 1.0f);
}

/// Atlas coordinate inside a 10×10 tile, including the bilinear border.
/// `u,v` in [0,1] map to [0.5, 8.5] texel space (centres 1..8 are interior).
inline void octahedralTileCoord(f32 u, f32 v, f32& tileX, f32& tileY) {
    const f32 s = static_cast<f32>(FarFieldConfig::kOctahedralSide);
    tileX = 0.5f + std::clamp(u, 0.0f, 1.0f) * s;
    tileY = 0.5f + std::clamp(v, 0.0f, 1.0f) * s;
}

/// Trilinear weights of the 8 probes around `p` in one level.
/// Indices are clamped to the grid; outWeights sum to 1.
inline void trilinearProbeWeights(const Vec3& p, const FarFieldLevel& level, f32 outWeights[8],
                                  u32 outIndices[8]) {
    const f32 spacing = level.probeSpacing;
    const Vec3 g((p.x - level.probeOrigin.x) / spacing,
                 (p.y - level.probeOrigin.y) / spacing,
                 (p.z - level.probeOrigin.z) / spacing);
    const Vec3 baseF(std::floor(g.x), std::floor(g.y), std::floor(g.z));
    const Vec3 f(g.x - baseF.x, g.y - baseF.y, g.z - baseF.z);
    const i32 n = static_cast<i32>(FarFieldConfig::kProbesPerAxis);
    const i32 base[3] = {static_cast<i32>(baseF.x), static_cast<i32>(baseF.y),
                         static_cast<i32>(baseF.z)};
    const f32 frac[3] = {f.x, f.y, f.z};
    for (u32 i = 0; i < 8; ++i) {
        const u32 ox = i & 1u;
        const u32 oy = (i >> 1) & 1u;
        const u32 oz = (i >> 2) & 1u;
        const i32 x = std::clamp(base[0] + static_cast<i32>(ox), 0, n - 1);
        const i32 y = std::clamp(base[1] + static_cast<i32>(oy), 0, n - 1);
        const i32 z = std::clamp(base[2] + static_cast<i32>(oz), 0, n - 1);
        // A clamp collapses two corners; both keep their weight so the total
        // stays 1 (same probe accumulates).
        outWeights[i] = (ox ? frac[0] : 1.0f - frac[0]) *
                        (oy ? frac[1] : 1.0f - frac[1]) *
                        (oz ? frac[2] : 1.0f - frac[2]);
        outIndices[i] = static_cast<u32>(x) +
                        static_cast<u32>(y) * FarFieldConfig::kProbesPerAxis +
                        static_cast<u32>(z) * FarFieldConfig::kProbesPerAxis *
                            FarFieldConfig::kProbesPerAxis;
    }
}

/// Chebyshev visibility (spec §8.6). `mean`/`meanSquared` come from RG16F.
inline f32 chebyshevWeight(f32 distance, f32 mean, f32 meanSquared) {
    const f32 variance = std::max(meanSquared - mean * mean,
                                  FarFieldConfig::kChebyshevVarFloor);
    const f32 slack = std::max(distance - mean, 0.0f);
    const f32 c = variance / (variance + slack * slack);
    return c * c * c;
}

/// Anisotropic voxel face radiance (spec §8.3). Faces: +X,-X,+Y,-Y,+Z,-Z.
inline Vec3 voxelFaceRadiance(const Vec3 faces[6], const Vec3& omega) {
    const f32 wx = omega.x * omega.x;
    const f32 wy = omega.y * omega.y;
    const f32 wz = omega.z * omega.z;
    const Vec3& x = faces[omega.x > 0.0f ? 1 : 0];
    const Vec3& y = faces[omega.y > 0.0f ? 3 : 2];
    const Vec3& z = faces[omega.z > 0.0f ? 5 : 4];
    return Vec3(x.x * wx + y.x * wy + z.x * wz,
                x.y * wx + y.y * wy + z.y * wz,
                x.z * wx + y.z * wy + z.z * wz);
}

// ============================================================================
// Update scheduling
// ============================================================================
inline u32 farFieldUpdatePeriod(u32 level) {
    return FarFieldConfig::kUpdatePeriods[std::min(level, FarFieldConfig::kLevels - 1)];
}

/// True when level l is re-injected on this frame (round-robin 1/U_l).
inline bool farFieldUpdatesThisFrame(u32 level, u32 frameIndex) {
    const u32 period = farFieldUpdatePeriod(level);
    return period == 0 ? false : (frameIndex % period) == 0;
}

/// Which probe slice (of U_l) is updated this frame.
inline u32 farFieldProbeSlice(u32 level, u32 frameIndex) {
    return frameIndex % std::max(farFieldUpdatePeriod(level), 1u);
}

/// Temporal blend used for radiance, moments and sky alpha.
inline f32 farFieldHysteresis(f32 newValue, f32 oldValue) {
    return newValue * (1.0f - FarFieldConfig::kHysteresis) +
           oldValue * FarFieldConfig::kHysteresis;
}

inline bool voxelIsSolid(f32 coverage) {
    return coverage >= FarFieldConfig::kOccThreshold;
}

/// Convert a linear octahedral index (x,y,z) into the probe-major atlas texel.
/// Level l occupies a `16·10` square; levels are packed side by side.
inline void farFieldAtlasTexel(u32 level, u32 probeX, u32 probeY, u32 probeZ, f32 tileX,
                               f32 tileY, u32& atlasX, u32& atlasY, u32& atlasWidth) {
    const u32 probeIndex = probeX + probeY * FarFieldConfig::kProbesPerAxis +
                           probeZ * FarFieldConfig::kProbesPerAxis *
                               FarFieldConfig::kProbesPerAxis;
    // Probes are laid out in probeIndex order, 16 per row.
    const u32 perRow = FarFieldConfig::kProbesPerAxis;
    const u32 row = probeIndex / perRow;
    const u32 col = probeIndex % perRow;
    const u32 levelOffset = level * perRow * FarFieldConfig::kTileStride;
    atlasX = levelOffset + col * FarFieldConfig::kTileStride + static_cast<u32>(tileX);
    atlasY = row * FarFieldConfig::kTileStride + static_cast<u32>(tileY);
    atlasWidth = FarFieldConfig::kLevels * perRow * FarFieldConfig::kTileStride;
}

// ============================================================================
// Clipmap movement (toroidal storage, spec §8.1)
// ============================================================================
/// Conservative AABB of voxels that must be re-voxelised after the clipmap
/// origin moved. `max` is exclusive. `full` means the camera moved by at least
/// one full grid, so the whole level must be rebuilt.
struct ClipmapDirtyBox {
    bool full = false;
    bool empty = true;
    u32 min[3] = {0, 0, 0};
    u32 max[3] = {0, 0, 0};
};

/// Integer cells the origin moved along one axis.
inline i32 clipmapCellDelta(f32 oldOrigin, f32 newOrigin, f32 cellSize) {
    if (cellSize <= 0.0f) return 0;
    return static_cast<i32>(std::lround((newOrigin - oldOrigin) / cellSize));
}

inline ClipmapDirtyBox clipmapDirtyRegion(const Vec3& oldOrigin, const Vec3& newOrigin,
                                          f32 cellSize, u32 cellsPerAxis) {
    ClipmapDirtyBox box;
    if (cellsPerAxis == 0 || cellSize <= 0.0f) return box;

    const f32 oldPos[3] = {oldOrigin.x, oldOrigin.y, oldOrigin.z};
    const f32 newPos[3] = {newOrigin.x, newOrigin.y, newOrigin.z};
    i32 delta[3] = {0, 0, 0};
    bool any = false;
    for (u32 axis = 0; axis < 3; ++axis) {
        delta[axis] = clipmapCellDelta(oldPos[axis], newPos[axis], cellSize);
        if (delta[axis] != 0) any = true;
    }
    if (!any) return box;

    box.empty = false;
    for (u32 axis = 0; axis < 3; ++axis) {
        const i32 d = delta[axis];
        const i32 cells = static_cast<i32>(cellsPerAxis);
        if (d == 0) {
            // No movement on this axis: the whole axis is dirty (conservative).
            box.min[axis] = 0;
            box.max[axis] = cellsPerAxis;
        } else if (std::abs(d) >= cells) {
            box.full = true;
            box.min[axis] = 0;
            box.max[axis] = cellsPerAxis;
        } else if (d > 0) {
            box.min[axis] = static_cast<u32>(cells - d);
            box.max[axis] = cellsPerAxis;
        } else {
            box.min[axis] = 0;
            box.max[axis] = static_cast<u32>(-d);
        }
    }
    return box;
}

inline bool clipmapDirtyContains(const ClipmapDirtyBox& box, u32 x, u32 y, u32 z) {
    if (box.empty) return false;
    if (box.full) return true;
    return x >= box.min[0] && x < box.max[0] && y >= box.min[1] && y < box.max[1] &&
           z >= box.min[2] && z < box.max[2];
}

// ============================================================================
// Evaluation metric (S4 / S5 seam check, spec §16.3)
// ============================================================================
/// Relative error between two radiance samples.
inline f32 relativeRadianceError(const Vec3& a, const Vec3& b) {
    const f32 scale = std::max(a.length(), b.length()) + 1e-4f;
    return (a - b).length() / scale;
}

/// RMS seam error between the same set of points sampled on adjacent levels.
/// Returns 0 when both sets are identical; NaN-free for empty inputs.
inline f32 seamMetric(const std::vector<Vec3>& fine, const std::vector<Vec3>& coarse) {
    const size_t count = std::min(fine.size(), coarse.size());
    if (count == 0) return 0.0f;
    f64 sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const f32 e = relativeRadianceError(fine[i], coarse[i]);
        sum += static_cast<f64>(e) * static_cast<f64>(e);
    }
    return static_cast<f32>(std::sqrt(sum / static_cast<f64>(count)));
}

}  // namespace Caffeine::Photon
