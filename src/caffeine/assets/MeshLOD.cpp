#include "assets/MeshLOD.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Caffeine::Assets {

namespace {

struct CellKey {
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
    bool operator==(const CellKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct CellKeyHash {
    usize operator()(const CellKey& key) const {
        usize h = static_cast<usize>(key.x) * 73856093u;
        h ^= static_cast<usize>(key.y) * 19349663u;
        h ^= static_cast<usize>(key.z) * 83492791u;
        return h;
    }
};

}  // namespace

bool MeshLOD::buildClusteredLod(const Mesh3D& source, Mesh3D& target, f32 cellSize) {
    target = Mesh3D{};
    if (source.vertices.empty() || source.indices.size() < 3 || cellSize <= 0.0f) return false;

    auto quantize = [](f32 v, f32 cell) {
        return static_cast<i32>(std::floor(v / cell));
    };

    struct Accum {
        u32 index = 0;
        u32 count = 0;
        Vec3 position{};
        Vec3 normal{};
        Vec2 uv{};
        Vec4 tangent{};
    };
    std::unordered_map<CellKey, Accum, CellKeyHash> cells;
    cells.reserve(source.vertices.size() / 2);
    std::vector<u32> remap(source.vertices.size(), 0);

    for (u32 i = 0; i < source.vertices.size(); ++i) {
        const Vertex3D& v = source.vertices[i];
        const CellKey key{quantize(v.position.x, cellSize), quantize(v.position.y, cellSize),
                          quantize(v.position.z, cellSize)};
        auto [it, inserted] = cells.try_emplace(key);
        Accum& acc = it->second;
        if (inserted) acc.index = static_cast<u32>(cells.size() - 1);
        acc.count++;
        acc.position += v.position;
        acc.normal += v.normal;
        acc.uv += v.texcoord;
        acc.tangent = acc.tangent + v.tangent;
        remap[i] = acc.index;
    }

    target.vertices.resize(cells.size());
    for (auto& [_, acc] : cells) {
        const f32 inv = 1.0f / static_cast<f32>(std::max(acc.count, 1u));
        Vertex3D& out = target.vertices[acc.index];
        out.position = acc.position * inv;
        Vec3 n = acc.normal * inv;
        const f32 len = n.length();
        out.normal = (len > 1e-6f) ? n / len : Vec3(0.0f, 1.0f, 0.0f);
        out.texcoord = acc.uv * inv;
        out.tangent = acc.tangent * inv;
    }

    target.indices.reserve(source.indices.size());
    for (u32 i = 0; i + 2 < source.indices.size(); i += 3) {
        const u32 i0 = source.indices[i];
        const u32 i1 = source.indices[i + 1];
        const u32 i2 = source.indices[i + 2];
        if (i0 >= remap.size() || i1 >= remap.size() || i2 >= remap.size()) continue;
        const u32 a = remap[i0];
        const u32 b = remap[i1];
        const u32 c = remap[i2];
        if (a == b || b == c || c == a) continue;
        target.indices.push_back(a);
        target.indices.push_back(b);
        target.indices.push_back(c);
    }

    if (target.indices.size() < 3 || target.indices.size() >= source.indices.size()) {
        target = Mesh3D{};
        return false;
    }

    target.bounds = source.bounds;
    target.flipTextureV = source.flipTextureV;
    target.lodCount = 1;
    return true;
}

void MeshLOD::generateLODs(Mesh3D* mesh, int lodCount, f32 reductionRatio) {
    if (!mesh || mesh->vertices.empty() || lodCount < 1) return;
    mesh->lodCount = static_cast<u32>(lodCount);
    if (reductionRatio <= 0.0f || reductionRatio >= 1.0f) return;

    const Vec3 extent = mesh->bounds.max - mesh->bounds.min;
    const f32 diagonal = std::max(extent.length(), 0.001f);
    Mesh3D simplified;
    if (buildClusteredLod(*mesh, simplified, diagonal * (1.0f - reductionRatio) * 0.05f)) {
        mesh->lodCount = std::max(mesh->lodCount, 2u);
    }
}

void MeshLOD::simplifyMesh(const Mesh3D& source, Mesh3D& target, f32 targetRatio) {
    const Vec3 extent = source.bounds.max - source.bounds.min;
    const f32 diagonal = std::max(extent.length(), 0.001f);
    const f32 cell = diagonal * std::clamp(1.0f - targetRatio, 0.01f, 0.5f);
    if (!buildClusteredLod(source, target, cell)) {
        target = source;
    }
}

}  // namespace Caffeine::Assets
