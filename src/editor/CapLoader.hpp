#pragma once
#include "core/Types.hpp"
#include <vector>
#include <filesystem>

#ifdef CF_HAS_CAF_PACK
#include "../caf-pack/include/caffeine/CafTypes.hpp"
#endif

namespace Caffeine::Editor {

struct CapAssetMetadata {
    uint32_t magic;
    uint32_t version;
    uint8_t assetType;
    uint8_t reserved[7];
    uint32_t payloadSize;
    uint32_t flags;
    uint64_t crc64;
};

class CapLoader {
public:
    struct LoadedAsset {
        uint64_t hashID;
#ifdef CF_HAS_CAF_PACK
        Caffeine::Assets::CafAssetType type;
#else
        uint8_t type;
#endif
        std::vector<u8> cafBlob;
        CapAssetMetadata metadata;
    };

    static std::vector<LoadedAsset> loadCap(const std::filesystem::path& path);

private:
#ifdef CF_HAS_CAF_PACK
    static Caffeine::Assets::CafAssetType identifyAssetType(const CapAssetMetadata& metadata);
#endif
};

}
