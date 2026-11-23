#pragma once

// What sound can touch: a set of solid boxes with acoustic materials. The volumetric audio asks it two questions,
// both pure geometry (no device, no engine): how much of a sound gets through what lies between two points, and how
// far a ray travels before it hits something. A game fills it from its colliders; the tests and the demo build it by
// hand.

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Caffeine::Audio {

/// How a material treats sound. `transmission*` is the share of the sound's amplitude that gets through a layer
/// 20 cm thick (thicker walls lose more, thinner ones less); lows pass walls far better than highs. `reflectivity`
/// is the share of the energy a surface sends back (what makes a room echo).
struct AcousticMaterial {
    f32 transmissionLow = 0.1f;
    f32 transmissionHigh = 0.01f;
    f32 reflectivity = 0.85f;

    static AcousticMaterial concrete() { return {0.12f, 0.015f, 0.9f}; }
    static AcousticMaterial brick() { return {0.2f, 0.03f, 0.85f}; }
    static AcousticMaterial wood() { return {0.4f, 0.12f, 0.7f}; }
    static AcousticMaterial glass() { return {0.55f, 0.3f, 0.8f}; }
    static AcousticMaterial foliage() { return {0.85f, 0.6f, 0.25f}; }
    static AcousticMaterial curtain() { return {0.7f, 0.25f, 0.1f}; }
    static AcousticMaterial ground() { return {0.0f, 0.0f, 0.7f}; }
};

struct AcousticBox {
    Vec3 min;
    Vec3 max;
    AcousticMaterial material;
    bool solidForRooms = true;  ///< false: a box that blocks sound but is not a room boundary (a lone wall in a field)
};

/// What a sound keeps after crossing everything between two points: the share of low and of high frequencies.
struct Transmission {
    f32 low = 1.0f;
    f32 high = 1.0f;
    u32 crossed = 0;  ///< how many boxes the segment passed through
    bool clear() const { return crossed == 0; }
};

struct RayHit {
    bool hit = false;
    f32 distance = 0.0f;
    f32 reflectivity = 0.0f;
    Vec3 normal{0.0f, 1.0f, 0.0f};
};

/// The geometry sound travels through.
class AcousticWorld {
public:
    virtual ~AcousticWorld() = default;
    virtual Transmission transmission(const Vec3& from, const Vec3& to) const = 0;
    virtual RayHit raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance) const = 0;
    /// A point at the side of the first thing between `from` and `to`, where the sound could bend round it: the
    /// corners of the blocking box, a little outside it. Empty when nothing blocks.
    virtual std::vector<Vec3> detourCandidates(const Vec3& from, const Vec3& to) const = 0;
};

/// A box in any orientation: a centre, half extents along three orthonormal axes. A scene's obstacles are these (a cube
/// entity scaled and rotated into a wall).
struct AcousticOrientedBox {
    Vec3 center;
    Vec3 half;
    Vec3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    AcousticMaterial material;
    bool solidForRooms = true;
};

/// A world of boxes, axis-aligned or oriented. Fine for rooms, walls and ground; a game with other shapes
/// implements `AcousticWorld` itself.
class AcousticBoxWorld final : public AcousticWorld {
public:
    void clear() { m_boxes.clear(); }
    void add(const AcousticBox& box) {
        AcousticOrientedBox o;
        o.center = (box.min + box.max) * 0.5f;
        o.half = (box.max - box.min) * 0.5f;
        o.material = box.material;
        o.solidForRooms = box.solidForRooms;
        m_boxes.push_back(o);
    }
    void add(const Vec3& min, const Vec3& max, const AcousticMaterial& material, bool solidForRooms = true) {
        add(AcousticBox{min, max, material, solidForRooms});
    }
    void add(const AcousticOrientedBox& box) { m_boxes.push_back(box); }
    /// A closed room: four walls and a ceiling around [min, max], `thickness` thick, with an optional doorway cut in the
    /// wall at +Z (`doorWidth` > 0, centred on x = doorCentreX).
    void addRoom(const Vec3& min, const Vec3& max, const AcousticMaterial& material, f32 thickness = 0.3f,
                 f32 doorWidth = 0.0f, f32 doorCentreX = 0.0f, f32 doorHeight = 2.2f) {
        const f32 t = thickness;
        add({min.x - t, min.y, min.z - t}, {min.x, max.y, max.z + t}, material);  // -X
        add({max.x, min.y, min.z - t}, {max.x + t, max.y, max.z + t}, material);  // +X
        add({min.x, min.y, min.z - t}, {max.x, max.y, min.z}, material);          // -Z
        if (doorWidth > 0.0f) {
            const f32 a = doorCentreX - doorWidth * 0.5f;
            const f32 b = doorCentreX + doorWidth * 0.5f;
            add({min.x, min.y, max.z}, {a, max.y, max.z + t}, material);
            add({b, min.y, max.z}, {max.x, max.y, max.z + t}, material);
            add({a, min.y + doorHeight, max.z}, {b, max.y, max.z + t}, material);  // the lintel
        } else {
            add({min.x, min.y, max.z}, {max.x, max.y, max.z + t}, material);
        }
        add({min.x - t, max.y, min.z - t}, {max.x + t, max.y + t, max.z + t}, material);  // the ceiling
    }
    const std::vector<AcousticOrientedBox>& boxes() const { return m_boxes; }

