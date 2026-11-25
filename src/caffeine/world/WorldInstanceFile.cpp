#include "world/WorldInstanceFile.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

namespace Caffeine::World {
namespace {

constexpr char kMagic[8] = {'C', 'I', 'N', 'S', 'T', '\0', '\0', '\0'};

class Writer {
public:
    void u32v(u32 v) {
        for (int i = 0; i < 4; ++i) m_bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void u64v(u64 v) {
        for (int i = 0; i < 8; ++i) m_bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void i32v(i32 v) { u32v(static_cast<u32>(v)); }
    void f32v(f32 v) { u32v(std::bit_cast<u32>(v)); }
    void text(const std::string& s) {
        u32v(static_cast<u32>(s.size()));
        m_bytes.insert(m_bytes.end(), s.begin(), s.end());
    }
    void raw(const char* data, size_t size) { m_bytes.insert(m_bytes.end(), data, data + size); }
    std::vector<u8>& bytes() { return m_bytes; }

private:
    std::vector<u8> m_bytes;
};

class Reader {
public:
    explicit Reader(const std::vector<u8>& bytes) : m_bytes(bytes) {}
    bool ok() const { return m_ok; }
    bool has(size_t n) {
        if (m_at + n > m_bytes.size()) m_ok = false;
        return m_ok;
    }
    u32 u32v() {
        if (!has(4)) return 0;
        u32 v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<u32>(m_bytes[m_at++]) << (8 * i);
        return v;
    }
    u64 u64v() {
        if (!has(8)) return 0;
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<u64>(m_bytes[m_at++]) << (8 * i);
        return v;
    }
    i32 i32v() { return static_cast<i32>(u32v()); }
    f32 f32v() { return std::bit_cast<f32>(u32v()); }
    std::string text() {
        const u32 size = u32v();
        if (!has(size)) return {};
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_at), size);
        m_at += size;
        return s;
    }
    bool magic() {
        if (!has(sizeof(kMagic))) return false;
        const bool same = std::memcmp(m_bytes.data() + m_at, kMagic, sizeof(kMagic)) == 0;
        m_at += sizeof(kMagic);
        return same;
    }

private:
    const std::vector<u8>& m_bytes;
    size_t m_at = 0;
    bool m_ok = true;
};

}  // namespace

u32 InstanceFile::instanceCount() const {
    u32 n = 0;
    for (const InstanceChunk& c : chunks) n += static_cast<u32>(c.instances.size());
    return n;
}

std::vector<InstanceRecord> InstanceFile::flatten() const {
    std::vector<InstanceRecord> out;
    out.reserve(instanceCount());
    for (const InstanceChunk& c : chunks) out.insert(out.end(), c.instances.begin(), c.instances.end());
    return out;
}

InstanceFile bakeInstances(const WorldPart& part, f32 chunkSize) {
    InstanceFile file;
    file.generatorVersion = part.generatorVersion;
    file.cacheKey = part.cacheKey;
    file.instanceHash = instanceHash(part);
    file.region = {part.region[0], part.region[1], part.region[2], part.region[3]};
    file.chunkSize = chunkSize > 0.0f ? chunkSize : kDefaultInstanceChunk;
    std::map<std::pair<i32, i32>, InstanceChunk> chunks;  // ordered by (z, x)
    for (u32 c = 0; c < part.classes.size(); ++c) {
        const WorldClassInstances& cls = part.classes[c];
        file.classes.push_back({cls.id, cls.ref, cls.salt, cls.variants, cls.unique, cls.params});
        for (size_t i = 0; i < cls.instances.size(); ++i) {
            const Scatter::Instance& in = cls.instances[i];
            const i32 cx = static_cast<i32>(std::floor(in.position.x / file.chunkSize));
            const i32 cz = static_cast<i32>(std::floor(in.position.z / file.chunkSize));
            InstanceChunk& chunk = chunks[{cz, cx}];
            chunk.chunkX = cx;
            chunk.chunkZ = cz;
            InstanceRecord r;
            r.classIndex = c;
            r.variant = in.variant;
            r.cellX = in.cellX;
            r.cellZ = in.cellZ;
            r.position = {in.position.x, in.position.y, in.position.z};
            r.up = {in.up.x, in.up.y, in.up.z};
            r.yaw = in.yaw;
            r.scale = in.scale;
            r.tint = {in.tint.x, in.tint.y, in.tint.z};
            r.seed = in.seed;
            if (i < cls.context.size()) {
                r.wetness = cls.context[i].wetnessModifier;
                r.snow = cls.context[i].snowModifier;
                for (size_t k = 0; k < r.params.size(); ++k) r.params[k] = cls.context[i].params[k];
            }
            chunk.instances.push_back(r);
        }
    }
    for (auto& [key, chunk] : chunks) file.chunks.push_back(std::move(chunk));
    return file;
}

WorldPart partFromInstances(const InstanceFile& file) {
    WorldPart part;
    part.ok = true;
    part.cacheKey = file.cacheKey;
    part.generatorVersion = file.generatorVersion;
    for (int i = 0; i < 4; ++i) part.region[i] = file.region[static_cast<size_t>(i)];
    for (const InstanceFileClass& c : file.classes) {
        WorldClassInstances cls;
        cls.id = c.id;
        cls.ref = c.ref;
        cls.salt = c.salt;
        cls.variants = c.variants;
        cls.unique = c.unique;
        cls.params = c.params;
        part.classes.push_back(std::move(cls));
    }
    for (const InstanceChunk& chunk : file.chunks) {
        for (const InstanceRecord& r : chunk.instances) {
            WorldClassInstances& cls = part.classes[r.classIndex];
            Scatter::Instance in;
            in.classId = cls.salt;
            in.variant = r.variant;
            in.cellX = r.cellX;
            in.cellZ = r.cellZ;
            in.position = Vec3(r.position[0], r.position[1], r.position[2]);
            in.up = Vec3(r.up[0], r.up[1], r.up[2]);
            in.yaw = r.yaw;
            in.scale = r.scale;
            in.tint = Vec3(r.tint[0], r.tint[1], r.tint[2]);
            in.seed = r.seed;
            InstanceContext ctx;
            ctx.wetnessModifier = r.wetness;
            ctx.snowModifier = r.snow;
            for (size_t k = 0; k < r.params.size(); ++k) ctx.params[k] = r.params[k];
            cls.instances.push_back(in);
            cls.context.push_back(ctx);
            ++part.total;
        }
    }
    // Back to the canonical order inside a class (by cell), whatever the chunking.
    for (WorldClassInstances& cls : part.classes) {
        std::vector<size_t> order(cls.instances.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const Scatter::Instance &x = cls.instances[a], &y = cls.instances[b];
            return x.cellZ != y.cellZ ? x.cellZ < y.cellZ : x.cellX < y.cellX;
        });
        std::vector<Scatter::Instance> instances;
        std::vector<InstanceContext> context;
        for (const size_t i : order) {
            instances.push_back(cls.instances[i]);
            context.push_back(cls.context[i]);
        }
        cls.instances = std::move(instances);
        cls.context = std::move(context);
    }
    return part;
}

