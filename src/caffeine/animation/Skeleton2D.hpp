#pragma once

#include "core/Types.hpp"
#include "containers/FixedString.hpp"
#include "math/Vec2.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Caffeine::Animation {

using namespace Caffeine;

// ============================================================================
// Live2D-style 2D rig.
//
// Animation sources (sprite clips, 2D parameter clips, PhaseShift states) never
// touch bones or vertices directly. They write named parameters. A rig maps each
// parameter to bone channels with piecewise-linear curves, and skinning deforms
// a 2D mesh from the resulting bone pose. Parameters are the single interface.
// ============================================================================

/// 2D affine transform: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Affine2 {
    f32 a  = 1.0f;
    f32 b  = 0.0f;
    f32 c  = 0.0f;
    f32 d  = 1.0f;
    f32 tx = 0.0f;
    f32 ty = 0.0f;

    static Affine2 identity() { return Affine2{}; }

    /// Local transform: scale, then rotate (radians, counter-clockwise), then translate.
    static Affine2 fromTRS(Vec2 position, f32 rotation, Vec2 scale) {
        const f32 cs = std::cos(rotation);
        const f32 sn = std::sin(rotation);
        Affine2 m;
        m.a  = cs * scale.x;
        m.c  = -sn * scale.y;
        m.b  = sn * scale.x;
        m.d  = cs * scale.y;
        m.tx = position.x;
        m.ty = position.y;
        return m;
    }

    /// `this * r` applies `r` first, then `this`.
    Affine2 operator*(const Affine2& r) const {
        Affine2 o;
        o.a  = a * r.a + c * r.b;
        o.b  = b * r.a + d * r.b;
        o.c  = a * r.c + c * r.d;
        o.d  = b * r.c + d * r.d;
        o.tx = a * r.tx + c * r.ty + tx;
        o.ty = b * r.tx + d * r.ty + ty;
        return o;
    }

    Vec2 apply(Vec2 p) const {
        return Vec2(a * p.x + c * p.y + tx, b * p.x + d * p.y + ty);
    }

    /// Inverse of the transform. A singular matrix returns identity.
    Affine2 inverse() const {
        const f32 det = a * d - b * c;
        if (std::fabs(det) < 1e-12f) return Affine2{};
        const f32 inv = 1.0f / det;
        Affine2 o;
        o.a  = d * inv;
        o.b  = -b * inv;
        o.c  = -c * inv;
        o.d  = a * inv;
        o.tx = -(o.a * tx + o.c * ty);
        o.ty = -(o.b * tx + o.d * ty);
        return o;
    }
};

// ----------------------------------------------------------------------------
// Skeleton
// ----------------------------------------------------------------------------

struct Bone2D {
    FixedString<32> name;
    i32             parentIndex   = -1;
    /// Rest (bind) local transform. Parameters add offsets on top of it.
    Vec2            restPosition  {};
    f32             restRotation  = 0.0f;
    Vec2            restScale     { 1.0f, 1.0f };
};

struct Skeleton2D {
    std::vector<Bone2D> bones;

    u32 boneCount() const { return static_cast<u32>(bones.size()); }

    i32 findBone(const char* name) const {
        for (u32 i = 0; i < boneCount(); ++i) {
            if (bones[i].name == name) return static_cast<i32>(i);
        }
        return -1;
    }
};

/// Local pose of one bone: rest values plus parameter offsets.
struct BonePose2D {
    Vec2 position {};
    f32  rotation = 0.0f;
    Vec2 scale    { 1.0f, 1.0f };
};

// ----------------------------------------------------------------------------
// Mesh skinning
// ----------------------------------------------------------------------------

struct BoneInfluence2D {
    u32 bone   = 0;
    f32 weight = 0.0f;
};

struct Vertex2D {
    /// Rest position in skeleton space.
    Vec2 position {};
    Vec2 uv {};
    /// At most 4 by convention. See normalizeInfluences2D.
    std::vector<BoneInfluence2D> influences;
};

// ----------------------------------------------------------------------------
// Parameters and bindings
// ----------------------------------------------------------------------------

/// Sample of a curve. Used by parameter bindings (x = parameter value) and clips (x = seconds).
struct CurveKey2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Parameter2D {
    FixedString<32> name;
    f32 minValue     = -1.0f;
    f32 defaultValue = 0.0f;
    f32 maxValue     = 1.0f;
};

enum class BoneChannel2D : u8 {
    Rotation,   // radians
    TranslateX,
    TranslateY,
    ScaleX,
    ScaleY
};

/// Adds a curve of offsets to one bone channel. The curve is evaluated at the parameter value.
/// Several bindings on the same channel sum up.
struct ParamBinding2D {
    u32             parameter = 0;
    u32             bone      = 0;
    BoneChannel2D   channel   = BoneChannel2D::Rotation;
    /// Sorted by x (parameter value). Outside the range the end values hold.
    std::vector<CurveKey2D> keys;
};

struct Rig2D {
    Skeleton2D                 skeleton;
    std::vector<Vertex2D>      vertices;
    /// Triangle list over `vertices` (3 indices per triangle).
    std::vector<u32>           indices;
    std::vector<Parameter2D>   parameters;
    std::vector<ParamBinding2D> bindings;

