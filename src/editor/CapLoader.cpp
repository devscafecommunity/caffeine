#include "editor/CapLoader.hpp"
#include "debug/LogSystem.hpp"
#include <fstream>
#include <cstring>

namespace Caffeine::Editor {

using namespace Caffeine::Assets;

constexpr uint32_t CAP_MAGIC = 0x4341502F;
constexpr uint32_t CAP_VERSION = 1;

std::vector<CapLoader::LoadedAsset> CapLoader::loadCap(const std::filesystem::path& path) {
    std::vector<LoadedAsset> assets;
    std::ifstream file(path.string(), std::ios::binary);
    
    if (!file) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader", 
            "Failed to open CAP file: %s", path.string().c_str());
        return assets;
    }

    CapHeader capHeader{};
    file.read(reinterpret_cast<char*>(&capHeader), sizeof(CapHeader));
    
    if (capHeader.magic != CAP_MAGIC) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader", 
            "Invalid CAP magic bytes");
        return assets;
    }

    if (capHeader.version != CAP_VERSION) {
        Debug::LogSystem::instance().log(Debug::LogLevel::Error, "CapLoader", 
            "Unsupported CAP version");
        return assets;
    }

    std::vector<CapEntry> entries(capHeader.assetCount);
    file.read(reinterpret_cast<char*>(entries.data()), 
              capHeader.assetCount * sizeof(CapEntry));

    for (const auto& entry : entries) {
        file.seekg(entry.offset);
        
        std::vector<u8> cafBlob(entry.size);
        file.read(reinterpret_cast<char*>(cafBlob.data()), entry.size);
        
        CafHeader cafHeader{};
        std::memcpy(&cafHeader, cafBlob.data(), sizeof(CafHeader));
        
        Editor::CapAssetMetadata metadata{
            .magic = cafHeader.magic,
            .version = cafHeader.version,
            .assetType = cafHeader.assetType,
            .reserved = {0},
            .payloadSize = cafHeader.payloadSize,
            .flags = cafHeader.flags,
            .crc64 = cafHeader.crc64
        };
        std::memcpy(metadata.reserved, cafHeader.reserved, 7);
        
        LoadedAsset asset{
            .hashID = entry.hashID,
            .type = identifyAssetType(metadata),
            .cafBlob = cafBlob,
            .metadata = metadata
        };
        
        assets.push_back(asset);
    }

    return assets;
}

Caffeine::Assets::CafAssetType CapLoader::identifyAssetType(const Editor::CapAssetMetadata& metadata) {
    return static_cast<Caffeine::Assets::CafAssetType>(metadata.assetType);
}

}
