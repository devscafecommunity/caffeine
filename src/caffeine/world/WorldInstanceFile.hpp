#pragma once

// The instance bake of a world (`.cinst`, doppio/world-composition.md section 5): every instance of a build, grouped
// in square chunks so a renderer or a streamer can load and draw one chunk at a time (one instance buffer per chunk
// and class). Data, not entities: thousands of trees load as a few buffers instead of thousands of scene objects.
//
// Binary, little-endian, versioned:
//
//   header   "CINST\0\0\0", u32 format version (1), u32 generator_version, u64 cache key, u64 instance hash,
//            f32 region[4], f32 chunk size
//   classes  u32 count, then per class: string id, string ref, u32 salt, u32 variants, u32 unique
//   chunks   u32 count, then per chunk (rows of z, then x): i32 cx, i32 cz, u32 count, then `count` records
//   record   u32 class index, u32 variant, i32 cell x, i32 cell z, f32 position[3], f32 up[3], f32 yaw, f32 scale,
//            f32 tint[3], u64 seed, f32 wetness modifier, f32 snow modifier        (76 bytes)
//
// A string is a u32 byte count and the bytes. Inside a chunk, records keep placement order (class priority, then
// cell), so the file of a build is byte-for-byte the same on every machine.
//
// The cache (`WorldCache`) stores bakes under `<dir>/<cache key as 16 hex digits>.cinst`: the key covers the
// composition, every recipe it used, the baked terrain and the region, so a hit is always the right world.

#include "core/Types.hpp"
#include "world/WorldBuild.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Caffeine::World {

inline constexpr u32 kInstanceFileVersion = 1;
inline constexpr f32 kDefaultInstanceChunk = 64.0f;

struct InstanceRecord {
    u32 classIndex = 0;
    u32 variant = 0;
    i32 cellX = 0;
    i32 cellZ = 0;
    std::array<f32, 3> position{};
    std::array<f32, 3> up{0.0f, 1.0f, 0.0f};
    f32 yaw = 0.0f;
    f32 scale = 1.0f;
    std::array<f32, 3> tint{1.0f, 1.0f, 1.0f};
    u64 seed = 0;
    f32 wetness = 0.0f;
    f32 snow = 0.0f;
};

struct InstanceFileClass {
    std::string id;
    std::string ref;
    u32 salt = 0;
    u32 variants = 1;
    bool unique = false;
};

struct InstanceChunk {
    i32 chunkX = 0;
    i32 chunkZ = 0;
    std::vector<InstanceRecord> instances;
};

struct InstanceFile {
    u32 generatorVersion = 0;
    u64 cacheKey = 0;
    u64 instanceHash = 0;
    std::array<f32, 4> region{};
    f32 chunkSize = kDefaultInstanceChunk;
    std::vector<InstanceFileClass> classes;
    std::vector<InstanceChunk> chunks;

    u32 instanceCount() const;
    /// Every instance in placement order (chunk by chunk).
    std::vector<InstanceRecord> flatten() const;
};

/// The bake of a successful build, its instances bucketed into chunks of `chunkSize` metres.
InstanceFile bakeInstances(const WorldPart& part, f32 chunkSize = kDefaultInstanceChunk);

/// A build result rebuilt from a bake (classes and instances, the run-time modifiers in the context; no water and
/// no terrain): what a cache hit hands to code written for `WorldPart`.
WorldPart partFromInstances(const InstanceFile& file);

std::vector<u8> encodeInstanceFile(const InstanceFile& file);
/// False (and why in `error`) for a wrong magic, a newer format or a truncated file.
bool decodeInstanceFile(const std::vector<u8>& bytes, InstanceFile& out, std::string* error = nullptr);

bool writeInstanceFile(const std::filesystem::path& path, const InstanceFile& file, std::string* error = nullptr);
bool readInstanceFile(const std::filesystem::path& path, InstanceFile& out, std::string* error = nullptr);

/// Bakes on disk, keyed by the build's cache key.
class WorldCache {
public:
    explicit WorldCache(std::filesystem::path directory) : m_directory(std::move(directory)) {}

    std::filesystem::path pathOf(u64 cacheKey) const;
    /// The bake of `cacheKey`, if one was stored and still reads.
    std::optional<InstanceFile> find(u64 cacheKey) const;
    bool store(const InstanceFile& file, std::string* error = nullptr) const;

    /// The cached bake of `request` or, on a miss, a fresh build baked and stored. `part` receives the build when
    /// one ran (its errors when it failed); `hit` says whether the cache answered.
    std::optional<InstanceFile> buildOrLoad(const WorldBuildRequest& request, WorldPart* part = nullptr,
                                            bool* hit = nullptr, f32 chunkSize = kDefaultInstanceChunk) const;

private:
    std::filesystem::path m_directory;
};

}  // namespace Caffeine::World
