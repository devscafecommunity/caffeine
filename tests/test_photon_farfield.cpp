#include "catch.hpp"

#include "caffeine/photon/PhotonFarField.hpp"

using namespace Caffeine;
using namespace Caffeine::Photon;

TEST_CASE("FarField - Level constants follow the spec", "[photon][farfield]") {
    REQUIRE(farFieldCellSize(0) == Approx(0.25f));
    REQUIRE(farFieldCellSize(1) == Approx(0.5f));
    REQUIRE(farFieldCellSize(3) == Approx(2.0f));

    REQUIRE(farFieldExtent(0) == Approx(16.0f));
    REQUIRE(farFieldExtent(1) == Approx(32.0f));
    REQUIRE(farFieldExtent(3) == Approx(128.0f));

    // p_l = 1, 2, 4, 8 m
    REQUIRE(farFieldProbeSpacing(0) == Approx(1.0f));
    REQUIRE(farFieldProbeSpacing(1) == Approx(2.0f));
    REQUIRE(farFieldProbeSpacing(2) == Approx(4.0f));
    REQUIRE(farFieldProbeSpacing(3) == Approx(8.0f));

    REQUIRE(FarFieldConfig::kTileStride == 10);
    REQUIRE(FarFieldConfig::kMaxTraceDistance == Approx(256.0f));
}

TEST_CASE("FarField - Clipmap snaps to cells, probe lattice snaps to spacing",
          "[photon][farfield]") {
    const Vec3 camera(10.13f, 2.71f, -5.42f);
    const FarFieldLevel level = computeFarFieldLevel(0, camera);

    const f32 cell = level.cellSize;
    const f32 spacing = level.probeSpacing;
    // Clipmap corner is a multiple of the cell size.
    REQUIRE(level.clipmapOrigin.x / cell == Approx(std::floor(level.clipmapOrigin.x / cell)));
    REQUIRE(level.clipmapOrigin.y / cell == Approx(std::floor(level.clipmapOrigin.y / cell)));
    // Probe 0 is a half-spacing offset from a lattice point.
    REQUIRE((level.probeOrigin.x - spacing * 0.5f) / spacing ==
            Approx(std::floor((level.probeOrigin.x - spacing * 0.5f) / spacing)));
    REQUIRE((level.probeOrigin.z - spacing * 0.5f) / spacing ==
            Approx(std::floor((level.probeOrigin.z - spacing * 0.5f) / spacing)));

    // Moving the camera by less than a cell must not move the voxel origin.
    const FarFieldLevel nudged = computeFarFieldLevel(0, camera + Vec3(cell * 0.4f, 0.0f, 0.0f));
    REQUIRE(nudged.clipmapOrigin.x == level.clipmapOrigin.x);

    // The probe lattice snaps to the probe spacing, not the cell size: moving
    // by one cell (0.5 m at level 1) must not move it; moving by one probe
    // spacing (2 m) must.
    const FarFieldLevel l1 = computeFarFieldLevel(1, camera);
    const FarFieldLevel l1Cell = computeFarFieldLevel(1, camera + Vec3(0.5f, 0.0f, 0.0f));
    REQUIRE(l1Cell.probeOrigin.x == l1.probeOrigin.x);
    const FarFieldLevel l1Moved = computeFarFieldLevel(1, camera + Vec3(2.0f, 0.0f, 0.0f));
    REQUIRE(l1Moved.probeOrigin.x != l1.probeOrigin.x);
}

TEST_CASE("FarField - Level selection uses the smallest containing interior",
          "[photon][farfield]") {
    const Vec3 camera(0.0f, 0.0f, 0.0f);
    FarFieldLevel levels[FarFieldConfig::kLevels];
    for (u32 l = 0; l < FarFieldConfig::kLevels; ++l) {
        levels[l] = computeFarFieldLevel(l, camera);
    }

    const FarFieldQueryLevel center = selectFarFieldLevel(Vec3(0.0f, 0.0f, 0.0f), levels);
    REQUIRE(center.level == 0);
    REQUIRE(center.blendToCoarser == Approx(0.0f));

    // Just inside level 0's interior edge: must still pick level 0 but blend.
    Vec3 lo{}, hi{};
    farFieldInterior(levels[0], lo, hi);
    const Vec3 nearEdge(hi.x - 0.5f, 0.0f, 0.0f);
    const FarFieldQueryLevel edge = selectFarFieldLevel(nearEdge, levels);
    REQUIRE(edge.level == 0);
    REQUIRE(edge.blendToCoarser > 0.0f);

    // Outside level 0 (extent 16, interior margin 1 -> 7): level 1.
    const FarFieldQueryLevel outside = selectFarFieldLevel(Vec3(10.0f, 0.0f, 0.0f), levels);
    REQUIRE(outside.level == 1);
}