    Transmission transmission(const Vec3& from, const Vec3& to) const override {
        Transmission out;
        const Vec3 d = to - from;
        const f32 length = d.length();
        if (length < 1e-5f) return out;
        for (const AcousticOrientedBox& box : m_boxes) {
            f32 t0 = 0.0f, t1 = 0.0f;
            Vec3 normal;
            if (!slab(box, from, d, 0.0f, 1.0f, t0, t1, normal)) continue;
            const f32 thickness = (t1 - t0) * length;
            if (thickness < 1e-4f) continue;
            const f32 layers = thickness / 0.2f;
            out.low *= std::pow(std::max(box.material.transmissionLow, 1e-4f), layers);
            out.high *= std::pow(std::max(box.material.transmissionHigh, 1e-4f), layers);
            ++out.crossed;
        }
        return out;
    }

    RayHit raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance) const override {
        RayHit best;
        best.distance = maxDistance;
        for (const AcousticOrientedBox& box : m_boxes) {
            if (!box.solidForRooms) continue;
            f32 t0 = 0.0f, t1 = 0.0f;
            Vec3 normal;
            if (!slab(box, origin, direction, 0.0f, maxDistance, t0, t1, normal)) continue;
            // A ray that starts inside a box (the listener standing in a wall) sees nothing of it.
            if (t0 <= 1e-4f) continue;
            if (t0 < best.distance) {
                best.hit = true;
                best.distance = t0;
                best.reflectivity = box.material.reflectivity;
                best.normal = normal;
            }
        }
        return best;
    }

    std::vector<Vec3> detourCandidates(const Vec3& from, const Vec3& to) const override {
        std::vector<Vec3> out;
        const Vec3 d = to - from;
        const f32 length = d.length();
        if (length < 1e-5f) return out;
        const AcousticOrientedBox* first = nullptr;
        f32 firstT = 2.0f;
        for (const AcousticOrientedBox& box : m_boxes) {
            f32 t0 = 0.0f, t1 = 0.0f;
            Vec3 normal;
            if (!slab(box, from, d, 0.0f, 1.0f, t0, t1, normal) || (t1 - t0) * length < 1e-4f) continue;
            if (t0 < firstT) {
                firstT = t0;
                first = &box;
            }
        }
        if (!first) return out;
        const f32 pad = 0.35f;
        const f32 y = (from.y + to.y) * 0.5f;
        // The corners of the box seen from above, a little outside it (on its own axes, so a turned wall has turned corners),
        // and the point over its top.
        for (const f32 sx : {-1.0f, 1.0f}) {
            for (const f32 sz : {-1.0f, 1.0f}) {
                Vec3 corner = first->center + first->axis[0] * ((first->half.x + pad) * sx) + first->axis[2] * ((first->half.z + pad) * sz);
                corner.y = y;
                out.push_back(corner);
            }
        }
        out.push_back(first->center + first->axis[1] * (first->half.y + pad));
        return out;
    }

private:
    /// Segment/ray against a box: the parameter range [t0, t1] inside it, within [tMin, tMax] of `origin + dir * t`;
    /// `normal` is the face the ray enters through.
    static bool slab(const AcousticOrientedBox& box, const Vec3& origin, const Vec3& dir, f32 tMin, f32 tMax, f32& t0, f32& t1,
                     Vec3& normal) {
        f32 a = tMin, b = tMax;
        const Vec3 rel = origin - box.center;
        const f32 h[3] = {box.half.x, box.half.y, box.half.z};
        int enterAxis = -1;
        f32 enterSign = 1.0f;
        for (int i = 0; i < 3; ++i) {
            const f32 o = rel.dot(box.axis[i]);
            const f32 d = dir.dot(box.axis[i]);
            if (std::fabs(d) < 1e-9f) {
                if (o < -h[i] || o > h[i]) return false;
                continue;
            }
            f32 ta = (-h[i] - o) / d;
            f32 tb = (h[i] - o) / d;
            f32 sign = -1.0f;  // entering through the face on the near side: the one against the ray
            if (ta > tb) {
                std::swap(ta, tb);
                sign = 1.0f;
            }
            if (ta > a) {
                a = ta;
                enterAxis = i;
                enterSign = sign;
            }
            b = std::min(b, tb);
            if (a > b) return false;
        }
        t0 = a;
        t1 = b;
        normal = enterAxis >= 0 ? box.axis[enterAxis] * enterSign : Vec3(0.0f, 1.0f, 0.0f);
        return true;
    }

    std::vector<AcousticOrientedBox> m_boxes;
};

}  // namespace Caffeine::Audio
