#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "math/Mat4.hpp"
#include "math/Math.hpp"
#include "ecs/Entity.hpp"

#include <memory>
#include <vector>
#include <cmath>

namespace Caffeine::Spatial {
using namespace Caffeine;

// ============================================================================
// AABB 3D — axis-aligned bounding box
// ============================================================================
struct AABB3D {
    Vec3 min = {0, 0, 0};
    Vec3 max = {0, 0, 0};

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size()   const { return max - min; }

    bool contains(Vec3 p) const {
        return (p.x >= min.x && p.x <= max.x &&
                p.y >= min.y && p.y <= max.y &&
                p.z >= min.z && p.z <= max.z);
    }

    bool contains(const AABB3D& o) const {
        return (o.min.x >= min.x && o.max.x <= max.x &&
                o.min.y >= min.y && o.max.y <= max.y &&
                o.min.z >= min.z && o.max.z <= max.z);
    }

    bool intersects(const AABB3D& o) const {
        return (min.x <= o.max.x && max.x >= o.min.x &&
                min.y <= o.max.y && max.y >= o.min.y &&
                min.z <= o.max.z && max.z >= o.min.z);
    }

    // Ray-AABB using the slab method.
    bool intersectsRay(Vec3 origin, Vec3 dir, f32* tMinOut = nullptr) const {
        f32 tMin = -1e30f;
        f32 tMax =  1e30f;

        for (u32 axis = 0; axis < 3; ++axis) {
            f32 o = (&origin.x)[axis];
            f32 d = (&dir.x)[axis];
            f32 mn = (&min.x)[axis];
            f32 mx = (&max.x)[axis];

            if (Math::absf(d) < 1e-8f) {
                if (o < mn || o > mx) return false;
            } else {
                f32 t1 = (mn - o) / d;
                f32 t2 = (mx - o) / d;
                if (t1 > t2) { f32 tmp = t1; t1 = t2; t2 = tmp; }
                if (t1 > tMin) tMin = t1;
                if (t2 < tMax) tMax = t2;
                if (tMin > tMax || tMax < 0.0f) return false;
            }
        }

        if (tMinOut) *tMinOut = (tMin > 0.0f) ? tMin : 0.0f;
        return true;
    }
};


// ============================================================================
// Frustum for culling.
//
// Planes stored as (A,B,C,D) where Ax+By+Cz+D=0.
// Our convention: d <= 0 is inside, d > 0 is outside.
//
// Use fromCamera() for robust construction from camera parameters.
// fromMatrix() requires a standard OpenGL-style projection matrix.
// ============================================================================
struct Frustum {
    enum Plane { Left = 0, Right, Bottom, Top, Near, Far, PlaneCount };
    Vec4 planes[PlaneCount];

    static Frustum fromMatrix(const Mat4& viewProj) {
        Frustum f;

        Vec4 row0(viewProj(0,0), viewProj(0,1), viewProj(0,2), viewProj(0,3));
        Vec4 row1(viewProj(1,0), viewProj(1,1), viewProj(1,2), viewProj(1,3));
        Vec4 row2(viewProj(2,0), viewProj(2,1), viewProj(2,2), viewProj(2,3));
        Vec4 row3(viewProj(3,0), viewProj(3,1), viewProj(3,2), viewProj(3,3));

        // Gribb & Hartmann: (row3 ± rowN) gives d >= 0 = inside.
        // Negate components for our d <= 0 = inside convention.
        auto neg = [](const Vec4& v) { return Vec4(-v.x, -v.y, -v.z, -v.w); };
        f.planes[Left]   = neg(row3 + row0);
        f.planes[Right]  = neg(row3 - row0);
        f.planes[Bottom] = neg(row3 + row1);
        f.planes[Top]    = neg(row3 - row1);
        f.planes[Near]   = neg(row3 + row2);
        f.planes[Far]    = neg(row3 - row2);

        for (u32 i = 0; i < PlaneCount; ++i) {
            Vec4& p = f.planes[i];
            f32 len = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 1e-8f) {
                f32 inv = 1.0f / len;
                p.x *= inv; p.y *= inv; p.z *= inv; p.w *= inv;
            }
        }
        return f;
    }