TEST_CASE("FarField - Trilinear weights are a partition of unity", "[photon][farfield]") {
    const FarFieldLevel level = computeFarFieldLevel(0, Vec3(0.0f, 0.0f, 0.0f));
    f32 weights[8];
    u32 indices[8];
    trilinearProbeWeights(Vec3(0.3f, -0.2f, 0.7f), level, weights, indices);

    f32 sum = 0.0f;
    for (f32 w : weights) sum += w;
    REQUIRE(sum == Approx(1.0f));

    // At an exact probe position one weight dominates.
    const Vec3 atProbe = level.probeOrigin + Vec3(level.probeSpacing * 2.0f, 0.0f, 0.0f);
    trilinearProbeWeights(atProbe, level, weights, indices);
    f32 maxWeight = 0.0f;
    for (f32 w : weights) maxWeight = std::max(maxWeight, w);
    REQUIRE(maxWeight == Approx(1.0f));
}

TEST_CASE("FarField - Chebyshev visibility", "[photon][farfield]") {
    // At or in front of the mean distance the weight is 1.
    REQUIRE(chebyshevWeight(0.5f, 1.0f, 1.5f) == Approx(1.0f));
    // The variance floor keeps a degenerate moment finite and in [0,1].
    const f32 w = chebyshevWeight(100.0f, 0.0f, 0.0f);
    REQUIRE(w >= 0.0f);
    REQUIRE(w <= 1.0f);
    // Farther than the mean reduces the weight.
    REQUIRE(chebyshevWeight(5.0f, 1.0f, 2.0f) < chebyshevWeight(2.0f, 1.0f, 2.0f));
}

