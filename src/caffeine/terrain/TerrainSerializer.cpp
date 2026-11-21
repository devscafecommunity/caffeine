#include "terrain/TerrainSerializer.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace Caffeine::Terrain {

bool TerrainSerializer::save(const std::filesystem::path& path,
                             const TerrainHeightmap& heightmap,
                             const TerrainSplatmap& splatmap,
                             bool includeSplat,
                             const TerrainMasks* masks) {
    if (heightmap.empty()) return false;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const bool hasMasks = masks && !masks->empty();
    const u32 flags = (includeSplat && !splatmap.empty() ? kFlagHasSplat : 0u) | (hasMasks ? kFlagHasMasks : 0u);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    const u32 signature = kSignature;
    const u32 version = hasMasks ? kVersion : 2u;
    out.write(reinterpret_cast<const char*>(&signature), sizeof(signature));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&resX), sizeof(resX));
    out.write(reinterpret_cast<const char*>(&resZ), sizeof(resZ));
    out.write(reinterpret_cast<const char*>(&flags), sizeof(flags));

    const auto& heights = heightmap.heights();
    out.write(reinterpret_cast<const char*>(heights.data()),
              static_cast<std::streamsize>(heights.size() * sizeof(f32)));

    if (flags & kFlagHasSplat) {
        const u32 splatResX = splatmap.resolutionX();
        const u32 splatResZ = splatmap.resolutionZ();
        out.write(reinterpret_cast<const char*>(&splatResX), sizeof(splatResX));
        out.write(reinterpret_cast<const char*>(&splatResZ), sizeof(splatResZ));
        const auto& weights = splatmap.weights();
        out.write(reinterpret_cast<const char*>(weights.data()),
                  static_cast<std::streamsize>(weights.size() * sizeof(Vec4)));
    }

    if (hasMasks) {
        const u32 count = static_cast<u32>(masks->stored().size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const TerrainMasks::Stored& mask : masks->stored()) {
            const u32 nameLength = static_cast<u32>(mask.name.size());
            out.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
            out.write(mask.name.data(), static_cast<std::streamsize>(nameLength));
            out.write(reinterpret_cast<const char*>(&mask.resolutionX), sizeof(mask.resolutionX));
            out.write(reinterpret_cast<const char*>(&mask.resolutionZ), sizeof(mask.resolutionZ));
            out.write(reinterpret_cast<const char*>(mask.data.data()), static_cast<std::streamsize>(mask.data.size()));
        }
    }

    return static_cast<bool>(out);
}

bool TerrainSerializer::load(const std::filesystem::path& path,
                             TerrainHeightmap& heightmap,
                             TerrainSplatmap& splatmap,
                             bool* hadSplat,
                             TerrainMasks* masks) {
    if (hadSplat) *hadSplat = false;
    if (masks) masks->clear();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;

    const std::streampos fileSize = in.tellg();
    if (fileSize < 20) return false;
    in.seekg(0, std::ios::beg);

    u32 signature = 0;
    u32 version = 0;
    u32 resX = 0;
    u32 resZ = 0;
    u32 flags = 0;
    in.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&resX), sizeof(resX));
    in.read(reinterpret_cast<char*>(&resZ), sizeof(resZ));
    in.read(reinterpret_cast<char*>(&flags), sizeof(flags));

    if (!in || signature != kSignature || version < 1 || version > kVersion) return false;
    if (resX < 2 || resZ < 2) return false;

    const usize heightCount = static_cast<usize>(resX) * static_cast<usize>(resZ);
    const usize heightBytes = heightCount * sizeof(f32);
    const usize minSize = 20 + heightBytes;
    if (static_cast<usize>(fileSize) < minSize) return false;

    std::vector<f32> heights(heightCount);
    in.read(reinterpret_cast<char*>(heights.data()), static_cast<std::streamsize>(heightBytes));
    if (!in) return false;

    heightmap.resize(resX, resZ);
    std::copy(heights.begin(), heights.end(), heightmap.heights().begin());

    if (flags & kFlagHasSplat) {
        u32 splatResX = resX;
        u32 splatResZ = resZ;
        if (version >= 2) {
            in.read(reinterpret_cast<char*>(&splatResX), sizeof(splatResX));
            in.read(reinterpret_cast<char*>(&splatResZ), sizeof(splatResZ));
            if (!in || splatResX < 2 || splatResZ < 2) return false;
        }

        const usize splatCount = static_cast<usize>(splatResX) * static_cast<usize>(splatResZ);
        const usize splatBytes = splatCount * sizeof(Vec4);
        if (static_cast<usize>(fileSize) < minSize + splatBytes) return false;

        std::vector<Vec4> weights(splatCount);
        in.read(reinterpret_cast<char*>(weights.data()), static_cast<std::streamsize>(splatBytes));
        if (!in) return false;

        splatmap.resize(splatResX, splatResZ);
        std::copy(weights.begin(), weights.end(), splatmap.weights().begin());
        if (hadSplat) *hadSplat = true;
    } else {
        splatmap.resize(resX, resZ);
        splatmap.fillLayer(0, 1.0f);
    }

    if ((flags & kFlagHasMasks) && version >= 3) {
        u32 count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in || count > TerrainMasks::kMaxStoredMasks) return false;
        for (u32 i = 0; i < count; ++i) {
            u32 nameLength = 0;
            in.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
            if (!in || nameLength == 0 || nameLength > TerrainMasks::kMaxNameLength) return false;
            std::string name(nameLength, '\0');
            in.read(name.data(), static_cast<std::streamsize>(nameLength));
            u32 maskX = 0, maskZ = 0;
            in.read(reinterpret_cast<char*>(&maskX), sizeof(maskX));
            in.read(reinterpret_cast<char*>(&maskZ), sizeof(maskZ));
            if (!in || maskX < 2 || maskZ < 2 || static_cast<u64>(maskX) * maskZ > (1u << 26)) return false;
            std::vector<u8> data(static_cast<size_t>(maskX) * maskZ);
            in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!in) return false;
            if (masks && !masks->set(name, maskX, maskZ, data.data())) return false;
        }
    }

    return true;
}

}  // namespace Caffeine::Terrain
