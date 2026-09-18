#include "terrain/TerrainSerializer.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace Caffeine::Terrain {

bool TerrainSerializer::save(const std::filesystem::path& path,
                             const TerrainHeightmap& heightmap,
                             const TerrainSplatmap& splatmap,
                             bool includeSplat) {
    if (heightmap.empty()) return false;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const u32 flags = includeSplat && !splatmap.empty() ? kFlagHasSplat : 0u;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    const u32 signature = kSignature;
    const u32 version = kVersion;
    out.write(reinterpret_cast<const char*>(&signature), sizeof(signature));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&resX), sizeof(resX));
    out.write(reinterpret_cast<const char*>(&resZ), sizeof(resZ));
    out.write(reinterpret_cast<const char*>(&flags), sizeof(flags));

    const auto& heights = heightmap.heights();
    out.write(reinterpret_cast<const char*>(heights.data()),
              static_cast<std::streamsize>(heights.size() * sizeof(f32)));

    if (flags & kFlagHasSplat) {
        const auto& weights = splatmap.weights();
        out.write(reinterpret_cast<const char*>(weights.data()),
                  static_cast<std::streamsize>(weights.size() * sizeof(Vec4)));
    }

    return static_cast<bool>(out);
}

bool TerrainSerializer::load(const std::filesystem::path& path,
                             TerrainHeightmap& heightmap,
                             TerrainSplatmap& splatmap,
                             bool* hadSplat) {
    if (hadSplat) *hadSplat = false;

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

    if (!in || signature != kSignature || version != kVersion) return false;
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

    splatmap.resize(resX, resZ);
    if (flags & kFlagHasSplat) {
        const usize splatBytes = heightCount * sizeof(Vec4);
        if (static_cast<usize>(fileSize) < minSize + splatBytes) return false;

        std::vector<Vec4> weights(heightCount);
        in.read(reinterpret_cast<char*>(weights.data()), static_cast<std::streamsize>(splatBytes));
        if (!in) return false;

        std::copy(weights.begin(), weights.end(), splatmap.weights().begin());
        if (hadSplat) *hadSplat = true;
    } else {
        splatmap.fillLayer(3, 1.0f);
    }

    return true;
}

}  // namespace Caffeine::Terrain