std::vector<u8> encodeInstanceFile(const InstanceFile& file) {
    Writer w;
    w.raw(kMagic, sizeof(kMagic));
    w.u32v(kInstanceFileVersion);
    w.u32v(file.generatorVersion);
    w.u64v(file.cacheKey);
    w.u64v(file.instanceHash);
    for (const f32 v : file.region) w.f32v(v);
    w.f32v(file.chunkSize);
    w.u32v(static_cast<u32>(file.classes.size()));
    for (const InstanceFileClass& c : file.classes) {
        w.text(c.id);
        w.text(c.ref);
        w.u32v(c.salt);
        w.u32v(c.variants);
        w.u32v(c.unique ? 1u : 0u);
        w.u32v(static_cast<u32>(c.params.size()));
        for (const std::string& name : c.params) w.text(name);
    }
    w.u32v(static_cast<u32>(file.chunks.size()));
    for (const InstanceChunk& chunk : file.chunks) {
        w.i32v(chunk.chunkX);
        w.i32v(chunk.chunkZ);
        w.u32v(static_cast<u32>(chunk.instances.size()));
        for (const InstanceRecord& r : chunk.instances) {
            w.u32v(r.classIndex);
            w.u32v(r.variant);
            w.i32v(r.cellX);
            w.i32v(r.cellZ);
            for (const f32 v : r.position) w.f32v(v);
            for (const f32 v : r.up) w.f32v(v);
            w.f32v(r.yaw);
            w.f32v(r.scale);
            for (const f32 v : r.tint) w.f32v(v);
            w.u64v(r.seed);
            w.f32v(r.wetness);
            w.f32v(r.snow);
            for (const f32 v : r.params) w.f32v(v);
        }
    }
    return std::move(w.bytes());
}

