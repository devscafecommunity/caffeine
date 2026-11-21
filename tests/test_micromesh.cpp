#include "catch.hpp"

#include "../src/caffeine/micromesh/Micromesh.hpp"
#include "caffeine/capi/EngineAPI.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::Micromesh;

namespace {

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<u32> indices;
};

Mesh icosphere(int subdivisions) {
    const f32 t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    Mesh m;
    const Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (const Vec3& v : base) m.positions.push_back(v.normalized());
    m.indices = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2,
                 10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5,
                 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<u32, u32>, u32> mid;
        auto middle = [&](u32 a, u32 b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            m.positions.push_back(((m.positions[a] + m.positions[b]) * 0.5f).normalized());
            return mid[key] = static_cast<u32>(m.positions.size() - 1);
        };
        std::vector<u32> next;
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            const u32 a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
            const u32 ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        m.indices = std::move(next);
    }
    return m;
}

Mesh torus(int major, int minor) {
    Mesh m;
    for (int i = 0; i < major; ++i) {
        for (int j = 0; j < minor; ++j) {
            const f32 u = 6.2831853f * static_cast<f32>(i) / static_cast<f32>(major);
            const f32 v = 6.2831853f * static_cast<f32>(j) / static_cast<f32>(minor);
            const f32 r = 2.0f + std::cos(v) * 0.7f;
            m.positions.push_back(Vec3(r * std::cos(u), std::sin(v) * 0.7f, r * std::sin(u)));
        }
    }
    auto at = [&](int i, int j) { return static_cast<u32>((i % major) * minor + (j % minor)); };
    for (int i = 0; i < major; ++i) {
        for (int j = 0; j < minor; ++j) {
            m.indices.insert(m.indices.end(), {at(i, j), at(i + 1, j), at(i, j + 1),
                                               at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    }
    return m;
}

Mesh grid(int n) {
    Mesh m;
    for (int z = 0; z <= n; ++z) {
        for (int x = 0; x <= n; ++x) {
            m.positions.push_back(Vec3(static_cast<f32>(x), 0.2f * std::sin(static_cast<f32>(x) * 0.7f), static_cast<f32>(z)));
        }
    }
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const u32 a = static_cast<u32>(z * (n + 1) + x);
            const u32 b = a + 1, c = a + static_cast<u32>(n + 1), d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }
    return m;
}

std::map<std::pair<u32, u32>, int> edgeCounts(const std::vector<u32>& indices) {
    std::map<std::pair<u32, u32>, int> edges;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            const u32 a = indices[i + static_cast<size_t>(k)], b = indices[i + static_cast<size_t>((k + 1) % 3)];
            ++edges[{std::min(a, b), std::max(a, b)}];
        }
    }
    return edges;
}

std::vector<u32> cutIndices(const Data& d, const CutParams& p) {
    std::vector<u32> out;
    for (u32 c : selectCut(d, p)) appendTriangles(d, c, out);
    return out;
}

f32 area(const Data& d, const std::vector<u32>& indices) {
    f32 a = 0.0f;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        a += 0.5f * (d.positions[indices[i + 1]] - d.positions[indices[i]])
                        .cross(d.positions[indices[i + 2]] - d.positions[indices[i]]).length();
    }
    return a;
}

}  // namespace

TEST_CASE("Micromesh: clusters respect the limits and the hierarchy reaches one root", "[micromesh]") {
    const Mesh m = icosphere(5);  // 20480 triangles
    const Data d = build(m.positions, m.indices);
    REQUIRE(d.sourceTriangles == 20480u);
    REQUIRE(d.levelCount >= 6u);
    std::vector<u32> perLevel(d.levelCount, 0);
    u32 roots = 0;
    for (const Cluster& c : d.clusters) {
        CHECK(c.triangleCount >= 1u);
        CHECK(c.triangleCount <= kMaxClusterTriangles);
        CHECK(c.vertexCount <= kMaxClusterVertices);
        perLevel[c.level] += c.triangleCount;
        if (c.parentError >= kRootError) ++roots;
    }
    CHECK(perLevel[0] == 20480u);
    for (u32 l = 1; l < d.levelCount; ++l) CHECK(perLevel[l] < perLevel[l - 1]);  // every level is coarser
    CHECK(perLevel.back() <= kMaxClusterTriangles);
    CHECK(roots == 1u);  // a closed mesh simplifies down to a single cluster
}