    static Frustum fromCamera(Vec3 eye, Vec3 target, Vec3 up,
                               f32 fovY, f32 aspect, f32 nearZ, f32 farZ) {
        Frustum f;

        Vec3 forward = (target - eye).normalized();
        Vec3 right   = forward.cross(up).normalized();
        Vec3 upDir   = right.cross(forward);

        f32 tanHalf = tanf(fovY * 0.5f);
        f32 tanH    = tanHalf * aspect; // horizontal half-tan

        Vec3 nc = eye + forward * nearZ;
        Vec3 fc = eye + forward * farZ;

        // Helper: make a plane from a point and inward (standard) normal,
        // then negate so our d <= 0 = inside convention holds.
        auto makePlane = [](Vec3 pointOnPlane, Vec3 inwardNormal) -> Vec4 {
            f32 D = -inwardNormal.dot(pointOnPlane);
            f32 len = inwardNormal.length();
            return {-inwardNormal.x / len, -inwardNormal.y / len,
                    -inwardNormal.z / len, -D / len};
        };

        f.planes[Near] = makePlane(nc, forward);
        f.planes[Far]  = makePlane(fc, Vec3(-forward.x, -forward.y, -forward.z));

        // Side plane inward normals derived from view-space frustum geometry.
        // In view space (camera at origin, looking down -Z):
        //   Left:  x - z*tanH  = 0 → inward normal = ( 1, 0, -tanH)
        //   Right: x + z*tanH  = 0 → inward normal = (-1, 0, -tanH)
        //   Top:   y + z*tanY  = 0 → inward normal = ( 0, -1, -tanY)
        //   Bottom: y - z*tanY = 0 → inward normal = ( 0,  1, -tanY)
        // Transform to world via: n_world = R^T * n_view, where R^T columns are
        // (right, upDir, -forward). All side planes pass through eye.
        auto sidePlane = [&](Vec3 nView) -> Vec4 {
            Vec3 nw = right * nView.x + upDir * nView.y + Vec3(-forward.x, -forward.y, -forward.z) * nView.z;
            f32 len = nw.length();
            f32 D = -nw.dot(eye);
            return {-nw.x / len, -nw.y / len, -nw.z / len, -D / len};
        };

        f.planes[Left]   = sidePlane(Vec3( 1.0f,  0.0f, -tanH));
        f.planes[Right]  = sidePlane(Vec3(-1.0f,  0.0f, -tanH));
        f.planes[Top]    = sidePlane(Vec3( 0.0f, -1.0f, -tanHalf));
        f.planes[Bottom] = sidePlane(Vec3( 0.0f,  1.0f, -tanHalf));

        return f;
    }

    bool contains(Vec3 point) const {
        for (u32 i = 0; i < PlaneCount; ++i) {
            f32 d = planes[i].x * point.x +
                    planes[i].y * point.y +
                    planes[i].z * point.z +
                    planes[i].w;
            if (d > 0.0f) return false;
        }
        return true;
    }

    bool intersects(const AABB3D& aabb) const {
        for (u32 i = 0; i < PlaneCount; ++i) {
            const Vec4& p = planes[i];
            // p-vertex: the AABB corner with the LEAST signed distance to the
            // plane (furthest inside for our d <= 0 = inside convention).
            f32 px = p.x >= 0.0f ? aabb.min.x : aabb.max.x;
            f32 py = p.y >= 0.0f ? aabb.min.y : aabb.max.y;
            f32 pz = p.z >= 0.0f ? aabb.min.z : aabb.max.z;
            if (p.x * px + p.y * py + p.z * pz + p.w > 0.0f)
                return false;
        }
        return true;
    }

    bool containsSphere(Vec3 center, f32 radius) const {
        for (u32 i = 0; i < PlaneCount; ++i) {
            f32 d = planes[i].x * center.x +
                    planes[i].y * center.y +
                    planes[i].z * center.z +
                    planes[i].w;
            if (d - radius > 0.0f) return false;
        }
        return true;
    }
};


// ============================================================================
// Octree for 3D spatial partitioning.
//
// Each node subdivides into 8 children when exceeding maxEntitiesPerNode.
// Entities are inserted by AABB center point and stored in leaf nodes.
// ============================================================================
class Octree {
public:
    struct Config {
        AABB3D rootBounds;
        u32    maxEntitiesPerNode = 8;
        u32    maxDepth           = 8;
    };

