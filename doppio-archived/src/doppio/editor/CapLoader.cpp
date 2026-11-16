#include "editor/CapLoader.hpp"
#include "debug/LogSystem.hpp"

#include <cstring>
#include <fstream>

#ifdef CF_HAS_CAF_PACK
#include "caffeine/CafTypes.hpp"
#endif

namespace Caffeine::Editor {

#ifdef CF_HAS_CAF_PACK

std::vector<CapLoader::LoadedAsset> CapLoader::loadCap(const std::filesystem::path& path) {
    std::vector<LoadedAsset> assets;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
                                         "Failed to open CAP file: %s", path.string().c_str());
        return assets;
    }

    using Caffeine::Assets::CapHeader;
    using Caffeine::Assets::CapEntry;
    using Caffeine::Assets::CafHeader;
    using Caffeine::Assets::CAP_MAGIC;
    using Caffeine::Assets::CAP_VERSION;
    using Caffeine::Assets::CAF_MAGIC;

    CapHeader cap{};
    file.read(reinterpret_cast<char*>(&cap), sizeof(cap));
    if (!file || file.gcount() != static_cast<std::streamsize>(sizeof(cap))) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
                                         "CAP header truncated: %s", path.string().c_str());
        return assets;
    }
    if (cap.magic != CAP_MAGIC) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
                                         "Invalid CAP magic in %s", path.string().c_str());
        return assets;
    }
    if (cap.version != CAP_VERSION) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
                                         "Unsupported CAP version %u in %s", cap.version,
                                         path.string().c_str());
        return assets;
    }

    if (cap.assetCount == 0) return assets;

    file.seekg(static_cast<std::streamoff>(cap.tableOffset), std::ios::beg);
    if (!file) return assets;

    std::vector<CapEntry> entries(cap.assetCount);
    file.read(reinterpret_cast<char*>(entries.data()),
              static_cast<std::streamsize>(cap.assetCount * sizeof(CapEntry)));
    if (!file) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
                                         "CAP entry table truncated: %s", path.string().c_str());
        return assets;
    }

    for (const CapEntry& entry : entries) {
        if (entry.size < sizeof(CafHeader)) continue;

        std::vector<u8> blob(entry.size);
        file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(entry.size));
        if (!file) continue;

        CafHeader cafHeader{};
        std::memcpy(&cafHeader, blob.data(), sizeof(CafHeader));
        if (cafHeader.magic != CAF_MAGIC) continue;

        CapAssetMetadata metadata{
            .magic = cafHeader.magic,
            .version = cafHeader.version,
            .assetType = cafHeader.assetType,
            .reserved = {0},
            .payloadSize = cafHeader.payloadSize,
            .flags = cafHeader.flags,
            .crc64 = cafHeader.crc64,
        };
        std::memcpy(metadata.reserved, cafHeader.reserved, sizeof(metadata.reserved));

        assets.push_back(LoadedAsset{
            .hashID = entry.hashID,
            .type = static_cast<Caffeine::Assets::CafAssetType>(cafHeader.assetType),
            .cafBlob = std::move(blob),
            .metadata = metadata,
        });
    }

    return assets;
}

Caffeine::Assets::CafAssetType CapLoader::identifyAssetType(const CapAssetMetadata& metadata) {
    return static_cast<Caffeine::Assets::CafAssetType>(metadata.assetType);
}

#else

std::vector<CapLoader::LoadedAsset> CapLoader::loadCap(const std::filesystem::path& path) {
    (void)path;
    Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
        "CAP loading not available - caf-pack submodule not included");
    return {};
}

#endif

}  // namespace Caffeine::Editor
