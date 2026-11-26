#pragma once

// The draw path of an instance bake (`.cinst`, see WorldInstanceFile.hpp): one entity with a `World Instances`
// component draws every instance of a bake. This file holds the pure part, testable without a GPU:
//
//   - the models file, which says which mesh parts draw each (class, variant) or each unique instance (cell);
//   - the instance matrix (the convention of `caffeine_session_scatter_terrain`: up aligned from +Y by the
//     shortest arc, then yaw about the up, then uniform scale);
//   - chunk bounds and chunk culling (frustum and draw distance), computed once per loaded bake;
//   - the per-instance modifiers, quantised so instances keep sharing instanced draws;
//   - a cache of loaded bakes and models files keyed by resolved path and modification time.
//
// GpuSceneRenderer turns the visible instances into ordinary mesh draws; its automatic batching groups them into
// instanced draws. Next step (not done here): impostors for far chunks, and per-chunk instance buffers kept on
// the GPU instead of a draw per instance and part every frame.
//
// Models file, one line per model part (`#` comments and blank lines are ignored):
//
//   <class index> <variant> <mesh path> [slots a.mat;b.mat] [cutout on|off]
//   <class index> cell <cx> <cz> <mesh path> [slots a.mat;b.mat] [cutout on|off]
//
// Several lines with the same key are the parts of one model (a tree: trunk part + leaves part). A cell line is the
// model of the one instance of that class at that cell (unique or parameter-driven bakes) and wins over the
// variant lines. Paths resolve like the Mesh Filter's `Mesh` (see `resolveWorldAssetPath`). A cutout part does
// not cast shadows.

#include "core/Types.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "math/Vec3.hpp"
#include "spatial/Octree.hpp"
#include "world/WorldInstanceFile.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caffeine::World {

struct InstanceModelPart {
    std::string meshPath;
    std::string slots;   ///< material slots, `a.mat;b.mat` (MeshFilterComponent::slotMaterials); empty = mesh's own
    bool cutout = false; ///< alpha-tested part (leaves): drawn, but casts no shadow
};

struct InstanceModels {
    std::map<std::pair<u32, u32>, std::vector<InstanceModelPart>> variants;        ///< (class, variant)
    std::map<std::tuple<u32, i32, i32>, std::vector<InstanceModelPart>> cells;      ///< (class, cell x, cell z)

    /// The parts that draw `record`: its cell entry, else its (class, variant) entry, else none.
    const std::vector<InstanceModelPart>* partsFor(const InstanceRecord& record) const;
    bool empty() const { return variants.empty() && cells.empty(); }
};

/// False (and the first error, with its line number, in `error`) for a malformed line.
bool parseInstanceModels(const std::string& text, InstanceModels& out, std::string* error = nullptr);
bool readInstanceModels(const std::filesystem::path& path, InstanceModels& out, std::string* error = nullptr);

/// The rotation of an instance: `up` reached from +Y by the shortest arc, after `yaw` radians about +Y.
Quat instanceRotation(const std::array<f32, 3>& up, f32 yaw);
/// translate(position) x rotation x scale, in the space of the bake (metres from the terrain corner).
Mat4 instanceLocalMatrix(const InstanceRecord& record);

/// Per-instance modifiers enter the batch key of the instanced draws: quantised to 1/32 steps, neighbouring
/// tints become equal and the instances keep sharing one draw.
inline constexpr f32 kInstanceModifierStep = 1.0f / 32.0f;
f32 quantiseInstanceModifier(f32 value);
Vec3 quantisedInstanceTint(const InstanceRecord& record);

/// The extent of a chunk's instance origins (bake space) and its largest instance scale.
struct InstanceChunkBounds {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};
    f32 maxScale = 0.0f;
    bool empty = true;
};
std::vector<InstanceChunkBounds> computeChunkBounds(const InstanceFile& file);

/// The world box of a chunk: its origins grown by `modelRadius` x its largest scale (the farthest point of any
/// model from its pivot), through `world` (the entity's world matrix).
Spatial::AABB3D chunkWorldBounds(const InstanceChunkBounds& bounds, const Mat4& world, f32 modelRadius);
f32 distanceToBox(const Vec3& point, const Spatial::AABB3D& box);
/// Whether a chunk can be seen: inside the frustum (when given) and, with `drawDistance` > 0, no farther than
/// that from `camera`.
bool chunkVisible(const InstanceChunkBounds& bounds, const Mat4& world, f32 modelRadius,
                  const Spatial::Frustum* frustum, const Vec3& camera, f32 drawDistance);

/// `path` resolved the way MeshCache resolves a Mesh Filter's `Mesh`: the first existing candidate of
/// MeshCache::buildCandidatePaths(path, projectRoot); empty when none exists.
std::string resolveWorldAssetPath(const std::string& path, const std::string& projectRoot);

/// A loaded bake and models file, ready to draw.
struct WorldInstanceSet {
    InstanceFile bake;
    std::vector<InstanceChunkBounds> chunkBounds;
    InstanceModels models;
    std::string error; ///< why the bake or the models did not load (they are then empty)
};

/// Bakes and models files, loaded once and reloaded when their resolved path or modification time changes.
class WorldInstanceCache {
public:
    static WorldInstanceCache& instance();

    /// The set of `bakePath` + `modelsPath` (resolved against `projectRoot`); never null.
    const WorldInstanceSet& get(const std::string& bakePath, const std::string& modelsPath,
                                const std::string& projectRoot);
    /// How many times a file was read (tests: a second get of unchanged files reads nothing).
    u64 loads() const { return m_loads; }
    void clear();

private:
    struct FileStamp {
        std::string resolved;
        std::filesystem::file_time_type time{};
        bool operator==(const FileStamp& o) const { return resolved == o.resolved && time == o.time; }
    };
    struct Entry {
        FileStamp bake;
        FileStamp models;
        std::shared_ptr<WorldInstanceSet> set;
    };
    static FileStamp stampOf(const std::string& path, const std::string& projectRoot);

    std::unordered_map<std::string, Entry> m_entries;
    u64 m_loads = 0;
};

}  // namespace Caffeine::World