TEST_CASE("Micromesh: errors only grow toward the root and spheres contain their children", "[micromesh]") {
    const Mesh m = icosphere(4);
    const Data d = build(m.positions, m.indices);
    for (const Cluster& c : d.clusters) {
        if (c.parentError >= kRootError) continue;
        CHECK(c.parentError > c.error);
        const f32 gap = (c.parentLodSphere.center - c.lodSphere.center).length() + c.lodSphere.radius;
        CHECK(gap <= c.parentLodSphere.radius * 1.0001f + 1e-5f);
    }
    // Level 0 is exact; coarser levels have a real error.
    f32 top = 0.0f;
    for (const Cluster& c : d.clusters) {
        if (c.level == 0) CHECK(c.error == 0.0f);
        top = std::max(top, c.error);
    }
    CHECK(top > 0.0f);
}

TEST_CASE("Micromesh: every cut of a closed mesh is crack-free and keeps the surface", "[micromesh]") {
    for (const Mesh& m : {icosphere(5), torus(96, 48)}) {
        const Data d = build(m.positions, m.indices);
        const f32 fullArea = area(d, m.indices);
        // Threshold 0 selects the source; a huge one selects the root; in between, mixed levels.
        std::vector<f32> thresholds = {0.0f, 1e-4f, 3e-4f, 1e-3f, 2e-3f, 5e-3f, 0.01f, 0.02f, 0.05f, 0.1f, 0.3f, 1e9f};
        size_t last = ~size_t{0};
        for (f32 threshold : thresholds) {
            CutParams p;
            p.threshold = threshold;
            const auto indices = cutIndices(d, p);
            REQUIRE_FALSE(indices.empty());
            for (const auto& [edge, count] : edgeCounts(indices)) {
                (void)edge;
                REQUIRE(count == 2);  // closed and manifold: no crack, no overlap
            }
            // The coarsest cut is smaller than the finest, and the area is the surface's, within the shrink
            // that simplification causes.
            // The error is an RMS distance, so a coarse cut may shrink the surface a little more than its error
            // suggests (the root, at most 128 triangles, is a polyhedron inside the sphere).
            CHECK(area(d, indices) > fullArea * 0.8f);
            CHECK(area(d, indices) <= fullArea * 1.001f);
            if (last != ~size_t{0}) CHECK(indices.size() <= last);  // a higher threshold never adds triangles
            last = indices.size();
        }
        CutParams finest;
        CHECK(cutIndices(d, finest).size() == m.indices.size());
    }
}

TEST_CASE("Micromesh: view-dependent cuts are crack-free and coarser far away", "[micromesh]") {
    const Mesh m = icosphere(5);
    const Data d = build(m.positions, m.indices);
    size_t near = 0, far = 0;
    for (f32 distance : {1.5f, 3.0f, 8.0f, 40.0f}) {
        CutParams p;
        p.viewDependent = true;
        p.camera = Vec3(distance, 0.2f, 0.1f);
        p.threshold = 0.002f;
        const auto indices = cutIndices(d, p);
        for (const auto& [edge, count] : edgeCounts(indices)) {
            (void)edge;
            REQUIRE(count == 2);
        }
        if (distance == 1.5f) near = indices.size();
        if (distance == 40.0f) far = indices.size();
    }
    CHECK(far < near);
}

