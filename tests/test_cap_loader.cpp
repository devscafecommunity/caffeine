#include "catch.hpp"
#include "cap_test_helpers.hpp"
#include "../src/editor/CapLoader.hpp"
#include <fstream>
#include <filesystem>

using namespace Caffeine;
using namespace Caffeine::Editor;
using namespace Caffeine::Test;

#ifdef CF_HAS_CAF_PACK
#include "caffeine/CafTypes.hpp"
using CafAssetTypeEnum = Caffeine::Assets::CafAssetType;
#else
using CafAssetTypeEnum = CafAssetType;
#endif

static uint8_t assetTypeValue(const CapLoader::LoadedAsset& asset) {
#ifdef CF_HAS_CAF_PACK
    return static_cast<uint8_t>(asset.type);
#else
    return asset.type;
#endif
}

TEST_CASE("CapLoader - loadCap returns empty vector for non-existent file", "[editor][caploader]") {
    auto assets = CapLoader::loadCap("/nonexistent/path/test.cap");
    REQUIRE(assets.empty());
}

TEST_CASE("CapLoader - loadCap returns empty vector for invalid magic", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_invalid_magic.cap";

    std::ofstream file(tempPath, std::ios::binary);
    CapHeader header;
    header.magic = 0xDEADBEEF;
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.close();

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.empty());

    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap returns empty vector for unsupported version", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_bad_version.cap";

    std::ofstream file(tempPath, std::ios::binary);
    CapHeader header;
    header.magic = CAP_MAGIC;
    header.version = 999;
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.close();

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.empty());

    std::filesystem::remove(tempPath);
}

#ifdef CF_HAS_CAF_PACK

TEST_CASE("CapLoader - loadCap extracts correct number of assets", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_valid.cap";
    writeSampleCapFile(tempPath);

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);

    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap extracts asset with correct hashID", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_hashid.cap";
    writeSampleCapFile(tempPath);

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE_FALSE(assets.empty());
    REQUIRE(assets[0].hashID == 0x12345678ABCDEF00);
    REQUIRE(assets[1].hashID == 0xFEDCBA9876543210);

    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap identifies asset types correctly", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_types.cap";
    writeSampleCapFile(tempPath);

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);
    REQUIRE(assetTypeValue(assets[0]) == static_cast<uint8_t>(CafAssetTypeEnum::Texture));
    REQUIRE(assetTypeValue(assets[1]) == static_cast<uint8_t>(CafAssetTypeEnum::Audio));

    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap stores CAF blob data", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_blobs.cap";
    writeSampleCapFile(tempPath);

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);
    REQUIRE(assets[0].cafBlob.size() == sizeof(CafHeader) + 16);
    REQUIRE(assets[1].cafBlob.size() == sizeof(CafHeader) + 32);

    std::filesystem::remove(tempPath);
}

TEST_CASE("CapLoader - loadCap parses CAF metadata correctly", "[editor][caploader]") {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_cap_metadata.cap";
    writeSampleCapFile(tempPath);

    auto assets = CapLoader::loadCap(tempPath);
    REQUIRE(assets.size() == 2);

    REQUIRE(assets[0].metadata.magic == CAF_MAGIC);
    REQUIRE(assets[0].metadata.version == CAF_VERSION);
    REQUIRE(assets[0].metadata.assetType == static_cast<uint8_t>(CafAssetType::Texture));
    REQUIRE(assets[0].metadata.payloadSize == 16);

    REQUIRE(assets[1].metadata.magic == CAF_MAGIC);
    REQUIRE(assets[1].metadata.version == CAF_VERSION);
    REQUIRE(assets[1].metadata.assetType == static_cast<uint8_t>(CafAssetType::Audio));
    REQUIRE(assets[1].metadata.payloadSize == 32);

    std::filesystem::remove(tempPath);
}

#endif
