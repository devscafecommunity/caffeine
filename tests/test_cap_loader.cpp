#include "catch.hpp"
#include "../src/editor/CapLoader.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>

using namespace Caffeine;
using namespace Caffeine::Editor;

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

// Helper to create a minimal valid CAP file for testing
static void createTestCapFile(const std::filesystem::path& path) {
    std::ofstream file(path.string(), std::ios::binary);
    REQUIRE(file.is_open());
    
    // Write CAP header
    CapHeader capHeader;
    capHeader.magic = CAP_MAGIC;
    capHeader.version = CAP_VERSION;
    capHeader.assetCount = 2;
    capHeader.tableOffset = 64;
    capHeader.tableSize = 2 * sizeof(CapEntry);
    capHeader.dataOffset = 64 + capHeader.tableSize;
    
    // Create two test CAF blobs
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
    
    uint64_t caf1Offset = capHeader.dataOffset;
    uint64_t caf2Offset = caf1Offset + sizeof(CafHeader) + caf1.payloadSize;
    
    capHeader.totalSize = caf2Offset + sizeof(CafHeader) + caf2.payloadSize;
    
    // Write CAP header
    file.write(reinterpret_cast<const char*>(&capHeader), sizeof(capHeader));
    
    // Write entry table
    CapEntry entry1;
    entry1.hashID = 0x12345678ABCDEF00;
    entry1.offset = caf1Offset;
    entry1.size = sizeof(CafHeader) + caf1.payloadSize;
    
    CapEntry entry2;
    entry2.hashID = 0xFEDCBA9876543210;
    entry2.offset = caf2Offset;
    entry2.size = sizeof(CafHeader) + caf2.payloadSize;
    
    file.write(reinterpret_cast<const char*>(&entry1), sizeof(entry1));
    file.write(reinterpret_cast<const char*>(&entry2), sizeof(entry2));
    
    // Write first CAF blob (texture)
    file.write(reinterpret_cast<const char*>(&caf1), sizeof(caf1));
    std::vector<uint8_t> payload1(16, 0xAA);
    file.write(reinterpret_cast<const char*>(payload1.data()), payload1.size());
    
    // Write second CAF blob (audio)
    file.write(reinterpret_cast<const char*>(&caf2), sizeof(caf2));
    std::vector<uint8_t> payload2(32, 0xBB);
    file.write(reinterpret_cast<const char*>(payload2.data()), payload2.size());
    
    file.close();
}

TEST_CASE("CapLoader - loadCap returns empty vector for non-existent file", "[editor][caploader]") {
    auto assets = CapLoader::loadCap("/nonexistent/path/test.cap");
    REQUIRE(assets.empty());
}

TEST_CASE("CapLoader - loadCap returns empty vector for invalid magic", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_invalid_magic.cap";
    
    // Create file with invalid magic
    std::ofstream file(tempPath.string(), std::ios::binary);
    CapHeader header;
    header.magic = 0xDEADBEEF;  // Invalid magic
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.close();
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.empty());
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap returns empty vector for unsupported version", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_bad_version.cap";
    
    std::ofstream file(tempPath.string(), std::ios::binary);
    CapHeader header;
    header.magic = CAP_MAGIC;
    header.version = 999;  // Unsupported version
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.close();
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.empty());
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap extracts correct number of assets", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_valid.cap";
    createTestCapFile(tempPath);
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap extracts asset with correct hashID", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_hashid.cap";
    createTestCapFile(tempPath);
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE_FALSE(assets.empty());
    REQUIRE(assets[0].hashID == 0x12345678ABCDEF00);
    REQUIRE(assets[1].hashID == 0xFEDCBA9876543210);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap identifies asset types correctly", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_types.cap";
    createTestCapFile(tempPath);
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);
    REQUIRE(assets[0].type == Assets::CafAssetType::Texture);
    REQUIRE(assets[1].type == Assets::CafAssetType::Audio);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap stores CAF blob data", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_blobs.cap";
    createTestCapFile(tempPath);
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);
    
    // First asset should have CafHeader + 16 bytes payload
    REQUIRE(assets[0].cafBlob.size() == sizeof(CafHeader) + 16);
    
    // Second asset should have CafHeader + 32 bytes payload
    REQUIRE(assets[1].cafBlob.size() == sizeof(CafHeader) + 32);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap parses CAF metadata correctly", "[editor][caploader]") {
    std::filesystem::path tempPath = "/tmp/test_cap_metadata.cap";
    createTestCapFile(tempPath);
    
    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);
    
    // Check first asset metadata
    REQUIRE(assets[0].metadata.magic == CAF_MAGIC);
    REQUIRE(assets[0].metadata.version == CAF_VERSION);
    REQUIRE(assets[0].metadata.assetType == static_cast<uint8_t>(CafAssetType::Texture));
    REQUIRE(assets[0].metadata.payloadSize == 16);
    
    // Check second asset metadata
    REQUIRE(assets[1].metadata.magic == CAF_MAGIC);
    REQUIRE(assets[1].metadata.version == CAF_VERSION);
    REQUIRE(assets[1].metadata.assetType == static_cast<uint8_t>(CafAssetType::Audio));
    REQUIRE(assets[1].metadata.payloadSize == 32);
    
    std::filesystem::remove(tempPath);
}