    explicit Octree(const Config& cfg) : m_config(cfg) {
        m_root.bounds = cfg.rootBounds;
        m_root.depth  = 0;
    }

    // ── Insert / Remove / Update ───────────────────────────────

    void insert(ECS::Entity entity, const AABB3D& bounds) {
        insertRecursive(&m_root, entity, bounds);
    }

    void remove(ECS::Entity entity) {
        removeRecursive(&m_root, entity);
    }

    void update(ECS::Entity entity, const AABB3D& newBounds) {
        remove(entity);
        insert(entity, newBounds);
    }

    void rebuild() {
        std::vector<Entry> all;
        collectEntries(&m_root, all);

        m_root = Node();
        m_root.bounds = m_config.rootBounds;
        m_root.depth  = 0;

        for (auto& e : all) {
            insertRecursive(&m_root, e.entity, e.bounds);
        }
    }

    // ── Queries ────────────────────────────────────────────────

    void queryAABB(const AABB3D& queryBounds,
                   std::vector<ECS::Entity>& out) const {
        queryAABBRecursive(&m_root, queryBounds, out);
    }

    void queryFrustum(const Frustum& frustum,
                      std::vector<ECS::Entity>& out) const {
        queryFrustumRecursive(&m_root, frustum, out);
    }

    void queryRay(Vec3 origin, Vec3 dir, f32 maxDist,
                  std::vector<ECS::Entity>& out) const {
        queryRayRecursive(&m_root, origin, dir, maxDist, out);
    }

    void queryRadius(Vec3 center, f32 radius,
                     std::vector<ECS::Entity>& out) const {
        queryRadiusRecursive(&m_root, center, radius, out);
    }

    // ── Stats ──────────────────────────────────────────────────

    u32 nodeCount()   const { return nodeCountRecursive(&m_root); }
    u32 entityCount() const { return entityCountRecursive(&m_root); }
    u32 depth()       const { return depthRecursive(&m_root); }

private:
    struct Entry { ECS::Entity entity; AABB3D bounds; };

    struct Node {
        AABB3D                         bounds;
        std::vector<Entry>             entries;
        std::unique_ptr<Node>          children[8]{};
        bool                           isLeaf = true;
        u32                            depth  = 0;

        int childIndex(Vec3 point) const {
            Vec3 c = bounds.center();
            int idx = 0;
            if (point.x >= c.x) idx |= 1;
            if (point.y >= c.y) idx |= 2;
            if (point.z >= c.z) idx |= 4;
            return idx;
        }

        void subdivide() {
            Vec3 c = bounds.center();
            for (int i = 0; i < 8; ++i) {
                auto ch = std::make_unique<Node>();
                ch->bounds.min = {
                    (i & 1) ? c.x : bounds.min.x,
                    (i & 2) ? c.y : bounds.min.y,
                    (i & 4) ? c.z : bounds.min.z
                };
                ch->bounds.max = {
                    (i & 1) ? bounds.max.x : c.x,
                    (i & 2) ? bounds.max.y : c.y,
                    (i & 4) ? bounds.max.z : c.z
                };
                ch->depth  = depth + 1;
                ch->isLeaf = true;
                children[i] = std::move(ch);
            }
            isLeaf = false;
        }
    };

    Node   m_root;
    Config m_config;

    // ── Recursive helpers ──────────────────────────────────────

    void insertRecursive(Node* node, ECS::Entity entity, const AABB3D& bounds) {
        if (!node->isLeaf) {
            int idx = node->childIndex(bounds.center());
            insertRecursive(node->children[idx].get(), entity, bounds);
            return;
        }

        node->entries.push_back({entity, bounds});

        if (node->entries.size() > m_config.maxEntitiesPerNode &&
            node->depth < m_config.maxDepth) {
            node->subdivide();
            auto old = std::move(node->entries);
            for (auto& e : old) {
                int idx = node->childIndex(e.bounds.center());
                node->children[idx]->entries.push_back(std::move(e));
            }
        }
    }

    static bool removeRecursive(Node* node, ECS::Entity entity) {
        if (node->isLeaf) {
            for (auto it = node->entries.begin(); it != node->entries.end(); ++it) {
                if (it->entity == entity) {
                    node->entries.erase(it);
                    return true;
                }
            }
            return false;
        }

        for (u32 i = 0; i < 8; ++i) {
            if (node->children[i] && removeRecursive(node->children[i].get(), entity))
                return true;
        }
        return false;
    }

