#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>

namespace Caffeine::Test {

constexpr uint32_t CAP_MAGIC = 0x4341502F;
constexpr uint32_t CAP_VERSION = 1;
constexpr uint32_t CAF_MAGIC = 0x43414621;
constexpr uint32_t CAF_VERSION = 1;

struct CapHeader {
    uint32_t magic = CAP_MAGIC;
    uint32_t version = CAP_VERSION;
    uint32_t assetCount = 0;
    uint32_t reserved1 = 0;
    uint64_t tableOffset = 64;
    uint64_t tableSize = 0;
    uint64_t dataOffset = 0;
    uint32_t totalSize = 0;
    uint64_t crc64 = 0;
    uint32_t reserved2 = 0;
    uint32_t reserved3 = 0;
};

struct CapEntry {
    uint64_t hashID = 0;
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t reserved = 0;
};

struct CafHeader {
    uint32_t magic = CAF_MAGIC;
    uint32_t version = CAF_VERSION;
    uint8_t assetType = 0;
    uint8_t reserved[7] = {0};
    uint32_t payloadSize = 0;
    uint32_t flags = 0;
    uint64_t crc64 = 0;
};

enum class CafAssetType : uint8_t {
    Unknown = 0,
    Texture = 1,
    Audio = 2,
    Mesh = 3,
    Script = 4,
    Animation = 5,
    Tileset = 6
};

inline void writeEmptyCapFile(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    CapHeader capHeader{};
    capHeader.magic = CAP_MAGIC;
    capHeader.version = CAP_VERSION;
    capHeader.assetCount = 0;
    capHeader.tableOffset = 64;
    capHeader.tableSize = 0;
    capHeader.dataOffset = 64;
    capHeader.totalSize = 64;
    capHeader.crc64 = 0;
    file.write(reinterpret_cast<const char*>(&capHeader), sizeof(CapHeader));
}

inline void writeSampleCapFile(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);

    CapHeader capHeader;
    capHeader.magic = CAP_MAGIC;
    capHeader.version = CAP_VERSION;
    capHeader.assetCount = 2;
    capHeader.tableOffset = 64;
    capHeader.tableSize = 2 * sizeof(CapEntry);
    capHeader.dataOffset = 64 + capHeader.tableSize;

    CafHeader caf1;
    caf1.magic = CAF_MAGIC;
    caf1.version = CAF_VERSION;
    caf1.assetType = static_cast<uint8_t>(CafAssetType::Texture);
    caf1.payloadSize = 16;

    CafHeader caf2;
    caf2.magic = CAF_MAGIC;
    caf2.version = CAF_VERSION;
    caf2.assetType = static_cast<uint8_t>(CafAssetType::Audio);
    caf2.payloadSize = 32;

    const uint64_t caf1Offset = capHeader.dataOffset;
    const uint64_t caf2Offset = caf1Offset + sizeof(CafHeader) + caf1.payloadSize;
    capHeader.totalSize = static_cast<uint32_t>(caf2Offset + sizeof(CafHeader) + caf2.payloadSize);

    file.write(reinterpret_cast<const char*>(&capHeader), sizeof(capHeader));

    CapEntry entry1;
    entry1.hashID = 0x12345678ABCDEF00;
    entry1.offset = caf1Offset;
    entry1.size = static_cast<uint32_t>(sizeof(CafHeader) + caf1.payloadSize);

    CapEntry entry2;
    entry2.hashID = 0xFEDCBA9876543210;
    entry2.offset = caf2Offset;
    entry2.size = static_cast<uint32_t>(sizeof(CafHeader) + caf2.payloadSize);

    file.write(reinterpret_cast<const char*>(&entry1), sizeof(entry1));
    file.write(reinterpret_cast<const char*>(&entry2), sizeof(entry2));
    file.write(reinterpret_cast<const char*>(&caf1), sizeof(caf1));
    std::vector<uint8_t> payload1(16, 0xAA);
    file.write(reinterpret_cast<const char*>(payload1.data()), payload1.size());
    file.write(reinterpret_cast<const char*>(&caf2), sizeof(caf2));
    std::vector<uint8_t> payload2(32, 0xBB);
    file.write(reinterpret_cast<const char*>(payload2.data()), payload2.size());
}

}  // namespace Caffeine::Test