TEST_CASE("FarField - Octahedral encode/decode round-trips", "[photon][farfield]") {
    const Vec3 dirs[] = {{1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                         {0.0f, 0.0f, 1.0f},  {0.3f, -0.7f, 0.2f},  {0.2f, 0.1f, -0.9f}};
    for (const Vec3& d : dirs) {
        const Vec3 n = d / d.length();
        f32 u = 0.0f, v = 0.0f;
        octEncode(n, u, v);
        REQUIRE(u >= 0.0f);
        REQUIRE(u <= 1.0f);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        const Vec3 back = octDecode(u, v);
        REQUIRE((back - n).length() < 1e-4f);

        f32 tx = 0.0f, ty = 0.0f;
        octahedralTileCoord(u, v, tx, ty);
        REQUIRE(tx >= 0.5f);
        REQUIRE(tx <= 8.5f);
        REQUIRE(ty >= 0.5f);
        REQUIRE(ty <= 8.5f);
    }
}

TEST_CASE("FarField - Anisotropic voxel face radiance", "[photon][farfield]") {
    const Vec3 faces[6] = {Vec3(1, 0, 0),  Vec3(2, 0, 0), Vec3(0, 3, 0),
                           Vec3(0, 4, 0), Vec3(0, 0, 5), Vec3(0, 0, 6)};

    // Traveling towards +X reads the -X face; towards -Y reads the +Y face.
    REQUIRE(voxelFaceRadiance(faces, Vec3(1, 0, 0)).x == Approx(2.0f));
    REQUIRE(voxelFaceRadiance(faces, Vec3(0, -1, 0)).y == Approx(3.0f));

    // A generic direction is a convex combination of the three back faces.
    const Vec3 mixed = voxelFaceRadiance(faces, Vec3(0.0f, 0.0f, -1.0f));
    REQUIRE(mixed.z == Approx(5.0f));
    const Vec3 diag = voxelFaceRadiance(faces, Vec3(0.6f, -0.8f, 0.0f));
    REQUIRE(diag.x == Approx(2.0f * 0.36f));
    REQUIRE(diag.y == Approx(3.0f * 0.64f));
}

TEST_CASE("FarField - Round-robin update schedule", "[photon][farfield]") {
    REQUIRE(farFieldUpdatePeriod(0) == 1);
    REQUIRE(farFieldUpdatePeriod(1) == 2);
    REQUIRE(farFieldUpdatePeriod(2) == 4);
    REQUIRE(farFieldUpdatePeriod(3) == 4);

    REQUIRE(farFieldUpdatesThisFrame(0, 5));
    REQUIRE(farFieldUpdatesThisFrame(0, 6));
    REQUIRE(farFieldUpdatesThisFrame(1, 4));
    REQUIRE_FALSE(farFieldUpdatesThisFrame(1, 5));
    REQUIRE(farFieldUpdatesThisFrame(2, 8));
    REQUIRE_FALSE(farFieldUpdatesThisFrame(2, 9));
    REQUIRE(farFieldProbeSlice(2, 9) == 1);
    REQUIRE(farFieldProbeSlice(2, 8) == 0);
}

TEST_CASE("FarField - Hysteresis and occupancy", "[photon][farfield]") {
    REQUIRE(farFieldHysteresis(1.0f, 0.0f) == Approx(0.15f));
    REQUIRE(farFieldHysteresis(0.0f, 1.0f) == Approx(0.85f));
    REQUIRE(farFieldHysteresis(1.0f, 1.0f) == Approx(1.0f));

    REQUIRE(voxelIsSolid(0.5f));
    REQUIRE(voxelIsSolid(0.9f));
    REQUIRE_FALSE(voxelIsSolid(0.49f));
}

TEST_CASE("FarField - Atlas packing stays inside the level strip", "[photon][farfield]") {
    u32 x = 0, y = 0, width = 0;
    farFieldAtlasTexel(3, 15, 15, 15, 9.0f, 9.0f, x, y, width);
    REQUIRE(width == 4 * 16 * 10);
    REQUIRE(x < width);
    REQUIRE(x >= 3 * 16 * 10);  // inside level 3's strip
    REQUIRE(y < 16 * 10 * 16);

    u32 x0 = 0, y0 = 0, w0 = 0;
    farFieldAtlasTexel(0, 0, 0, 0, 0.0f, 0.0f, x0, y0, w0);
    REQUIRE(x0 == 0);
    REQUIRE(y0 == 0);
}

TEST_CASE("FarField - Seam metric", "[photon][farfield]") {
    std::vector<Vec3> a = {Vec3(1, 1, 1), Vec3(2, 0, 0)};
    REQUIRE(seamMetric(a, a) == Approx(0.0f));

    std::vector<Vec3> b = {Vec3(2, 2, 2), Vec3(2, 0, 0)};
    const f32 err = seamMetric(a, b);
    REQUIRE(err > 0.0f);
    REQUIRE(err == Approx(std::sqrt((0.5f * 0.5f) / 2.0f)).epsilon(1e-3f));

    REQUIRE(seamMetric({}, a) == Approx(0.0f));
}

TEST_CASE("FarField - Toroidal clipmap dirty region", "[photon][farfield]") {
    const Vec3 origin(0.0f, 0.0f, 0.0f);
    const f32 cell = 0.25f;

    // No movement -> nothing to rebuild.
    ClipmapDirtyBox same = clipmapDirtyRegion(origin, origin, cell, 64);
    REQUIRE(same.empty);
    REQUIRE_FALSE(same.full);

    // +1 cell on X: the exposed slab is the last cell of the new ring.
    ClipmapDirtyBox plusX = clipmapDirtyRegion(origin, Vec3(cell, 0.0f, 0.0f), cell, 64);
    REQUIRE_FALSE(plusX.empty);
    REQUIRE_FALSE(plusX.full);
    REQUIRE(plusX.min[0] == 63);
    REQUIRE(plusX.max[0] == 64);
    REQUIRE(plusX.min[1] == 0);
    REQUIRE(plusX.max[1] == 64);
    REQUIRE(clipmapDirtyContains(plusX, 63, 10, 10));
    REQUIRE_FALSE(clipmapDirtyContains(plusX, 62, 10, 10));

    // -2 cells on Z: the exposed slab is the first two cells.
    ClipmapDirtyBox minusZ = clipmapDirtyRegion(origin, Vec3(0.0f, 0.0f, -2.0f * cell), cell, 64);
    REQUIRE(minusZ.min[2] == 0);
    REQUIRE(minusZ.max[2] == 2);
    REQUIRE(clipmapDirtyContains(minusZ, 5, 5, 1));
    REQUIRE_FALSE(clipmapDirtyContains(minusZ, 5, 5, 2));

    // A full grid jump rebuilds everything.
    ClipmapDirtyBox full =
        clipmapDirtyRegion(origin, Vec3(64.0f * cell, 0.0f, 0.0f), cell, 64);
    REQUIRE(full.full);
    REQUIRE(clipmapDirtyContains(full, 1, 2, 3));
}
