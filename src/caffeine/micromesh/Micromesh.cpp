#include "micromesh/Micromesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>

namespace Caffeine::Micromesh {
namespace {

using Tri = std::array<u32, 3>;

u32 expandBits(u32 v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

/// 30-bit Morton code of a point inside the box.
u32 morton(const Vec3& p, const Vec3& lo, const Vec3& extent) {
    auto q = [](f32 v, f32 lo, f32 e) {
        const f32 t = e > 0.0f ? (v - lo) / e : 0.0f;
        return static_cast<u32>(std::clamp(t, 0.0f, 1.0f) * 1023.0f);
    };
    return (expandBits(q(p.x, lo.x, extent.x)) << 2) | (expandBits(q(p.y, lo.y, extent.y)) << 1) |
           expandBits(q(p.z, lo.z, extent.z));
}

Sphere sphereOf(const std::vector<Vec3>& points) {
    Sphere s;
    if (points.empty()) return s;
    Vec3 lo = points[0];
    Vec3 hi = points[0];
    for (const Vec3& p : points) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    s.center = (lo + hi) * 0.5f;
    for (const Vec3& p : points) s.radius = std::max(s.radius, (p - s.center).length());
    return s;
}

/// A sphere that contains both.
Sphere merge(const Sphere& a, const Sphere& b) {
    const Vec3 d = b.center - a.center;
    const f32 dist = d.length();
    if (dist + b.radius <= a.radius) return a;
    if (dist + a.radius <= b.radius) return b;
    Sphere s;
    s.radius = (dist + a.radius + b.radius) * 0.5f;
    s.center = a.center + (dist > 0.0f ? d * ((s.radius - a.radius) / dist) : Vec3(0.0f, 0.0f, 0.0f));
    return s;
}

Vec3 triangleNormal(const std::vector<Vec3>& p, const Tri& t) {
    return (p[t[1]] - p[t[0]]).cross(p[t[2]] - p[t[0]]);
}

// ----------------------------------------------------------------------------- quadrics

struct Quadric {
    /// Number of planes accumulated: the error is reported as an RMS distance (cost / weight), not a sum.
    double weight = 0;
    double a2 = 0, ab = 0, ac = 0, ad = 0, b2 = 0, bc = 0, bd = 0, c2 = 0, cd = 0, d2 = 0;

    static Quadric plane(double a, double b, double c, double d) {
        Quadric q;
        q.weight = 1.0;
        q.a2 = a * a; q.ab = a * b; q.ac = a * c; q.ad = a * d;
        q.b2 = b * b; q.bc = b * c; q.bd = b * d;
        q.c2 = c * c; q.cd = c * d;
        q.d2 = d * d;
        return q;
    }
    Quadric operator+(const Quadric& o) const {
        Quadric r;
        r.weight = weight + o.weight;
        r.a2 = a2 + o.a2; r.ab = ab + o.ab; r.ac = ac + o.ac; r.ad = ad + o.ad;
        r.b2 = b2 + o.b2; r.bc = bc + o.bc; r.bd = bd + o.bd;
        r.c2 = c2 + o.c2; r.cd = cd + o.cd; r.d2 = d2 + o.d2;
        return r;
    }
    double error(const Vec3& v) const {
        const double x = v.x, y = v.y, z = v.z;
        return a2 * x * x + 2 * ab * x * y + 2 * ac * x * z + 2 * ad * x + b2 * y * y +
               2 * bc * y * z + 2 * bd * y + c2 * z * z + 2 * cd * z + d2;
    }
};

// -------------------------------------------------------------------------- simplification

struct Simplified {
    std::vector<Tri> tris;
    f32 error = 0.0f;
};

/// Quadric edge collapse of one group. Vertices on edges that belong to a single triangle of the
/// group (its boundary) and on non-manifold edges are locked, so neighbouring groups keep matching
/// edges. A collapse creates a new vertex (the old ones stay valid for the finer levels).
Simplified simplifyGroup(std::vector<Vec3>& positions, std::vector<Vec3>& normals, std::vector<Vec2>& uvs,
                         std::vector<Vec4>& tangents, std::vector<Vec4>& colors, const std::vector<Tri>& input,
                         u32 target) {
    Simplified result;
    // Local vertex table.
    std::vector<u32> globalOf;
    std::vector<i32> localOfGlobal;  // grown on demand
    auto local = [&](u32 g) {
        if (g >= localOfGlobal.size()) localOfGlobal.resize(g + 1, -1);
        if (localOfGlobal[g] < 0) {
            localOfGlobal[g] = static_cast<i32>(globalOf.size());
            globalOf.push_back(g);
        }
        return static_cast<u32>(localOfGlobal[g]);
    };
    std::vector<Tri> tris;
    tris.reserve(input.size());
    for (const Tri& t : input) tris.push_back({local(t[0]), local(t[1]), local(t[2])});

    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;  // shading normals, parallel to `pos`
    std::vector<Vec2> tex;  // texture coordinates, parallel to `pos` (empty without UVs)
    std::vector<Vec4> tan;  // tangents (xyz, handedness w), parallel to `pos` (empty without tangents)
    std::vector<Vec4> col;  // vertex colours, parallel to `pos` (empty without colours)
    const bool hasUv = !uvs.empty();
    const bool hasTangent = !tangents.empty();
    const bool hasColor = !colors.empty();
    for (u32 g : globalOf) {
        pos.push_back(positions[g]);
        nrm.push_back(normals[g]);
        if (hasUv) tex.push_back(uvs[g]);
        if (hasTangent) tan.push_back(tangents[g]);
        if (hasColor) col.push_back(colors[g]);
    }
    const u32 originalCount = static_cast<u32>(pos.size());
    std::vector<Quadric> quad(pos.size());
    std::vector<char> locked(pos.size(), 0);
    std::vector<char> alive(pos.size(), 1);
    std::vector<u32> stamp(pos.size(), 0);
    std::vector<std::vector<u32>> vt(pos.size());
    std::vector<char> triAlive(tris.size(), 1);

    for (u32 i = 0; i < tris.size(); ++i) {
        const Vec3 n = triangleNormal(pos, tris[i]);
        const f32 len = n.length();
        for (u32 k = 0; k < 3; ++k) vt[tris[i][k]].push_back(i);
        if (len <= 0.0f) continue;
        const Vec3 u = n / len;
        const double d = -static_cast<double>(u.dot(pos[tris[i][0]]));
        const Quadric q = Quadric::plane(u.x, u.y, u.z, d);
        for (u32 k = 0; k < 3; ++k) quad[tris[i][k]] = quad[tris[i][k]] + q;
    }

    // Boundary: edges used by exactly one triangle (or by more than two) lock their vertices.
    {
        std::vector<std::array<u32, 2>> edges;
        for (const Tri& t : tris) {
            for (u32 k = 0; k < 3; ++k) {
                const u32 a = t[k], b = t[(k + 1) % 3];
                edges.push_back({std::min(a, b), std::max(a, b)});
            }
        }
        std::sort(edges.begin(), edges.end());
        for (size_t i = 0; i < edges.size();) {
            size_t j = i;
            while (j < edges.size() && edges[j] == edges[i]) ++j;
            if (j - i != 2) {
                locked[edges[i][0]] = 1;
                locked[edges[i][1]] = 1;
            }
            i = j;
        }
    }

    struct Candidate {
        double cost;
        u32 a, b, stampA, stampB;
        bool operator>(const Candidate& o) const {
            if (cost != o.cost) return cost > o.cost;
            if (a != o.a) return a > o.a;
            return b > o.b;
        }
    };
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> queue;

    struct Best {
        Vec3 position;
        double cost;   // quadric error (sum of squared distances to the accumulated planes)
        double weight; // planes accumulated: cost / weight is a mean squared distance
    };
    auto bestCollapse = [&](u32 a, u32 b) {
        const Quadric q = quad[a] + quad[b];
        const Vec3 mid = (pos[a] + pos[b]) * 0.5f;
        const double w = std::max(q.weight, 1.0);
        Best best{pos[a], q.error(pos[a]), w};
        const double eb = q.error(pos[b]);
        if (eb < best.cost) best = {pos[b], eb, w};
        const double em = q.error(mid);
        if (em < best.cost) best = {mid, em, w};
        return best;
    };
    auto neighbours = [&](u32 v) {
        std::vector<u32> n;
        for (u32 t : vt[v]) {
            if (!triAlive[t]) continue;
            for (u32 k = 0; k < 3; ++k) {
                if (tris[t][k] != v) n.push_back(tris[t][k]);
            }
        }
        std::sort(n.begin(), n.end());
        n.erase(std::unique(n.begin(), n.end()), n.end());
        return n;
    };
    auto pushEdges = [&](u32 v) {
        if (locked[v]) return;
        for (u32 n : neighbours(v)) {
            if (locked[n]) continue;
            const u32 a = std::min(v, n), b = std::max(v, n);
            queue.push({bestCollapse(a, b).cost, a, b, stamp[a], stamp[b]});
        }
    };
    for (u32 v = 0; v < originalCount; ++v) pushEdges(v);

    u32 liveTris = static_cast<u32>(tris.size());
    double maxCost = 0.0;
    while (liveTris > target && !queue.empty()) {
        const Candidate c = queue.top();
        queue.pop();
        if (!alive[c.a] || !alive[c.b] || stamp[c.a] != c.stampA || stamp[c.b] != c.stampB) continue;
        const Best best = bestCollapse(c.a, c.b);

        // Triangles around the edge, and the link condition (no non-manifold result).
        std::vector<u32> around;
        for (u32 v : {c.a, c.b}) {
            for (u32 t : vt[v]) {
                if (triAlive[t]) around.push_back(t);
            }
        }
        std::sort(around.begin(), around.end());
        around.erase(std::unique(around.begin(), around.end()), around.end());
        std::vector<u32> dying;
        for (u32 t : around) {
            const bool hasA = tris[t][0] == c.a || tris[t][1] == c.a || tris[t][2] == c.a;
            const bool hasB = tris[t][0] == c.b || tris[t][1] == c.b || tris[t][2] == c.b;
            if (hasA && hasB) dying.push_back(t);
        }
        const std::vector<u32> na = neighbours(c.a);
        const std::vector<u32> nb = neighbours(c.b);
        std::vector<u32> common;
        std::set_intersection(na.begin(), na.end(), nb.begin(), nb.end(), std::back_inserter(common));
        if (dying.size() != 2 || common.size() != 2) continue;

        bool legal = true;
        for (u32 t : around) {
            if (std::find(dying.begin(), dying.end(), t) != dying.end()) continue;
            Tri moved = tris[t];
            for (u32& v : moved) {
                if (v == c.a || v == c.b) v = static_cast<u32>(pos.size());
            }
            std::vector<Vec3> probe = pos;
            probe.push_back(best.position);
            const Vec3 before = triangleNormal(pos, tris[t]);
            const Vec3 after = triangleNormal(probe, moved);
            const f32 lb = before.length();
            const f32 la = after.length();
            if (la <= 1e-12f || lb <= 1e-12f || before.dot(after) / (lb * la) < 0.2f) {
                legal = false;
                break;
            }
        }
        if (!legal) continue;

        // Apply: a new vertex replaces both.
        const u32 w = static_cast<u32>(pos.size());
        pos.push_back(best.position);
        {
            // The new vertex shades like the two it replaces: their normals, averaged.
            const Vec3 sum = nrm[c.a] + nrm[c.b];
            const f32 len = sum.length();
            nrm.push_back(len > 1e-6f ? sum / len : nrm[c.a]);
            if (hasTangent) {
                // The new vertex's tangent: the two replaced ones, averaged; the handedness of the first.
                const Vec3 ta(tan[c.a].x, tan[c.a].y, tan[c.a].z), tb(tan[c.b].x, tan[c.b].y, tan[c.b].z);
                const Vec3 sumT = ta + tb;
                const f32 lenT = sumT.length();
                const Vec3 outT = lenT > 1e-6f ? sumT / lenT : ta;
                tan.push_back(Vec4(outT.x, outT.y, outT.z, tan[c.a].w));
            }
            if (hasColor) col.push_back((col[c.a] + col[c.b]) * 0.5f);  // colours: the two, averaged
            if (hasUv) {
                // Texture coordinates: the point of the edge closest to the new position. A seam is a
                // boundary of split vertices (an edge of one triangle), so it is locked and never
                // reaches this: both ends of a collapsed edge lie on the same UV island.
                const Vec3 edge = pos[c.b] - pos[c.a];
                const f32 edgeLength2 = edge.lengthSquared();
                const f32 t = edgeLength2 > 0.0f
                                  ? std::clamp((best.position - pos[c.a]).dot(edge) / edgeLength2, 0.0f, 1.0f)
                                  : 0.5f;
                tex.push_back(tex[c.a] * (1.0f - t) + tex[c.b] * t);
            }
        }
        quad.push_back(quad[c.a] + quad[c.b]);
        locked.push_back(0);
        alive.push_back(1);
        stamp.push_back(0);
        vt.emplace_back();
        for (u32 t : dying) {
            triAlive[t] = 0;
            --liveTris;
        }
        for (u32 t : around) {
            if (!triAlive[t]) continue;
            for (u32& v : tris[t]) {
                if (v == c.a || v == c.b) v = w;
            }
            vt[w].push_back(t);
        }
        alive[c.a] = alive[c.b] = 0;
        // RMS distance of the surface to the planes it replaced, a stable length in world units.
        maxCost = std::max(maxCost, best.cost / best.weight);
        pushEdges(w);
    }

    // Surviving triangles: new vertices get global ids in local order (deterministic).
    std::vector<i32> globalOfLocal(pos.size(), -1);
    for (u32 v = 0; v < originalCount; ++v) globalOfLocal[v] = static_cast<i32>(globalOf[v]);
    for (u32 i = 0; i < tris.size(); ++i) {
        if (!triAlive[i]) continue;
        Tri out;
        for (u32 k = 0; k < 3; ++k) {
            const u32 v = tris[i][k];
            if (globalOfLocal[v] < 0) {
                globalOfLocal[v] = static_cast<i32>(positions.size());
                positions.push_back(pos[v]);
                normals.push_back(nrm[v]);
                if (hasUv) uvs.push_back(tex[v]);
                if (hasTangent) tangents.push_back(tan[v]);
                if (hasColor) colors.push_back(col[v]);
            }
            out[k] = static_cast<u32>(globalOfLocal[v]);
        }
        result.tris.push_back(out);
    }
    result.error = static_cast<f32>(std::sqrt(std::max(maxCost, 0.0)));
    return result;
}

// ---------------------------------------------------------------------------- clustering

struct Piece {
    std::vector<Tri> tris;
    Sphere bounds;
    f32 error = 0.0f;
    Sphere lodSphere;
    u32 id = 0;  ///< index in Data::clusters once emitted
};

/// Splits triangles into clusters of <= 128 triangles and <= 128 vertices along a Morton curve.
std::vector<std::vector<Tri>> splitClusters(const std::vector<Vec3>& positions, std::vector<Tri> tris) {
    std::vector<std::vector<Tri>> out;
    if (tris.empty()) return out;
    Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (const Tri& t : tris) {
        for (u32 v : t) {
            const Vec3& p = positions[v];
            lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
            hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        }
    }
    const Vec3 extent = hi - lo;
    std::vector<std::pair<u32, u32>> order;  // (morton, original index)
    for (u32 i = 0; i < tris.size(); ++i) {
        const Vec3 c = (positions[tris[i][0]] + positions[tris[i][1]] + positions[tris[i][2]]) * (1.0f / 3.0f);
        order.push_back({morton(c, lo, extent), i});
    }
    std::sort(order.begin(), order.end());
    std::vector<Tri> current;
    std::vector<u32> verts;
    for (const auto& [code, index] : order) {
        (void)code;
        const Tri& t = tris[index];
        u32 fresh = 0;
        for (u32 v : t) {
            if (std::find(verts.begin(), verts.end(), v) == verts.end()) ++fresh;
        }
        if (!current.empty() && (current.size() + 1 > kMaxClusterTriangles ||
                                 verts.size() + fresh > kMaxClusterVertices)) {
            out.push_back(std::move(current));
            current.clear();
            verts.clear();
        }
        current.push_back(t);
        for (u32 v : t) {
            if (std::find(verts.begin(), verts.end(), v) == verts.end()) verts.push_back(v);
        }
    }
    if (!current.empty()) out.push_back(std::move(current));
    return out;
}

Sphere boundsOfTris(const std::vector<Vec3>& positions, const std::vector<Tri>& tris) {
    std::vector<Vec3> pts;
    for (const Tri& t : tris) {
        for (u32 v : t) pts.push_back(positions[v]);
    }
    return sphereOf(pts);
}

void emit(Data& data, Piece& piece, u32 level, u32 group) {
    Cluster c;
    c.level = level;
    c.group = group;
    c.firstVertex = static_cast<u32>(data.vertexIndices.size());
    c.firstTriangle = static_cast<u32>(data.triangles.size() / 3);
    std::vector<u32> locals;
    auto localOf = [&](u32 g) {
        for (u32 i = 0; i < locals.size(); ++i) {
            if (locals[i] == g) return i;
        }
        locals.push_back(g);
        return static_cast<u32>(locals.size() - 1);
    };
    Vec3 axis(0.0f, 0.0f, 0.0f);
    std::vector<Vec3> normals;
    for (const Tri& t : piece.tris) {
        for (u32 v : t) data.triangles.push_back(static_cast<u8>(localOf(v)));
        const Vec3 n = triangleNormal(data.positions, t);
        const f32 len = n.length();
        if (len > 0.0f) {
            normals.push_back(n / len);
            axis += n / len * len;  // area weighted
        }
    }
    for (u32 g : locals) data.vertexIndices.push_back(g);
    c.vertexCount = static_cast<u32>(locals.size());
    c.triangleCount = static_cast<u32>(piece.tris.size());
    c.bounds = piece.bounds;
    c.error = piece.error;
    c.lodSphere = piece.lodSphere;
    c.parentLodSphere = piece.lodSphere;
    if (axis.lengthSquared() > 0.0f) {
        c.coneAxis = axis.normalized();
        f32 cutoff = 1.0f;
        for (const Vec3& n : normals) cutoff = std::min(cutoff, n.dot(c.coneAxis));
        c.coneCutoff = cutoff;
    }
    piece.id = static_cast<u32>(data.clusters.size());
    data.clusters.push_back(c);
}

}  // namespace

Data build(const std::vector<Vec3>& positions, const std::vector<u32>& indices) {
    return build(positions, indices, std::vector<Vec2>());
}

Data build(const std::vector<Vec3>& positions, const std::vector<u32>& indices, const std::vector<Vec2>& uvs) {
    Attributes attributes;
    attributes.uvs = &uvs;
    return build(positions, indices, attributes);
}

Data build(const std::vector<Vec3>& positions, const std::vector<u32>& indices, const Attributes& attributes) {
    Data data;
    data.positions = positions;
    if (attributes.uvs && attributes.uvs->size() == positions.size()) data.uvs = *attributes.uvs;
    if (attributes.tangents && attributes.tangents->size() == positions.size()) data.tangents = *attributes.tangents;
    if (attributes.colors && attributes.colors->size() == positions.size()) data.colors = *attributes.colors;
    std::vector<Tri> source;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const Tri t = {indices[i], indices[i + 1], indices[i + 2]};
        if (t[0] >= positions.size() || t[1] >= positions.size() || t[2] >= positions.size()) continue;
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
        if (triangleNormal(positions, t).lengthSquared() <= 0.0f) continue;
        source.push_back(t);
    }
    data.sourceTriangles = static_cast<u32>(source.size());
    if (source.empty()) return data;