TEST_CASE("Micromesh: an open mesh keeps its boundary and still terminates", "[micromesh]") {
    const Mesh m = grid(48);
    const Data d = build(m.positions, m.indices);
    const auto source = edgeCounts(m.indices);
    for (f32 threshold : {0.0f, 0.01f, 0.05f, 0.2f, 1e9f}) {
        CutParams p;
        p.threshold = threshold;
        const auto indices = cutIndices(d, p);
        REQUIRE_FALSE(indices.empty());
        for (const auto& [edge, count] : edgeCounts(indices)) {
            REQUIRE(count >= 1);
            REQUIRE(count <= 2);
            if (count == 1) {
                // A free edge is a source boundary edge: its vertices were locked, so it was never moved.
                const auto it = source.find(edge);
                CHECK((it != source.end() && it->second == 1));
            }
        }
    }
}

TEST_CASE("Micromesh: back-face cones never hide a cluster that faces the camera", "[micromesh]") {
    const Mesh m = icosphere(4);
    const Data d = build(m.positions, m.indices);
    const Vec3 camera(50.0f, 0.0f, 0.0f);
    u32 culled = 0;
    for (u32 i = 0; i < d.clusters.size(); ++i) {
        const Cluster& c = d.clusters[i];
        if (c.level != 0) continue;
        if (!isBackFacing(c, camera)) continue;
        ++culled;
        std::vector<u32> tris;
        appendTriangles(d, i, tris);
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            const Vec3 n = (d.positions[tris[t + 1]] - d.positions[tris[t]]).cross(d.positions[tris[t + 2]] - d.positions[tris[t]]);
            const Vec3 centre = (d.positions[tris[t]] + d.positions[tris[t + 1]] + d.positions[tris[t + 2]]) * (1.0f / 3.0f);
            CHECK(n.dot(camera - centre) <= 1e-4f);
        }
    }
    CHECK(culled > 0u);
    // Roughly the far half is culled, never the near side.
    CHECK(culled < static_cast<u32>(d.clusters.size()));
}

TEST_CASE("Micromesh: shading normals reach every level and follow the surface", "[micromesh][normals]") {
    // The unit sphere: the true normal at a point is the point itself.
    const Mesh m = icosphere(5);
    const Data d = build(m.positions, m.indices);
    REQUIRE(d.normals.size() == d.positions.size());
    REQUIRE(d.levelCount > 3u);
    f32 worst = 1.0f;
    u32 maxLevelChecked = 0;
    for (const Cluster& c : d.clusters) {
        for (u32 i = 0; i < c.vertexCount; ++i) {
            const u32 v = d.vertexIndices[c.firstVertex + i];
            const f32 length = d.normals[v].length();
            REQUIRE(length == Approx(1.0f).margin(1e-3f));
            const Vec3 expected = d.positions[v].normalized();
            worst = std::min(worst, d.normals[v].dot(expected));
        }
        maxLevelChecked = std::max(maxLevelChecked, c.level);
    }
    // Even the coarsest level (a few dozen triangles) shades like the sphere it stands for.
    CHECK(maxLevelChecked == d.levelCount - 1u);
    CHECK(worst > 0.9f);
    // The normals are part of the bytes.
    const auto bytes = serialize(d);
    Data back;
    REQUIRE(deserialize(bytes.data(), bytes.size(), back));
    CHECK(back.normals.size() == d.normals.size());
    CHECK(serialize(back) == bytes);
    CHECK(std::memcmp(back.normals.data(), d.normals.data(), d.normals.size() * sizeof(Vec3)) == 0);
}

