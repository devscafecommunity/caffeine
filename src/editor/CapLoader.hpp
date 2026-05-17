#pragma once
#include "core/Types.hpp"
#include <vector>
#include <filesystem>
#include "../caf-pack/include/caffeine/CafTypes.hpp"

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
        Caffeine::Assets::CafAssetType type;
        std::vector<u8> cafBlob;
        CapAssetMetadata metadata;
    };

    static std::vector<LoadedAsset> loadCap(const std::filesystem::path& path);

private:
    static Caffeine::Assets::CafAssetType identifyAssetType(const CapAssetMetadata& metadata);
};

}