bool decodeInstanceFile(const std::vector<u8>& bytes, InstanceFile& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    Reader r(bytes);
    if (!r.magic()) return fail("not an instance bake (.cinst)");
    const u32 version = r.u32v();
    if (version == 0 || version > kInstanceFileVersion) return fail("the bake was written by a newer engine");
    InstanceFile file;
    file.generatorVersion = r.u32v();
    file.cacheKey = r.u64v();
    file.instanceHash = r.u64v();
    for (f32& v : file.region) v = r.f32v();
    file.chunkSize = r.f32v();
    const u32 classCount = r.u32v();
    if (!r.ok() || !r.has(static_cast<size_t>(classCount) * 24)) return fail("the bake is truncated");
    for (u32 i = 0; i < classCount && r.ok(); ++i) {
        InstanceFileClass c;
        c.id = r.text();
        c.ref = r.text();
        c.salt = r.u32v();
        c.variants = r.u32v();
        c.unique = r.u32v() != 0;
        const u32 params = r.u32v();
        if (params > kMaxBakeParams) return fail("a class has too many bake parameters");
        for (u32 k = 0; k < params && r.ok(); ++k) c.params.push_back(r.text());
        file.classes.push_back(std::move(c));
    }
    const u32 chunkCount = r.u32v();
    if (!r.ok() || !r.has(static_cast<size_t>(chunkCount) * 12)) return fail("the bake is truncated");
    for (u32 i = 0; i < chunkCount && r.ok(); ++i) {
        InstanceChunk chunk;
        chunk.chunkX = r.i32v();
        chunk.chunkZ = r.i32v();
        const u32 count = r.u32v();
        if (!r.has(static_cast<size_t>(count) * 92)) break;
        chunk.instances.resize(count);
        for (InstanceRecord& rec : chunk.instances) {
            rec.classIndex = r.u32v();
            rec.variant = r.u32v();
            rec.cellX = r.i32v();
            rec.cellZ = r.i32v();
            for (f32& v : rec.position) v = r.f32v();
            for (f32& v : rec.up) v = r.f32v();
            rec.yaw = r.f32v();
            rec.scale = r.f32v();
            for (f32& v : rec.tint) v = r.f32v();
            rec.seed = r.u64v();
            rec.wetness = r.f32v();
            rec.snow = r.f32v();
            for (f32& v : rec.params) v = r.f32v();
            if (rec.classIndex >= classCount) return fail("an instance names a class the bake does not have");
        }
        file.chunks.push_back(std::move(chunk));
    }
    if (!r.ok()) return fail("the bake is truncated");
    out = std::move(file);
    return true;
}

bool writeInstanceFile(const std::filesystem::path& path, const InstanceFile& file, std::string* error) {
    const std::vector<u8> bytes = encodeInstanceFile(file);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    // Write next to the target and rename, so a reader never sees half a file.
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + temp.string();
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            if (error) *error = "cannot write " + temp.string();
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        if (error) *error = "cannot rename to " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool readInstanceFile(const std::filesystem::path& path, InstanceFile& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path.string();
        return false;
    }
    std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeInstanceFile(bytes, out, error);
}

std::filesystem::path WorldCache::pathOf(u64 cacheKey) const {
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.cinst", static_cast<unsigned long long>(cacheKey));
    return m_directory / name;
}

std::optional<InstanceFile> WorldCache::find(u64 cacheKey) const {
    if (cacheKey == 0) return std::nullopt;
    InstanceFile file;
    if (!readInstanceFile(pathOf(cacheKey), file) || file.cacheKey != cacheKey) return std::nullopt;
    return file;
}

bool WorldCache::store(const InstanceFile& file, std::string* error) const {
    return writeInstanceFile(pathOf(file.cacheKey), file, error);
}

std::optional<InstanceFile> WorldCache::buildOrLoad(const WorldBuildRequest& request, WorldPart* part, bool* hit,
                                                    f32 chunkSize) const {
    if (hit) *hit = false;
    if (std::optional<InstanceFile> cached = find(cacheKeyOf(request))) {
        if (hit) *hit = true;
        return cached;
    }
    WorldPart built = build(request);
    std::optional<InstanceFile> result;
    if (built.ok) {
        InstanceFile file = bakeInstances(built, chunkSize);
        store(file);
        result = std::move(file);
    }
    if (part) *part = std::move(built);
    return result;
}

}  // namespace Caffeine::World
