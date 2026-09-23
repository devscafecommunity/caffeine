#include "editor/CapLoader.hpp"
#include "debug/LogSystem.hpp"

#include <cstring>

#ifdef CF_HAS_CAF_PACK
#include "caf-pack/Reader.hpp"
#endif

namespace Caffeine::Editor {

#ifdef CF_HAS_CAF_PACK

std::vector<CapLoader::LoadedAsset> CapLoader::loadCap(const std::filesystem::path& path) {
    std::vector<LoadedAsset> assets;
    std::string error;
    const auto loaded = CafPack::Reader::loadCap(path, &error);
    if (loaded.empty() && !error.empty()) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader", "%s",
                                         error.c_str());
        return assets;
    }

    for (const CafPack::CafAsset& caf : loaded) {
        CapAssetMetadata metadata{
            .magic = caf.header.magic,
            .version = caf.header.version,
            .assetType = caf.header.assetType,
            .reserved = {0},
            .payloadSize = caf.header.payloadSize,
            .flags = caf.header.flags,
            .crc64 = caf.header.crc64,
        };
        std::memcpy(metadata.reserved, caf.header.reserved, sizeof(metadata.reserved));

        assets.push_back(LoadedAsset{
            .hashID = caf.hashID,
            .type = static_cast<Caffeine::Assets::CafAssetType>(caf.header.assetType),
            .cafBlob = caf.blob,
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
    Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader",
        "CAP loading not available - caf-pack submodule not included");
    return {};
}

#endif

}  // namespace Caffeine::Editor