    static void collectEntries(Node* node, std::vector<Entry>& out) {
        for (auto& e : node->entries)
            out.push_back(e);
        if (!node->isLeaf) {
            for (u32 i = 0; i < 8; ++i) {
                if (node->children[i])
                    collectEntries(node->children[i].get(), out);
            }
        }
    }

    static void queryAABBRecursive(const Node* node, const AABB3D& q,
                                   std::vector<ECS::Entity>& out) {
        if (!node->bounds.intersects(q)) return;

        if (node->isLeaf) {
            for (auto& e : node->entries) {
                if (e.bounds.intersects(q))
                    out.push_back(e.entity);
            }
            return;
        }

        for (u32 i = 0; i < 8; ++i) {
            if (node->children[i])
                queryAABBRecursive(node->children[i].get(), q, out);
        }
    }

    static void queryFrustumRecursive(const Node* node, const Frustum& f,
                                      std::vector<ECS::Entity>& out) {
        if (!f.intersects(node->bounds)) return;

        if (node->isLeaf) {
            for (auto& e : node->entries) {
                if (f.intersects(e.bounds))
                    out.push_back(e.entity);
            }
            return;
        }

        for (u32 i = 0; i < 8; ++i) {
            if (node->children[i])
                queryFrustumRecursive(node->children[i].get(), f, out);
        }
    }

    static void queryRayRecursive(const Node* node, Vec3 origin, Vec3 dir,
                                  f32 maxDist, std::vector<ECS::Entity>& out) {
        f32 tHit;
        if (!node->bounds.intersectsRay(origin, dir, &tHit) || tHit > maxDist)
            return;

        if (node->isLeaf) {
            for (auto& e : node->entries) {
                f32 tE;
                if (e.bounds.intersectsRay(origin, dir, &tE) && tE <= maxDist)
                    out.push_back(e.entity);
            }
            return;
        }

        for (u32 i = 0; i < 8; ++i) {
            if (node->children[i])
                queryRayRecursive(node->children[i].get(), origin, dir, maxDist, out);
        }
    }

    static void queryRadiusRecursive(const Node* node, Vec3 center,
                                     f32 radius, std::vector<ECS::Entity>& out) {
        Vec3 closest(
            Math::clamp(center.x, node->bounds.min.x, node->bounds.max.x),
            Math::clamp(center.y, node->bounds.min.y, node->bounds.max.y),
            Math::clamp(center.z, node->bounds.min.z, node->bounds.max.z)
        );
        if ((center - closest).lengthSquared() > radius * radius)
            return;

        if (node->isLeaf) {
            for (auto& e : node->entries) {
                Vec3 ec(
                    Math::clamp(center.x, e.bounds.min.x, e.bounds.max.x),
                    Math::clamp(center.y, e.bounds.min.y, e.bounds.max.y),
                    Math::clamp(center.z, e.bounds.min.z, e.bounds.max.z)
                );
                if ((center - ec).lengthSquared() <= radius * radius)
                    out.push_back(e.entity);
            }
            return;
        }

        for (u32 i = 0; i < 8; ++i) {
            if (node->children[i])
                queryRadiusRecursive(node->children[i].get(), center, radius, out);
        }
    }

    static u32 nodeCountRecursive(const Node* node) {
        u32 count = 1;
        if (!node->isLeaf) {
            for (u32 i = 0; i < 8; ++i)
                if (node->children[i])
                    count += nodeCountRecursive(node->children[i].get());
        }
        return count;
    }

    static u32 entityCountRecursive(const Node* node) {
        u32 count = (u32)node->entries.size();
        if (!node->isLeaf) {
            for (u32 i = 0; i < 8; ++i)
                if (node->children[i])
                    count += entityCountRecursive(node->children[i].get());
        }
        return count;
    }

    static u32 depthRecursive(const Node* node) {
        if (node->isLeaf) return node->depth;
        u32 md = 0;
        for (u32 i = 0; i < 8; ++i)
            if (node->children[i]) {
                u32 cd = depthRecursive(node->children[i].get());
                if (cd > md) md = cd;
            }
        return md;
    }
};

} // namespace Caffeine::Spatial