    // Smooth shading normals of the source: each triangle's normal weighted by its corner angle. The
    // collapses carry them to the coarser levels, so a cut shades as one continuous surface.
    const bool givenNormals = attributes.normals && attributes.normals->size() == positions.size();
    data.normals.assign(positions.size(), Vec3(0.0f, 0.0f, 0.0f));
    for (const Tri& t : source) {
        if (givenNormals) break;
        const Vec3 n = triangleNormal(positions, t).normalized();
        for (u32 k = 0; k < 3; ++k) {
            const Vec3 e1 = (positions[t[(k + 1) % 3]] - positions[t[k]]).normalized();
            const Vec3 e2 = (positions[t[(k + 2) % 3]] - positions[t[k]]).normalized();
            const f32 angle = std::acos(std::clamp(e1.dot(e2), -1.0f, 1.0f));
            data.normals[t[k]] = data.normals[t[k]] + n * angle;
        }
    }
    if (givenNormals) data.normals = *attributes.normals;  // the mesh's own (hard edges are split vertices)
    for (Vec3& n : data.normals) {
        const f32 len = n.length();
        n = len > 1e-9f ? n / len : Vec3(0.0f, 1.0f, 0.0f);
    }

    // The monotonic epsilon: parent error strictly exceeds its children's.
    Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (const Vec3& p : positions) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    const f32 epsilon = std::max((hi - lo).length() * 1e-5f, 1e-9f);