    i32 findParameter(const char* name) const {
        for (u32 i = 0; i < parameters.size(); ++i) {
            if (parameters[i].name == name) return static_cast<i32>(i);
        }
        return -1;
    }
};

/// Parameter values, same order as Rig2D::parameters.
using ParamValues2D = std::vector<f32>;

// ----------------------------------------------------------------------------
// Parameter clips (2D keyframes over parameters)
// ----------------------------------------------------------------------------

struct ParamTrack2D {
    u32                     parameter = 0;
    /// x = seconds, y = parameter value. Sorted by x.
    std::vector<CurveKey2D> keys;
};

struct ParamClip2D {
    FixedString<32>         name;
    f32                     duration = 0.0f;
    bool                    loop     = true;
    std::vector<ParamTrack2D> tracks;
};

// ----------------------------------------------------------------------------
// Evaluation
// ----------------------------------------------------------------------------

/// Piecewise-linear sample. Outside the keys the end values hold. Empty keys give 0.
inline f32 sampleCurve2D(const std::vector<CurveKey2D>& keys, f32 x) {
    if (keys.empty()) return 0.0f;
    if (x <= keys.front().x) return keys.front().y;
    if (x >= keys.back().x) return keys.back().y;
    for (usize i = 1; i < keys.size(); ++i) {
        if (x <= keys[i].x) {
            const CurveKey2D& a = keys[i - 1];
            const CurveKey2D& b = keys[i];
            const f32 span = b.x - a.x;
            const f32 t = span > 0.0f ? (x - a.x) / span : 0.0f;
            return a.y + (b.y - a.y) * t;
        }
    }
    return keys.back().y;
}

inline ParamValues2D defaultParameters2D(const Rig2D& rig) {
    ParamValues2D values;
    values.reserve(rig.parameters.size());
    for (const Parameter2D& p : rig.parameters) values.push_back(p.defaultValue);
    return values;
}

/// Resizes to the rig's parameter count (new slots take their defaults) and clamps every value to its range.
inline void clampParameters2D(const Rig2D& rig, ParamValues2D& values) {
    const usize old = values.size();
    values.resize(rig.parameters.size());
    for (usize i = 0; i < rig.parameters.size(); ++i) {
        const Parameter2D& p = rig.parameters[i];
        if (i >= old) values[i] = p.defaultValue;
        values[i] = std::min(std::max(values[i], p.minValue), p.maxValue);
    }
}

inline std::vector<BonePose2D> restPose2D(const Skeleton2D& skeleton) {
    std::vector<BonePose2D> pose(skeleton.boneCount());
    for (u32 i = 0; i < skeleton.boneCount(); ++i) {
        pose[i].position = skeleton.bones[i].restPosition;
        pose[i].rotation = skeleton.bones[i].restRotation;
        pose[i].scale    = skeleton.bones[i].restScale;
    }
    return pose;
}

/// Rest pose plus every binding evaluated at the (clamped) parameter values.
inline std::vector<BonePose2D> evaluateBonePose2D(const Rig2D& rig, const ParamValues2D& rawValues) {
    ParamValues2D values = rawValues;
    clampParameters2D(rig, values);

    std::vector<BonePose2D> pose = restPose2D(rig.skeleton);
    for (const ParamBinding2D& binding : rig.bindings) {
        if (binding.parameter >= values.size() || binding.bone >= pose.size()) continue;
        const f32 offset = sampleCurve2D(binding.keys, values[binding.parameter]);
        BonePose2D& bone = pose[binding.bone];
        switch (binding.channel) {
            case BoneChannel2D::Rotation:   bone.rotation += offset; break;
            case BoneChannel2D::TranslateX: bone.position.x += offset; break;
            case BoneChannel2D::TranslateY: bone.position.y += offset; break;
            case BoneChannel2D::ScaleX:     bone.scale.x += offset; break;
            case BoneChannel2D::ScaleY:     bone.scale.y += offset; break;
        }
    }
    return pose;
}

/// Skeleton-space transform of every bone. Parents may come after their children.
/// Bones with an invalid or cyclic parent fall back to their local transform.
inline std::vector<Affine2> computeWorld2D(const Skeleton2D& skeleton, const std::vector<BonePose2D>& local) {
    const u32 count = skeleton.boneCount();
    std::vector<Affine2> world(count);
    std::vector<u8> resolved(count, 0);
    if (local.size() < count) return world;

    for (u32 i = 0; i < count; ++i) {
        world[i] = Affine2::fromTRS(local[i].position, local[i].rotation, local[i].scale);
    }
    for (u32 pass = 0; pass < count; ++pass) {
        bool progressed = false;
        for (u32 i = 0; i < count; ++i) {
            if (resolved[i]) continue;
            const i32 parent = skeleton.bones[i].parentIndex;
            const Affine2 localMatrix = Affine2::fromTRS(local[i].position, local[i].rotation, local[i].scale);
            if (parent < 0) {
                world[i] = localMatrix;
                resolved[i] = 1;
                progressed = true;
            } else if (static_cast<u32>(parent) < count && resolved[static_cast<u32>(parent)]) {
                world[i] = world[static_cast<u32>(parent)] * localMatrix;
                resolved[i] = 1;
                progressed = true;
            }
        }
        if (!progressed) break;
    }
    return world;
}

