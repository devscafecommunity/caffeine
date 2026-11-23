#pragma once

#include "animation/AnimationComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/ComponentQuery.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "math/Math.hpp"
#include "containers/FixedString.hpp"
#include "containers/HashMap.hpp"

#include <algorithm>
#include <vector>
#include <cmath>
#include <functional>

namespace Caffeine::Animation {

using namespace Caffeine;

// ============================================================================
// @brief Bone — nó da hierarquia esquelética.
// ============================================================================
struct Bone {
    FixedString<32> name;
    i32             parentIndex   = -1;
    Mat4            bindPoseInverse;
    Mat4            localTransform;
};

// ============================================================================
// @brief Esqueleto — hierarquia de bones.
// ============================================================================
struct Skeleton {
    std::vector<Bone> bones;
    /// Inverse of the skinned mesh node. Puts joint matrices back in mesh space.
    Mat4 skinSpace;

    u32 boneCount() const { return static_cast<u32>(bones.size()); }

    i32 findBone(const char* name) const {
        for (u32 i = 0; i < boneCount(); ++i) {
            if (bones[i].name == name) return static_cast<i32>(i);
        }
        return -1;
    }
};

/// Local transform of one bone: TRS when it has keys, or the raw bind-local matrix when it does not.
struct BoneLocal {
    Vec3 position { 0.0f, 0.0f, 0.0f };
    Quat rotation { Quat::identity() };
    Vec3 scale    { 1.0f, 1.0f, 1.0f };
    Mat4 raw      = Mat4::identity();
    bool isRaw    = false;
};

// ============================================================================
// @brief Keyframe de animação esquelética.
// ============================================================================
struct SkeletalKeyframe {
    f32  time  = 0.0f;
    Vec3 position { 0.0f, 0.0f, 0.0f };
    Quat rotation { Quat::identity() };
    Vec3 scale    { 1.0f, 1.0f, 1.0f };
};

// ============================================================================
// @brief Clipe de animação esquelética.
// ============================================================================
struct SkeletalClip {
    FixedString<32>  name;
    f32              duration = 0.0f;
    f32              fps      = 30.0f;
    bool             loop     = true;

    /// Canal por bone: bone_index -> lista de keyframes.
    HashMap<u32, std::vector<SkeletalKeyframe>> channels;

    /// Amostra o clipe no tempo `time` (segundos) e preenche `boneTransforms`
    /// com as matrizes ósseas finais (world * inverseBindPose).
    /// `boneTransforms` deve ter tamanho >= skeleton.boneCount().
    /// When `jointPositions` is set, it receives each joint in skeleton space.
    void sampleAt(f32 time, const Skeleton& skeleton, std::vector<Mat4>& boneTransforms,
                  std::vector<Vec3>* jointPositions = nullptr) const;

    /// Local pose of every bone at `time` (no hierarchy applied). Blending works on these, not on matrices.
    void sampleLocal(f32 time, const Skeleton& skeleton, std::vector<BoneLocal>& locals) const;
};

// ============================================================================
// @brief Blend tree — combina múltiplas animações com pesos.
// ============================================================================
struct BlendNode {
    enum class Type : u8 {
        Clip,
        Blend1D,
        Blend2D
    };

