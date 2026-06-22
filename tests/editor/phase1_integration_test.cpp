#include "catch.hpp"
#include "editor/CapLoader.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/AudioWaveformRenderer.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>

using namespace Caffeine;
using namespace Caffeine::Editor;
using namespace Caffeine::Assets;

TEST_CASE("Phase 1: Asset Browser Initialization", "[phase1][asset_browser]") {
    auto testProjectPath = std::filesystem::temp_directory_path() / "caffeine_test_phase1";
    if (std::filesystem::exists(testProjectPath)) {
        std::filesystem::remove_all(testProjectPath);
    }
    std::filesystem::create_directories(testProjectPath);

    SECTION("AssetBrowser initializes in Filesystem mode") {
        AssetBrowser browser;
        browser.init((testProjectPath / "assets").string().c_str());
        
        REQUIRE(browser.browseMode() == AssetBrowser::BrowseMode::Filesystem);
        REQUIRE(browser.entryCount() == 0);
    }

    // Cleanup
    if (std::filesystem::exists(testProjectPath)) {
        std::filesystem::remove_all(testProjectPath);
    }
}

TEST_CASE("Phase 1: CAP File Browsing", "[phase1][cap_loader]") {
    auto testProjectPath = std::filesystem::temp_directory_path() / "caffeine_test_phase1";
    if (std::filesystem::exists(testProjectPath)) {
        std::filesystem::remove_all(testProjectPath);
    }
    std::filesystem::create_directories(testProjectPath);

    SECTION("AssetBrowser can switch to CAP mode") {
        auto capPath = testProjectPath / "game.cap";
        
        std::ofstream file(capPath, std::ios::binary);
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
        file.close();

        AssetBrowser browser;
        browser.init((testProjectPath / "assets").string().c_str());
        browser.loadCapFile(capPath);
        
        REQUIRE(browser.browseMode() == AssetBrowser::BrowseMode::CapFile);
    }

    // Cleanup
    if (std::filesystem::exists(testProjectPath)) {
        std::filesystem::remove_all(testProjectPath);
    }
}

TEST_CASE("Phase 1: CAP Loader Robustness", "[phase1][cap_loader]") {
    auto testProjectPath = std::filesystem::temp_directory_path() / "caffeine_test_phase1";
    if (std::filesystem::exists(testProjectPath)) {
        std::filesystem::remove_all(testProjectPath);
    }
    std::filesystem::create_directories(testProjectPath);

    SECTION("CapLoader handles empty CAP files") {
        auto capPath = testProjectPath / "empty.cap";
        
        std::ofstream file(capPath, std::ios::binary);
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
        file.close();

        auto assets = CapLoader::loadCap(capPath);
        REQUIRE(assets.empty());
    }

    // Cleanup
    if (std::filesystem::exists(testProjectPath)) {
        std::filesystem::remove_all(testProjectPath);
    }
}

TEST_CASE("Phase 1: Audio Waveform Rendering", "[phase1][audio_waveform]") {
    SECTION("AudioWaveformRenderer generates waveform from empty blob") {
        std::vector<u8> emptyBlob(1024, 0);
        auto waveform = AudioWaveformRenderer::generateWaveform(emptyBlob);
        
        REQUIRE(waveform.sampleRate == 0);
        REQUIRE(!waveform.isStereo);
    }
}