    // Level 0.
    std::vector<Piece> current;
    for (auto& tris : splitClusters(data.positions, source)) {
        Piece piece;
        piece.tris = std::move(tris);
        piece.bounds = boundsOfTris(data.positions, piece.tris);
        piece.lodSphere = piece.bounds;
        current.push_back(std::move(piece));
    }
    for (Piece& piece : current) emit(data, piece, 0, static_cast<u32>(data.clusters.size()));
    data.levelCount = 1;

    u32 level = 0;
    while (current.size() > 1) {
        u32 groupSize = 4;
        u32 inputTriangles = 0;
        for (const Piece& p : current) inputTriangles += static_cast<u32>(p.tris.size());

        // Order the clusters along a Morton curve of their centres, so groups are neighbours.
        Vec3 clo(1e30f, 1e30f, 1e30f), chi(-1e30f, -1e30f, -1e30f);
        for (const Piece& p : current) {
            clo = Vec3(std::min(clo.x, p.bounds.center.x), std::min(clo.y, p.bounds.center.y), std::min(clo.z, p.bounds.center.z));
            chi = Vec3(std::max(chi.x, p.bounds.center.x), std::max(chi.y, p.bounds.center.y), std::max(chi.z, p.bounds.center.z));
        }
        std::vector<u32> order(current.size());
        for (u32 i = 0; i < order.size(); ++i) order[i] = i;
        std::vector<u32> codes(current.size());
        for (u32 i = 0; i < codes.size(); ++i) codes[i] = morton(current[i].bounds.center, clo, chi - clo);
        std::stable_sort(order.begin(), order.end(), [&](u32 a, u32 b) { return codes[a] < codes[b]; });

        struct Result {
            std::vector<Piece> next;
            std::vector<std::vector<u32>> members;  // clusters (ids in `current`) per group
            std::vector<f32> groupError;
            std::vector<Sphere> groupSphere;
            std::vector<size_t> nextFirst;
            u32 triangles = 0;
        };
        Result best;
        const size_t savedPositions = data.positions.size();  // normals grow and shrink with it
        for (;;) {
            Result r;
            for (size_t first = 0; first < order.size(); first += groupSize) {
                const size_t last = std::min(order.size(), first + groupSize);
                std::vector<u32> members(order.begin() + static_cast<long>(first), order.begin() + static_cast<long>(last));
                std::vector<Tri> merged;
                f32 childError = 0.0f;
                Sphere sphere = current[members[0]].lodSphere;
                for (u32 m : members) {
                    merged.insert(merged.end(), current[m].tris.begin(), current[m].tris.end());
                    childError = std::max(childError, current[m].error);
                    sphere = merge(sphere, current[m].lodSphere);
                }
                const u32 target = std::max<u32>(static_cast<u32>(merged.size()) / 2, 1u);
                Simplified s = simplifyGroup(data.positions, data.normals, data.uvs, data.tangents, data.colors, merged, target);
                const f32 error = std::max(s.error, childError) + epsilon;
                const Sphere out = boundsOfTris(data.positions, s.tris);
                sphere = merge(sphere, out);
                r.nextFirst.push_back(r.next.size());
                for (auto& tris : splitClusters(data.positions, std::move(s.tris))) {
                    Piece piece;
                    piece.tris = std::move(tris);
                    piece.bounds = boundsOfTris(data.positions, piece.tris);
                    piece.error = error;
                    piece.lodSphere = sphere;
                    r.triangles += static_cast<u32>(piece.tris.size());
                    r.next.push_back(std::move(piece));
                }
                r.members.push_back(std::move(members));
                r.groupError.push_back(error);
                r.groupSphere.push_back(sphere);
            }
            const bool onlyGroup = groupSize >= order.size();
            if (r.triangles * 100u > inputTriangles * 85u && !onlyGroup) {
                // Not enough reduction (groups of loose pieces keep their boundary): widen the groups.
                data.positions.resize(savedPositions);
                data.normals.resize(savedPositions);
                if (!data.uvs.empty()) data.uvs.resize(savedPositions);
                if (!data.tangents.empty()) data.tangents.resize(savedPositions);
                if (!data.colors.empty()) data.colors.resize(savedPositions);
                groupSize *= 2;
                continue;
            }
            best = std::move(r);
            break;
        }

        // No progress at all (an open mesh whose boundary pins everything): these are the roots.
        if (best.triangles >= inputTriangles) {
            data.positions.resize(savedPositions);
            data.normals.resize(savedPositions);
            if (!data.uvs.empty()) data.uvs.resize(savedPositions);
            if (!data.tangents.empty()) data.tangents.resize(savedPositions);
            if (!data.colors.empty()) data.colors.resize(savedPositions);
            break;
        }

        ++level;
        for (size_t g = 0; g < best.members.size(); ++g) {
            for (u32 m : best.members[g]) {
                Cluster& child = data.clusters[current[m].id];
                child.parentError = best.groupError[g];
                child.parentLodSphere = best.groupSphere[g];
            }
        }
        const u32 groupBase = static_cast<u32>(data.clusters.size());
        for (size_t g = 0; g < best.members.size(); ++g) {
            const size_t end = g + 1 < best.nextFirst.size() ? best.nextFirst[g + 1] : best.next.size();
            for (size_t i = best.nextFirst[g]; i < end; ++i) {
                emit(data, best.next[i], level, groupBase + static_cast<u32>(g));
            }
        }
        data.levelCount = level + 1;
        current = std::move(best.next);
    }
    return data;
}

std::vector<u32> selectCut(const Data& data, const CutParams& params) {
    auto projected = [&](f32 error, const Sphere& s) {
        if (error >= kRootError) return kRootError;
        if (!params.viewDependent) return error;
        const f32 distance = std::max((s.center - params.camera).length() - s.radius, 1e-4f);
        return error / distance;
    };
    std::vector<u32> out;
    for (u32 i = 0; i < data.clusters.size(); ++i) {
        const Cluster& c = data.clusters[i];
        if (projected(c.error, c.lodSphere) <= params.threshold &&
            projected(c.parentError, c.parentLodSphere) > params.threshold) {
            out.push_back(i);
        }
    }
    return out;
}

bool isBackFacing(const Cluster& cluster, const Vec3& camera) {
    if (cluster.coneCutoff <= 0.0f) return false;
    const Vec3 toCamera = (camera - cluster.bounds.center).normalized();
    const f32 sine = std::sqrt(std::max(0.0f, 1.0f - cluster.coneCutoff * cluster.coneCutoff));
    return toCamera.dot(cluster.coneAxis) < -sine;
}

void appendTriangles(const Data& data, u32 cluster, std::vector<u32>& outIndices) {
    const Cluster& c = data.clusters[cluster];
    for (u32 t = 0; t < c.triangleCount; ++t) {
        for (u32 k = 0; k < 3; ++k) {
            const u8 local = data.triangles[(c.firstTriangle + t) * 3 + k];
            outIndices.push_back(data.vertexIndices[c.firstVertex + local]);
        }
    }
}

// ------------------------------------------------------------------------- serialization

namespace {
constexpr u32 kMagic = 0x314D4D43u;  // "CMM1"; version 2 added the shading normals, 3 the texture coordinates, 4 the tangents, 5 the vertex colours
constexpr u32 kVersion = 5;

template <typename T>
void put(std::vector<u8>& out, const T& value) {
    const u8* bytes = reinterpret_cast<const u8*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
void putArray(std::vector<u8>& out, const std::vector<T>& values) {
    put(out, static_cast<u32>(values.size()));
    const u8* bytes = reinterpret_cast<const u8*>(values.data());
    out.insert(out.end(), bytes, bytes + values.size() * sizeof(T));
}

struct Reader {
    const u8* bytes;
    usize size;
    usize at = 0;
    template <typename T>
    bool get(T& value) {
        if (at + sizeof(T) > size) return false;
        std::memcpy(&value, bytes + at, sizeof(T));
        at += sizeof(T);
        return true;
    }
    template <typename T>
    bool getArray(std::vector<T>& values) {
        u32 count = 0;
        if (!get(count)) return false;
        if (at + static_cast<usize>(count) * sizeof(T) > size) return false;
        values.resize(count);
        std::memcpy(values.data(), bytes + at, static_cast<usize>(count) * sizeof(T));
        at += static_cast<usize>(count) * sizeof(T);
        return true;
    }
};
}  // namespace

std::vector<u8> serialize(const Data& data) {
    static_assert(sizeof(Vec3) == 12, "Vec3 must be three floats");
    std::vector<u8> out;
    put(out, kMagic);
    put(out, kVersion);
    put(out, data.levelCount);
    put(out, data.sourceTriangles);
    putArray(out, data.positions);
    putArray(out, data.normals);
    putArray(out, data.uvs);
    putArray(out, data.tangents);
    putArray(out, data.colors);
    putArray(out, data.vertexIndices);
    putArray(out, data.triangles);
    putArray(out, data.clusters);
    return out;
}

bool deserialize(const u8* bytes, usize size, Data& out) {
    Reader r{bytes, size};
    u32 magic = 0, version = 0;
    if (!r.get(magic) || magic != kMagic || !r.get(version) || version != kVersion) return false;
    Data data;
    if (!r.get(data.levelCount) || !r.get(data.sourceTriangles)) return false;
    if (!r.getArray(data.positions) || !r.getArray(data.normals) || !r.getArray(data.uvs) || !r.getArray(data.tangents) || !r.getArray(data.colors) ||
        !r.getArray(data.vertexIndices) ||
        !r.getArray(data.triangles) || !r.getArray(data.clusters)) {
        return false;
    }
    if (data.normals.size() != data.positions.size()) return false;
    if (!data.uvs.empty() && data.uvs.size() != data.positions.size()) return false;
    if (!data.tangents.empty() && data.tangents.size() != data.positions.size()) return false;
    if (!data.colors.empty() && data.colors.size() != data.positions.size()) return false;
    // Every cluster must stay inside the arrays it indexes.
    for (const Cluster& c : data.clusters) {
        if (c.firstVertex + c.vertexCount > data.vertexIndices.size()) return false;
        if ((static_cast<usize>(c.firstTriangle) + c.triangleCount) * 3 > data.triangles.size()) return false;
    }
    out = std::move(data);
    return true;
}

}  // namespace Caffeine::Micromesh