    Type type = Type::Clip;
    const SkeletalClip* clip = nullptr;
    std::vector<std::pair<f32, BlendNode*>> children;
    f32 parameter = 0.0f;
};

// ============================================================================
// Sampling and blending (the skinned pose path is SkinnedPose + AnimationSystem in SkinLibrary)
// ============================================================================

namespace Detail {

inline Vec3 lerpVec3(const Vec3& a, const Vec3& b, f32 t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

inline Mat4 buildTRS(const Vec3& pos, const Quat& rot, const Vec3& scale) {
    Mat4 t = Mat4::translation(pos.x, pos.y, pos.z);
    Mat4 r = rot.toMatrix();
    Mat4 s = Mat4::scale(scale.x, scale.y, scale.z);
    return t * r * s;
}

} // namespace Detail

inline Mat4 boneLocalMatrix(const BoneLocal& l) {
    return l.isRaw ? l.raw : Detail::buildTRS(l.position, l.rotation, l.scale);
}

/// Blends two local poses. TRS blends component-wise; a raw matrix blends element-wise.
inline BoneLocal lerpBoneLocal(const BoneLocal& a, const BoneLocal& b, f32 t) {
    BoneLocal out;
    if (a.isRaw || b.isRaw) {
        const Mat4 ma = boneLocalMatrix(a);
        const Mat4 mb = boneLocalMatrix(b);
        out.isRaw = true;
        for (u32 k = 0; k < 16; ++k) {
            out.raw.data()[k] = ma.data()[k] + (mb.data()[k] - ma.data()[k]) * t;
        }
        return out;
    }
    out.position = Detail::lerpVec3(a.position, b.position, t);
    out.rotation = Quat::slerp(a.rotation, b.rotation, t);
    out.scale    = Detail::lerpVec3(a.scale, b.scale, t);
    return out;
}


inline void composeBoneWorld(const Skeleton& skeleton, const std::vector<BoneLocal>& locals,
                            std::vector<Mat4>& world) {
    const u32 boneCount = skeleton.boneCount();
    world.assign(boneCount, Mat4::identity());
    std::vector<u8> resolved(boneCount, 0);
    // Parents may appear after children in imported joint lists.
    for (u32 pass = 0; pass < boneCount; ++pass) {
        bool progressed = false;
        for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
            if (resolved[boneIdx]) continue;
            const Bone& bone = skeleton.bones[boneIdx];
            const Mat4 localMatrix = boneIdx < locals.size()
                ? (locals[boneIdx].isRaw ? locals[boneIdx].raw
                                         : Detail::buildTRS(locals[boneIdx].position, locals[boneIdx].rotation,
                                                            locals[boneIdx].scale))
                : bone.localTransform;
            if (bone.parentIndex >= 0) {
                const u32 parent = static_cast<u32>(bone.parentIndex);
                if (parent >= boneCount || !resolved[parent]) continue;
                world[boneIdx] = world[parent] * localMatrix;
            } else {
                world[boneIdx] = localMatrix;
            }
            resolved[boneIdx] = 1;
            progressed = true;
        }
        if (!progressed) break;
    }
}

inline void SkeletalClip::sampleLocal(f32 time, const Skeleton& skeleton, std::vector<BoneLocal>& locals) const {
    const u32 boneCount = skeleton.boneCount();
    locals.assign(boneCount, BoneLocal{});

    // Wrap/clamp time
    f32 t = time;
    if (duration > 0.0f) {
        if (loop) {
            t = std::fmod(time, duration);
            if (t < 0.0f) t += duration;
        } else {
            t = (time < 0.0f) ? 0.0f : (time > duration ? duration : time);
        }
    }

    for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
        BoneLocal& out = locals[boneIdx];
        // Bones without keys keep the bind-pose local transform.
        out.isRaw = true;
        out.raw = skeleton.bones[boneIdx].localTransform;

        const std::vector<SkeletalKeyframe>* keys = channels.get(boneIdx);
        if (!keys || keys->empty()) continue;

        out.isRaw = false;
        const auto& kf = *keys;
        if (t <= kf[0].time) {
            out.position = kf[0].position;
            out.rotation = kf[0].rotation;
            out.scale    = kf[0].scale;
            continue;
        }
        if (t >= kf.back().time) {
            out.position = kf.back().position;
            out.rotation = kf.back().rotation;
            out.scale    = kf.back().scale;
            continue;
        }

        usize i = 0;
        for (; i + 1 < kf.size(); ++i) {
            if (kf[i + 1].time >= t) break;
        }
        const SkeletalKeyframe& k0 = kf[i];
        const SkeletalKeyframe& k1 = kf[i + 1];
        const f32 segDur = k1.time - k0.time;
        const f32 frac = (segDur > 0.0f) ? (t - k0.time) / segDur : 0.0f;
        out.position = Detail::lerpVec3(k0.position, k1.position, frac);
        out.rotation = Quat::slerp(k0.rotation, k1.rotation, frac);
        out.scale    = Detail::lerpVec3(k0.scale, k1.scale, frac);
    }
}

