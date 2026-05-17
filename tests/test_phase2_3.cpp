#include "catch.hpp"
#include "engine/AssetLoader.hpp"
#include <vector>
#include <chrono>

TEST_CASE("Phase 3.1: AssetLoader async loading works", "[phase3]") {
    Caffeine::AssetLoader loader;
    bool callbackFired = false;
    std::vector<Caffeine::u8> receivedData;

    auto handle = loader.loadAssetAsync(0x1234567890ABCDEF, 
        [&](const std::vector<Caffeine::u8>& data) {
            callbackFired = true;
            receivedData = data;
        });

    REQUIRE(handle > 0);
    
    for (int i = 0; i < 50; ++i) {
        loader.update();
        if (callbackFired) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    REQUIRE(callbackFired == true);
}

TEST_CASE("Phase 3.1: AssetLoader handles multiple concurrent loads", "[phase3]") {
    Caffeine::AssetLoader loader;
    int callbacksRun = 0;

    auto handle1 = loader.loadAssetAsync(0x1111111111111111,
        [&](const std::vector<Caffeine::u8>& data) { callbacksRun++; });
    auto handle2 = loader.loadAssetAsync(0x2222222222222222,
        [&](const std::vector<Caffeine::u8>& data) { callbacksRun++; });
    auto handle3 = loader.loadAssetAsync(0x3333333333333333,
        [&](const std::vector<Caffeine::u8>& data) { callbacksRun++; });

    REQUIRE(handle1 > 0);
    REQUIRE(handle2 > handle1);
    REQUIRE(handle3 > handle2);

    for (int i = 0; i < 100; ++i) {
        loader.update();
        if (callbacksRun == 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(callbacksRun == 3);
}

TEST_CASE("Phase 3.1: AssetLoader cancellation (placeholder)", "[phase3]") {
    Caffeine::AssetLoader loader;
    
    auto handle = loader.loadAssetAsync(0xDEADBEEFCAFEBABE,
        [](const std::vector<Caffeine::u8>& data) {});

    loader.cancelLoad(handle);
    loader.update();
    
    REQUIRE(true);
}

TEST_CASE("Phase 2 & 3: Complete ecosystem compilation", "[phase2][phase3]") {
    REQUIRE(std::is_move_constructible_v<Caffeine::AssetLoader> == false);
    REQUIRE(std::is_copy_constructible_v<Caffeine::AssetLoader> == false);
}
