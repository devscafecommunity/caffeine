#pragma once

#include "math/Mat4.hpp"
#include "math/Vec3.hpp"

#include <algorithm>
#include <vector>

namespace Caffeine::Render {

struct OcclusionPrimitive {
    Vec3 aabbMin{};
    Vec3 aabbMax{};
    bool canOcclude = false;
    bool canBeOccluded = false;
};

struct ScreenOccluder {
    f32 minX = 0.0f;
    f32 minY = 0.0f;
    f32 maxX = 0.0f;
    f32 maxY = 0.0f;
    f32 maxDepth = 0.0f;
    f32 area = 0.0f;
};

/// Project an AABB. Returns false when a corner is behind the camera.
inline bool projectAabb(const Mat4& vp, const Vec3& cameraPos, const Vec3& cameraForward,
                        const Vec3& aabbMin, const Vec3& aabbMax, ScreenOccluder& out) {
    const Vec3 corners[8] = {
        {aabbMin.x, aabbMin.y, aabbMin.z}, {aabbMax.x, aabbMin.y, aabbMin.z},
        {aabbMin.x, aabbMax.y, aabbMin.z}, {aabbMax.x, aabbMax.y, aabbMin.z},
        {aabbMin.x, aabbMin.y, aabbMax.z}, {aabbMax.x, aabbMin.y, aabbMax.z},
        {aabbMin.x, aabbMax.y, aabbMax.z}, {aabbMax.x, aabbMax.y, aabbMax.z},
    };
    f32 minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    f32 minDepth = 1e9f, maxDepth = -1e9f;
    for (const Vec3& corner : corners) {
        const Vec4 clip = vp.transformVec4(Vec4(corner.x, corner.y, corner.z, 1.0f));
        if (clip.w <= 0.1f) return false;
        const f32 ndcX = clip.x / clip.w;
        const f32 ndcY = clip.y / clip.w;
        minX = std::min(minX, ndcX);
        minY = std::min(minY, ndcY);
        maxX = std::max(maxX, ndcX);
        maxY = std::max(maxY, ndcY);
        const f32 depth = (corner - cameraPos).dot(cameraForward);
        minDepth = std::min(minDepth, depth);
        maxDepth = std::max(maxDepth, depth);
    }
    if (maxDepth <= 0.05f) return false;
    out.minX = minX;
    out.minY = minY;
    out.maxX = maxX;
    out.maxY = maxY;
    out.maxDepth = maxDepth;
    out.area = std::max(0.0f, maxX - minX) * std::max(0.0f, maxY - minY);
    (void)minDepth;
    return true;
}

/// Drops primitives fully behind a closer occluder AABB. The screen test uses
/// the occluder box, so a thin mesh can hide neighbors inside that box.
inline std::vector<u32> visibleAfterCoarseOcclusion(const std::vector<OcclusionPrimitive>& prims,
                                                    const Mat4& vp, const Vec3& cameraPos,
                                                    const Vec3& cameraForward,
                                                    u32 maxOccluders) {
    std::vector<u32> visible;
    visible.reserve(prims.size());
    if (maxOccluders == 0) {
        for (u32 i = 0; i < prims.size(); ++i) visible.push_back(i);
        return visible;
    }

    struct Projected {
        bool ok = false;
        ScreenOccluder screen{};
        f32 minDepth = 0.0f;
    };
    std::vector<Projected> projected(prims.size());
    std::vector<u32> occluderOrder;
    for (u32 i = 0; i < prims.size(); ++i) {
        const OcclusionPrimitive& prim = prims[i];
        Projected& proj = projected[i];
        proj.ok = projectAabb(vp, cameraPos, cameraForward, prim.aabbMin, prim.aabbMax, proj.screen);
        if (!proj.ok) continue;
        f32 minDepth = 1e9f;
        const Vec3 corners[8] = {
            {prim.aabbMin.x, prim.aabbMin.y, prim.aabbMin.z},
            {prim.aabbMax.x, prim.aabbMin.y, prim.aabbMin.z},
            {prim.aabbMin.x, prim.aabbMax.y, prim.aabbMin.z},
            {prim.aabbMax.x, prim.aabbMax.y, prim.aabbMin.z},
            {prim.aabbMin.x, prim.aabbMin.y, prim.aabbMax.z},
            {prim.aabbMax.x, prim.aabbMin.y, prim.aabbMax.z},
            {prim.aabbMin.x, prim.aabbMax.y, prim.aabbMax.z},
            {prim.aabbMax.x, prim.aabbMax.y, prim.aabbMax.z},
        };
        for (const Vec3& corner : corners) {
            minDepth = std::min(minDepth, (corner - cameraPos).dot(cameraForward));
        }
        proj.minDepth = minDepth;
        if (prim.canOcclude && proj.screen.area > 0.02f) occluderOrder.push_back(i);
    }

    std::sort(occluderOrder.begin(), occluderOrder.end(), [&](u32 a, u32 b) {
        return projected[a].screen.area > projected[b].screen.area;
    });
    if (occluderOrder.size() > maxOccluders) occluderOrder.resize(maxOccluders);

    for (u32 i = 0; i < prims.size(); ++i) {
        const Projected& obj = projected[i];
        bool hidden = false;
        if (prims[i].canBeOccluded && obj.ok) {
            for (u32 occIndex : occluderOrder) {
                if (occIndex == i) continue;
                const Projected& occ = projected[occIndex];
                if (!occ.ok) continue;
                const bool inside = obj.screen.minX >= occ.screen.minX &&
                                    obj.screen.maxX <= occ.screen.maxX &&
                                    obj.screen.minY >= occ.screen.minY &&
                                    obj.screen.maxY <= occ.screen.maxY;
                if (inside && obj.minDepth > occ.screen.maxDepth + 0.15f) {
                    hidden = true;
                    break;
                }
            }
        }
        if (!hidden) visible.push_back(i);
    }
    return visible;
}

}  // namespace Caffeine::Render