TEST_CASE("Micromesh: texture coordinates are carried, and UV seams are never simplified across",
          "[micromesh][uv]") {
    // A torus with real seams: the first and last column / row are duplicated vertices, so the u = 0 / 1
    // and v = 0 / 1 lines are boundaries of split vertices, as in an exported mesh.
    constexpr int major = 128, minor = 64;
    constexpr f32 tau = 6.2831853f;
    auto surface = [&](f32 u, f32 v) {
        const f32 r = 2.0f + std::cos(v * tau) * 0.7f;
        return Vec3(r * std::cos(u * tau), std::sin(v * tau) * 0.7f, r * std::sin(u * tau));
    };
    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    for (int i = 0; i <= major; ++i) {
        for (int j = 0; j <= minor; ++j) {
            const f32 u = static_cast<f32>(i) / major, v = static_cast<f32>(j) / minor;
            positions.push_back(surface(u, v));
            uvs.push_back(Vec2(u, v));
        }
    }
    auto at = [&](int i, int j) { return static_cast<u32>(i * (minor + 1) + j); };
    std::vector<u32> indices;
    for (int i = 0; i < major; ++i) {
        for (int j = 0; j < minor; ++j) {
            indices.insert(indices.end(), {at(i, j), at(i + 1, j), at(i, j + 1),
                                           at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    }
    const Data d = build(positions, indices, uvs);
    REQUIRE(d.uvs.size() == d.positions.size());
    REQUIRE(d.levelCount >= 3u);

    // Texture coordinates stay valid, and every vertex sits where its UV says it should, to within the
    // simplification error of the level it belongs to (the collapses move vertices, the UVs follow the edge).
    f32 worstRatio = 0.0f;
    for (size_t v = 0; v < d.positions.size(); ++v) {
        REQUIRE(d.uvs[v].x >= -1e-4f);
        REQUIRE(d.uvs[v].x <= 1.0001f);
        REQUIRE(d.uvs[v].y >= -1e-4f);
        REQUIRE(d.uvs[v].y <= 1.0001f);
    }
    for (const Cluster& c : d.clusters) {
        for (u32 i = 0; i < c.vertexCount; ++i) {
            const u32 v = d.vertexIndices[c.firstVertex + i];
            const f32 off = (d.positions[v] - surface(d.uvs[v].x, d.uvs[v].y)).length();
            worstRatio = std::max(worstRatio, off / std::max(c.error, 0.01f));
        }
    }
    CHECK(worstRatio < 6.0f);

    // No triangle of any cut spans a seam (that would sweep the whole texture across it).
    for (f32 threshold : {0.0f, 0.01f, 0.04f, 0.15f}) {
        CutParams cut;
        cut.threshold = threshold;
        std::vector<u32> tris;
        for (u32 c : selectCut(d, cut)) appendTriangles(d, c, tris);
        REQUIRE_FALSE(tris.empty());
        f32 widest = 0.0f;
        for (size_t i = 0; i < tris.size(); i += 3) {
            for (int k = 0; k < 3; ++k) {
                const Vec2& a = d.uvs[tris[i + static_cast<size_t>(k)]];
                const Vec2& b = d.uvs[tris[i + static_cast<size_t>((k + 1) % 3)]];
                widest = std::max(widest, std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)));
            }
        }
        INFO("threshold " << threshold);
        CHECK(widest < 0.5f);
    }
    // The UVs are part of the bytes.
    const auto bytes = serialize(d);
    Data back;
    REQUIRE(deserialize(bytes.data(), bytes.size(), back));
    CHECK(serialize(back) == bytes);
    CHECK(back.uvs.size() == d.uvs.size());
    // Without UVs nothing changes: the array is empty.
    const Data plain = build(positions, indices);
    CHECK(plain.uvs.empty());
}

TEST_CASE("Micromesh: vertex colours are carried through every level", "[micromesh][colors]") {
    const Mesh m = icosphere(5);
    // A colour that is a pure function of the position, so every level can be checked against it.
    std::vector<Vec4> colors;
    for (const Vec3& p : m.positions) colors.push_back(Vec4(0.5f + 0.5f * p.x, 0.5f + 0.5f * p.y, 0.5f + 0.5f * p.z, 1.0f));
    Attributes attributes;
    attributes.colors = &colors;
    const Data d = build(m.positions, m.indices, attributes);
    REQUIRE(d.colors.size() == d.positions.size());
    REQUIRE(d.levelCount > 3u);
    f32 worst = 0.0f;
    for (const Cluster& c : d.clusters) {
        for (u32 i = 0; i < c.vertexCount; ++i) {
            const u32 v = d.vertexIndices[c.firstVertex + i];
            const Vec4& color = d.colors[v];
            const Vec3 p = d.positions[v];
            REQUIRE(color.w == Approx(1.0f).margin(1e-4f));
            worst = std::max(worst, std::abs(color.x - (0.5f + 0.5f * p.x)) + std::abs(color.y - (0.5f + 0.5f * p.y)) +
                                         std::abs(color.z - (0.5f + 0.5f * p.z)));
        }
    }
    CHECK(worst < 0.35f);  // a coarse vertex sits on the average of the two it replaced
    const auto bytes = serialize(d);
    Data back;
    REQUIRE(deserialize(bytes.data(), bytes.size(), back));
    CHECK(back.colors.size() == d.colors.size());
    CHECK(serialize(back) == bytes);
    // No colours given: none stored.
    CHECK(build(m.positions, m.indices).colors.empty());
}

TEST_CASE("Micromesh: the build is deterministic and the bytes round-trip", "[micromesh][determinism]") {
    const Mesh m = icosphere(4);
    const Data a = build(m.positions, m.indices);
    const Data b = build(m.positions, m.indices);
    const auto bytesA = serialize(a);
    const auto bytesB = serialize(b);
    REQUIRE(!bytesA.empty());
    CHECK(bytesA == bytesB);  // byte-identical rebuilds

    Data back;
    REQUIRE(deserialize(bytesA.data(), bytesA.size(), back));
    CHECK(serialize(back) == bytesA);
    CHECK(back.clusters.size() == a.clusters.size());
    CHECK(selectCut(back, CutParams{0.01f, false, {}}) == selectCut(a, CutParams{0.01f, false, {}}));

    // Truncated or foreign bytes are refused, never read out of range.
    Data junk;
    CHECK_FALSE(deserialize(bytesA.data(), bytesA.size() / 2, junk));
    CHECK_FALSE(deserialize(bytesA.data(), 3, junk));
    std::vector<u8> wrong = bytesA;
    wrong[0] ^= 0xFF;
    CHECK_FALSE(deserialize(wrong.data(), wrong.size(), junk));
}

TEST_CASE("Micromesh: degenerate input does not crash", "[micromesh]") {
    CHECK(build({}, {}).clusters.empty());
    const std::vector<Vec3> p = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    CHECK(build(p, {0, 1, 2}).clusters.empty());  // a zero-area triangle
    CHECK(build(p, {0, 1, 9}).clusters.empty());  // an index out of range
    const std::vector<Vec3> one = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const Data tiny = build(one, {0, 1, 2});
    REQUIRE(tiny.clusters.size() == 1u);
    CHECK(tiny.clusters[0].parentError >= kRootError);
}

TEST_CASE("Micromesh: the C ABI builds the same bytes as the core module", "[micromesh][capi]") {
    const Mesh m = icosphere(3);
    std::vector<float> flat;
    for (const Vec3& p : m.positions) flat.insert(flat.end(), {p.x, p.y, p.z});
    CaffeineMicromeshStats stats{};
    uint8_t* bytes = nullptr;
    size_t size = 0;
    REQUIRE(caffeine_micromesh_build(flat.data(), static_cast<uint32_t>(m.positions.size()), m.indices.data(),
                                     static_cast<uint32_t>(m.indices.size()), &stats, &bytes, &size) == CAFFEINE_OK);
    REQUIRE(bytes != nullptr);
    const auto expected = serialize(build(m.positions, m.indices));
    CHECK(std::vector<u8>(bytes, bytes + size) == expected);
    CHECK(stats.source_triangles == 1280u);
    CHECK(stats.root_clusters == 1u);
    CHECK(stats.max_cluster_triangles <= 128u);
    CHECK(stats.levels >= 3u);
    CHECK(stats.max_error > 0.0f);
    caffeine_session_free(bytes);
    // Bad arguments are refused.
    CHECK(caffeine_micromesh_build(nullptr, 3, m.indices.data(), 3, nullptr, &bytes, &size) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_micromesh_build(flat.data(), 3, m.indices.data(), 2, nullptr, &bytes, &size) == CAFFEINE_INVALID_ARGUMENT);
}
