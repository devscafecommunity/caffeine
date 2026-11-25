#include "world/WorldInstanceDraw.hpp"

#include "assets/MeshCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Caffeine::World {

const std::vector<InstanceModelPart>* InstanceModels::partsFor(const InstanceRecord& record) const {
    if (!cells.empty()) {
        const auto cell = cells.find({record.classIndex, record.cellX, record.cellZ});
        if (cell != cells.end()) return &cell->second;
    }
    const auto variant = variants.find({record.classIndex, record.variant});
    return variant != variants.end() ? &variant->second : nullptr;
}

namespace {

bool parseUnsigned(const std::string& text, u32& out) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 9) return false;
    out = static_cast<u32>(std::strtoul(text.c_str(), nullptr, 10));
    return true;
}

bool parseSigned(const std::string& text, i32& out) {
    const usize start = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
    u32 magnitude = 0;
    if (!parseUnsigned(text.substr(start), magnitude)) return false;
    out = (start == 1 && text[0] == '-') ? -static_cast<i32>(magnitude) : static_cast<i32>(magnitude);
    return true;
}

bool fail(std::string* error, u32 line, const std::string& why) {
    if (error) *error = "models line " + std::to_string(line) + ": " + why;
    return false;
}

}  // namespace

bool parseInstanceModels(const std::string& text, InstanceModels& out, std::string* error) {
    out = InstanceModels{};
    std::istringstream lines(text);
    std::string raw;
    u32 lineNo = 0;
    while (std::getline(lines, raw)) {
        ++lineNo;
        const usize hash = raw.find('#');
        if (hash != std::string::npos) raw.resize(hash);
        std::istringstream words(raw);
        std::vector<std::string> tokens;
        for (std::string word; words >> word;) tokens.push_back(word);
        if (tokens.empty()) continue;

        u32 classIndex = 0;
        if (!parseUnsigned(tokens[0], classIndex)) return fail(error, lineNo, "class index '" + tokens[0] + "'");
        usize next = 1;
        bool isCell = false;
        u32 variant = 0;
        i32 cellX = 0, cellZ = 0;
        if (tokens.size() > 1 && tokens[1] == "cell") {
            if (tokens.size() < 5) return fail(error, lineNo, "cell needs <cx> <cz> <mesh path>");
            if (!parseSigned(tokens[2], cellX) || !parseSigned(tokens[3], cellZ)) {
                return fail(error, lineNo, "cell coordinates '" + tokens[2] + " " + tokens[3] + "'");
            }
            isCell = true;
            next = 4;
        } else {
            if (tokens.size() < 3) return fail(error, lineNo, "expected <class> <variant> <mesh path>");
            if (!parseUnsigned(tokens[1], variant)) return fail(error, lineNo, "variant '" + tokens[1] + "'");
            next = 2;
        }

        InstanceModelPart part;
        part.meshPath = tokens[next++];
        while (next < tokens.size()) {
            const std::string& key = tokens[next++];
            if (key == "slots") {
                if (next >= tokens.size()) return fail(error, lineNo, "slots needs a list");
                part.slots = tokens[next++];
            } else if (key == "cutout") {
                // `cutout` alone means on.
                if (next < tokens.size() && (tokens[next] == "on" || tokens[next] == "off")) {
                    part.cutout = tokens[next++] == "on";
                } else {
                    part.cutout = true;
                }
            } else {
                return fail(error, lineNo, "unknown option '" + key + "'");
            }
        }
        if (isCell) {
            out.cells[{classIndex, cellX, cellZ}].push_back(std::move(part));
        } else {
            out.variants[{classIndex, variant}].push_back(std::move(part));
        }
    }
    return true;
}

bool readInstanceModels(const std::filesystem::path& path, InstanceModels& out, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open " + path.string();
        out = InstanceModels{};
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return parseInstanceModels(text.str(), out, error);
}

Quat instanceRotation(const std::array<f32, 3>& upIn, f32 yaw) {
    // The convention of caffeine_session_scatter_terrain: the yaw about the object's own up, then up taken to the
    // instance's up by the shortest turn.
    Vec3 up(upIn[0], upIn[1], upIn[2]);
    if (up.lengthSquared() < 1e-12f) up = Vec3(0.0f, 1.0f, 0.0f);
    up = up.normalized();
    Quat align = Quat::identity();
    const Vec3 axis = Vec3(0.0f, 1.0f, 0.0f).cross(up);
    if (axis.length() > 1e-5f) {
        align = Quat::fromAxisAngle(axis.normalized(), std::acos(std::clamp(up.y, -1.0f, 1.0f)));
    }
    return (align * Quat::fromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), yaw)).normalized();
}

Mat4 instanceLocalMatrix(const InstanceRecord& record) {
    const Mat4 t = Mat4::translation(record.position[0], record.position[1], record.position[2]);
    return t * instanceRotation(record.up, record.yaw).toMatrix() * Mat4::scale(record.scale);
}

