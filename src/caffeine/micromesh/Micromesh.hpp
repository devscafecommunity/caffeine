#pragma once

// Micromeshes: a mesh cut into clusters of at most 128 triangles and a hierarchy of simplified
// levels, built offline (never at game time) so the renderer can pick, per view, the coarsest cut
// whose error is invisible. Our own design: public cluster-hierarchy literature only.
//
// Build: the source triangles are sorted along a Morton curve and split into clusters; clusters are
// grouped (4 at a time), each group is merged, simplified by quadric edge collapse **with the
// group's boundary locked**, and split again; this repeats until one cluster is left. A group's
// error is never smaller than its children's (strictly bigger by an epsilon) and its bounding
// sphere contains theirs, so for any threshold the selected clusters (own error <= threshold <
// parent error) tile the surface without cracks: a cluster and its parent are never both chosen,
// and the boundaries between groups were locked at every level.
//
// Everything is deterministic: the same input gives the same bytes on every platform and run.

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

#include <vector>

namespace Caffeine::Micromesh {

constexpr u32 kMaxClusterTriangles = 128;
constexpr u32 kMaxClusterVertices = 128;
/// `parentError` of a root cluster: it is always selected when nothing coarser exists.
constexpr f32 kRootError = 3.4028235e38f;

struct Sphere {
    Vec3 center;
    f32 radius = 0.0f;
};

struct Cluster {
    /// Into `Data::vertexIndices` (global vertex ids, in local order) and `Data::triangles`
    /// (three local indices per triangle, one byte each).
    u32 firstVertex = 0;
    u32 vertexCount = 0;
    u32 firstTriangle = 0;
    u32 triangleCount = 0;
    u32 level = 0;          ///< 0 = source detail
    u32 group = 0;          ///< the group that produced it (level 0: its own index)
    /// Tight bounds, for frustum and occlusion tests.
    Sphere bounds;
    /// Back-face cone: the cluster faces away from a camera when dot(toCamera, axis) < -sqrt(1 - cutoff^2).
    Vec3 coneAxis{0.0f, 1.0f, 0.0f};
    f32 coneCutoff = -1.0f;  ///< cos of the cone's half angle; <= 0 means "never cull"
    /// Simplification error (world units) of this cluster relative to the source, and the sphere it
    /// is measured around. Level 0: error 0.
    f32 error = 0.0f;
    Sphere lodSphere;
    /// The same for the group this cluster was merged into (the next level's error). Roots:
    /// `kRootError`.
    f32 parentError = kRootError;
    Sphere parentLodSphere;
};

struct Data {
    /// Source vertices first, then the vertices the collapses created.
    std::vector<Vec3> positions;
    /// Unit shading normals, one per position: smooth normals of the source, and for a vertex a collapse
    /// created, the normalised sum of the two it replaced. A cut shades as one continuous surface.
    std::vector<Vec3> normals;
    /// Texture coordinates, one per position, or empty when the mesh had none. A collapsed vertex takes
    /// the coordinates of the closest point of the edge it replaced. UV seams are preserved: a seam is a
    /// boundary of split vertices, so it is locked at every level (a mesh with many seams simplifies less).
    std::vector<Vec2> uvs;

    /// Tangents (xyz unit, w = handedness), one per position, or empty. A collapsed vertex takes the
    /// normalised sum of the two it replaced and the handedness of the first.
    std::vector<Vec4> tangents;

    /// Vertex colours (rgb tint, a flutter weight), one per position, or empty. A collapsed vertex takes the
    /// average of the two it replaced.
    std::vector<Vec4> colors;
    std::vector<u32> vertexIndices;
    std::vector<u8> triangles;
    std::vector<Cluster> clusters;
    u32 levelCount = 0;
    u32 sourceTriangles = 0;

    u32 triangleCountOf(u32 cluster) const { return clusters[cluster].triangleCount; }
};

/// Builds the cluster hierarchy of an indexed triangle mesh. Degenerate triangles are dropped.
Data build(const std::vector<Vec3>& positions, const std::vector<u32>& indices);
/// The same with texture coordinates (one per position; ignored when the count differs). Vertices at a
/// UV seam must be split (distinct indices), as in any exported mesh.
Data build(const std::vector<Vec3>& positions, const std::vector<u32>& indices, const std::vector<Vec2>& uvs);

/// Per-vertex attributes to carry through the hierarchy (each parallel to `positions`; a missing or
/// mismatched one is ignored). `normals` replaces the smooth normals `build` would compute, so a mesh
/// keeps its own hard edges (split vertices).
struct Attributes {
    const std::vector<Vec3>* normals = nullptr;
    const std::vector<Vec2>* uvs = nullptr;
    const std::vector<Vec4>* tangents = nullptr;
    const std::vector<Vec4>* colors = nullptr;
};
Data build(const std::vector<Vec3>& positions, const std::vector<u32>& indices, const Attributes& attributes);

struct CutParams {
    /// Error threshold in world units (`viewDependent` false) or in world units per metre of
    /// distance, i.e. a projected error (`viewDependent` true).
    f32 threshold = 0.0f;
    bool viewDependent = false;
    Vec3 camera;
};

/// The clusters to draw: those whose error is within the threshold while their parent's is not.
/// Sorted by cluster index.
std::vector<u32> selectCut(const Data& data, const CutParams& params);

/// True when the whole cluster faces away from `camera`.
bool isBackFacing(const Cluster& cluster, const Vec3& camera);

/// Appends the cluster's triangles as global vertex indices.
void appendTriangles(const Data& data, u32 cluster, std::vector<u32>& outIndices);

/// Versioned, POD, little-endian bytes (the `.micromesh` section of a `.caf`); byte-identical for
/// identical input.
std::vector<u8> serialize(const Data& data);
bool deserialize(const u8* bytes, usize size, Data& out);

}  // namespace Caffeine::Micromesh