/// Keeps the 4 largest weights, drops non-positive ones and normalizes the rest to sum to 1.
inline void normalizeInfluences2D(Vertex2D& vertex) {
    auto& inf = vertex.influences;
    inf.erase(std::remove_if(inf.begin(), inf.end(),
                             [](const BoneInfluence2D& i) { return !(i.weight > 0.0f); }),
              inf.end());
    std::sort(inf.begin(), inf.end(),
              [](const BoneInfluence2D& a, const BoneInfluence2D& b) { return a.weight > b.weight; });
    if (inf.size() > 4) inf.resize(4);
    f32 total = 0.0f;
    for (const BoneInfluence2D& i : inf) total += i.weight;
    if (total > 0.0f) {
        for (BoneInfluence2D& i : inf) i.weight /= total;
    }
}

/// Deformed vertex positions, in the same order as Rig2D::vertices.
/// Each vertex is the weighted sum of bone skin matrices (posed * inverse(bind)) applied to its rest position.
/// A vertex without usable influences keeps its rest position.
inline std::vector<Vec2> skinMesh2D(const Rig2D& rig, const ParamValues2D& params) {
    const Skeleton2D& skeleton = rig.skeleton;
    const u32 count = skeleton.boneCount();

    const std::vector<Affine2> bind  = computeWorld2D(skeleton, restPose2D(skeleton));
    const std::vector<Affine2> posed = computeWorld2D(skeleton, evaluateBonePose2D(rig, params));

    std::vector<Affine2> skin(count);
    for (u32 i = 0; i < count; ++i) {
        skin[i] = posed[i] * bind[i].inverse();
    }

    std::vector<Vec2> out;
    out.reserve(rig.vertices.size());
    for (const Vertex2D& vertex : rig.vertices) {
        Vec2 sum {};
        f32 total = 0.0f;
        for (const BoneInfluence2D& inf : vertex.influences) {
            if (inf.bone >= count || !(inf.weight > 0.0f)) continue;
            sum += skin[inf.bone].apply(vertex.position) * inf.weight;
            total += inf.weight;
        }
        out.push_back(total > 0.0f ? sum / total : vertex.position);
    }
    return out;
}

/// Replaces the rig mesh with a quad grid of `cols` x `rows` cells centred on the origin, spanning [-0.5, 0.5].
/// uv runs [0, 1] over the same grid. Every vertex starts with no influences; bind them afterwards.
inline void buildGridMesh2D(Rig2D& rig, u32 cols, u32 rows) {
    cols = std::max(1u, cols);
    rows = std::max(1u, rows);
    rig.vertices.clear();
    rig.indices.clear();
    rig.vertices.reserve(static_cast<usize>(cols + 1) * (rows + 1));
    for (u32 j = 0; j <= rows; ++j) {
        for (u32 i = 0; i <= cols; ++i) {
            Vertex2D v;
            const f32 u = static_cast<f32>(i) / static_cast<f32>(cols);
            const f32 w = static_cast<f32>(j) / static_cast<f32>(rows);
            v.uv       = Vec2(u, w);
            v.position = Vec2(u - 0.5f, w - 0.5f);
            rig.vertices.push_back(v);
        }
    }
    const u32 stride = cols + 1;
    for (u32 j = 0; j < rows; ++j) {
        for (u32 i = 0; i < cols; ++i) {
            const u32 a = j * stride + i;
            const u32 b = a + 1;
            const u32 c = a + stride;
            const u32 d = c + 1;
            rig.indices.insert(rig.indices.end(), {a, b, d, a, d, c});
        }
    }
}

/// Makes every vertex follow one bone with full weight (rigid attachment, e.g. a sprite on a bone).
inline void bindVerticesToBone2D(Rig2D& rig, u32 bone) {
    for (Vertex2D& v : rig.vertices) {
        v.influences = {BoneInfluence2D{bone, 1.0f}};
    }
}

/// Writes the clip's tracks into `values` at `time`. Looping clips wrap, others clamp to [0, duration].
/// Tracks whose parameter index is outside `values` are skipped. Size values with defaultParameters2D first.
inline void sampleParamClip2D(const ParamClip2D& clip, f32 time, ParamValues2D& values) {
    f32 t = time;
    if (clip.duration > 0.0f) {
        if (clip.loop) {
            t = std::fmod(time, clip.duration);
            if (t < 0.0f) t += clip.duration;
        } else {
            t = std::min(std::max(time, 0.0f), clip.duration);
        }
    } else {
        t = std::max(time, 0.0f);
    }

    for (const ParamTrack2D& track : clip.tracks) {
        if (track.keys.empty() || track.parameter >= values.size()) continue;
        values[track.parameter] = sampleCurve2D(track.keys, t);
    }
}

}  // namespace Caffeine::Animation