f32 quantiseInstanceModifier(f32 value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::round(value / kInstanceModifierStep) * kInstanceModifierStep;
}

Vec3 quantisedInstanceTint(const InstanceRecord& record) {
    return Vec3(std::max(quantiseInstanceModifier(record.tint[0]), 0.0f),
                std::max(quantiseInstanceModifier(record.tint[1]), 0.0f),
                std::max(quantiseInstanceModifier(record.tint[2]), 0.0f));
}

std::vector<InstanceChunkBounds> computeChunkBounds(const InstanceFile& file) {
    std::vector<InstanceChunkBounds> out(file.chunks.size());
    for (usize c = 0; c < file.chunks.size(); ++c) {
        InstanceChunkBounds& b = out[c];
        for (const InstanceRecord& r : file.chunks[c].instances) {
            const Vec3 p(r.position[0], r.position[1], r.position[2]);
            if (b.empty) {
                b.min = p;
                b.max = p;
                b.empty = false;
            } else {
                b.min = Vec3(std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z));
                b.max = Vec3(std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z));
            }
            b.maxScale = std::max(b.maxScale, std::abs(r.scale));
        }
    }
    return out;
}

Spatial::AABB3D chunkWorldBounds(const InstanceChunkBounds& bounds, const Mat4& world, f32 modelRadius) {
    const f32 grow = std::max(modelRadius, 0.0f) * bounds.maxScale;
    const Vec3 lo = bounds.min - Vec3(grow, grow, grow);
    const Vec3 hi = bounds.max + Vec3(grow, grow, grow);
    Spatial::AABB3D box;
    box.min = Vec3(1e30f, 1e30f, 1e30f);
    box.max = Vec3(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
        const Vec3 p = world.transformPoint(corner);
        box.min = Vec3(std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z));
        box.max = Vec3(std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z));
    }
    return box;
}

f32 distanceToBox(const Vec3& point, const Spatial::AABB3D& box) {
    const f32 dx = std::max({box.min.x - point.x, 0.0f, point.x - box.max.x});
    const f32 dy = std::max({box.min.y - point.y, 0.0f, point.y - box.max.y});
    const f32 dz = std::max({box.min.z - point.z, 0.0f, point.z - box.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool chunkVisible(const InstanceChunkBounds& bounds, const Mat4& world, f32 modelRadius,
                  const Spatial::Frustum* frustum, const Vec3& camera, f32 drawDistance) {
    if (bounds.empty) return false;
    const Spatial::AABB3D box = chunkWorldBounds(bounds, world, modelRadius);
    if (drawDistance > 0.0f && distanceToBox(camera, box) > drawDistance) return false;
    return !frustum || frustum->intersects(box);
}

std::string resolveWorldAssetPath(const std::string& path, const std::string& projectRoot) {
    if (path.empty()) return {};
    for (const std::string& candidate : Assets::MeshCache::buildCandidatePaths(path, projectRoot)) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
    }
    return {};
}

WorldInstanceCache& WorldInstanceCache::instance() {
    static WorldInstanceCache cache;
    return cache;
}

void WorldInstanceCache::clear() {
    m_entries.clear();
}

WorldInstanceCache::FileStamp WorldInstanceCache::stampOf(const std::string& path, const std::string& projectRoot) {
    FileStamp stamp;
    stamp.resolved = resolveWorldAssetPath(path, projectRoot);
    if (!stamp.resolved.empty()) {
        std::error_code ec;
        stamp.time = std::filesystem::last_write_time(stamp.resolved, ec);
    }
    return stamp;
}

const WorldInstanceSet& WorldInstanceCache::get(const std::string& bakePath, const std::string& modelsPath,
                                                const std::string& projectRoot) {
    const std::string key = projectRoot + '\n' + bakePath + '\n' + modelsPath;
    Entry& entry = m_entries[key];
    const FileStamp bake = stampOf(bakePath, projectRoot);
    const FileStamp models = stampOf(modelsPath, projectRoot);
    if (entry.set && entry.bake == bake && entry.models == models) return *entry.set;

    auto set = std::make_shared<WorldInstanceSet>();
    if (bake.resolved.empty()) {
        set->error = bakePath.empty() ? "no bake" : "bake not found: " + bakePath;
    } else {
        ++m_loads;
        std::string why;
        if (readInstanceFile(bake.resolved, set->bake, &why)) {
            set->chunkBounds = computeChunkBounds(set->bake);
        } else {
            set->bake = InstanceFile{};
            set->error = why;
        }
    }
    if (models.resolved.empty()) {
        if (set->error.empty()) set->error = modelsPath.empty() ? "no models file" : "models not found: " + modelsPath;
    } else {
        ++m_loads;
        std::string why;
        if (!readInstanceModels(models.resolved, set->models, &why)) {
            set->models = InstanceModels{};
            if (set->error.empty()) set->error = why;
        }
    }
    entry.bake = bake;
    entry.models = models;
    entry.set = std::move(set);
    return *entry.set;
}

}  // namespace Caffeine::World