inline void SkeletalClip::sampleAt(f32 time, const Skeleton& skeleton, std::vector<Mat4>& boneTransforms,
                                  std::vector<Vec3>* jointPositions) const {
    const u32 boneCount = skeleton.boneCount();
    if (boneTransforms.size() < boneCount) {
        boneTransforms.resize(boneCount);
    }

    std::vector<BoneLocal> locals;
    sampleLocal(time, skeleton, locals);
    std::vector<Mat4> worldMatrices;
    composeBoneWorld(skeleton, locals, worldMatrices);

    for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
        boneTransforms[boneIdx] =
            skeleton.skinSpace * worldMatrices[boneIdx] * skeleton.bones[boneIdx].bindPoseInverse;
    }
    if (jointPositions) {
        jointPositions->resize(boneCount);
        for (u32 boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
            const Mat4& world = worldMatrices[boneIdx];
            (*jointPositions)[boneIdx] = skeleton.skinSpace.transformPoint(
                Vec3(world(0, 3), world(1, 3), world(2, 3)));
        }
    }
}

/// Adds `sample` into `accum`, scaled by `weight`. Blend trees sum their clips this way.
inline void accumulateBlend(std::vector<Mat4>& accum, const std::vector<Mat4>& sample, f32 weight);

/// Skinning matrices of a blend tree at `time`, multiplied by `weight` and added into `outResult`.
inline void sampleBlendTree(const BlendNode* node, f32 time, const Skeleton& skeleton,
                            std::vector<Mat4>& outResult, f32 weight);

/// Local pose of a blend node. Blend1D interpolates TRS between the two children around its parameter,
/// so rotations stay on the sphere instead of being averaged as matrices.
inline void sampleBlendLocal(const BlendNode* node, f32 time, const Skeleton& skeleton,
                             std::vector<BoneLocal>& out) {
    if (!node) return;
    switch (node->type) {
    case BlendNode::Type::Clip:
        if (node->clip) node->clip->sampleLocal(time, skeleton, out);
        return;

    case BlendNode::Type::Blend1D: {
        if (node->children.empty()) return;
        const f32 param = node->parameter;
        if (param <= node->children[0].first || node->children.size() == 1) {
            sampleBlendLocal(node->children[0].second, time, skeleton, out);
            return;
        }
        if (param >= node->children.back().first) {
            sampleBlendLocal(node->children.back().second, time, skeleton, out);
            return;
        }
        for (usize i = 0; i + 1 < node->children.size(); ++i) {
            const f32 a = node->children[i].first;
            const f32 b = node->children[i + 1].first;
            if (param >= a && param <= b) {
                const f32 t = (b > a) ? (param - a) / (b - a) : 0.0f;
                std::vector<BoneLocal> la;
                std::vector<BoneLocal> lb;
                sampleBlendLocal(node->children[i].second, time, skeleton, la);
                sampleBlendLocal(node->children[i + 1].second, time, skeleton, lb);
                const usize n = std::min(la.size(), lb.size());
                out.resize(n);
                for (usize j = 0; j < n; ++j) {
                    out[j] = lerpBoneLocal(la[j], lb[j], t);
                }
                return;
            }
        }
        return;
    }

    case BlendNode::Type::Blend2D:
        // Blend2D is not implemented yet: it plays its first child.
        if (!node->children.empty()) {
            sampleBlendLocal(node->children[0].second, time, skeleton, out);
        }
        return;
    }
}

inline void sampleBlendTree(
    const BlendNode* node, f32 time,
    const Skeleton& skeleton,
    std::vector<Mat4>& outResult,
    f32 weight)
{
    std::vector<BoneLocal> locals;
    sampleBlendLocal(node, time, skeleton, locals);
    if (locals.empty()) return;

    std::vector<Mat4> world;
    composeBoneWorld(skeleton, locals, world);
    std::vector<Mat4> skin(skeleton.boneCount(), Mat4::identity());
    for (u32 i = 0; i < skeleton.boneCount() && i < world.size(); ++i) {
        skin[i] = skeleton.skinSpace * world[i] * skeleton.bones[i].bindPoseInverse;
    }
    accumulateBlend(outResult, skin, weight);
}

inline void accumulateBlend(
    std::vector<Mat4>& accum,
    const std::vector<Mat4>& sample,
    f32 weight)
{
    u32 count = static_cast<u32>(accum.size() < sample.size() ? accum.size() : sample.size());
    for (u32 i = 0; i < count; ++i) {
        for (u32 k = 0; k < 16; ++k) {
            accum[i].data()[k] += sample[i].data()[k] * weight;
        }
    }
}

} // namespace Caffeine::Animation
