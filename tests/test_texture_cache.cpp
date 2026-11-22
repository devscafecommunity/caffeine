#include "catch.hpp"

#include "../src/caffeine/render/GpuTextureCache.hpp"
#include "../src/caffeine/rhi/RenderDevice.hpp"

#include <SDL3/SDL.h>
#include <stb/stb_image_write.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::RHI;
using Caffeine::Render::GpuTextureCache;

// A draw asks the texture cache for the same pictures every frame. The cache answers from memory and looks at the
// disk again only every half second, remembers pictures that are missing, and still notices a file that changes
// (a bake rewriting it) or an explicit invalidate. Needs a GPU (skipped otherwise).

namespace {

struct Gpu {
    RenderDevice device;
    bool ok = false;

    Gpu() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
        RenderConfig config;
        config.width = 64;
        config.height = 64;
        ok = device.initOffscreen(config);
    }
    ~Gpu() {
        GpuTextureCache::instance().releaseAll(&device);
        device.shutdown();
    }
};

/// Writes a size x size picture of one grey level.
void writePicture(const std::filesystem::path& path, u8 grey, int size = 16) {
    std::vector<u8> pixels(static_cast<size_t>(size) * size * 4, grey);
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    REQUIRE(stbi_write_png(path.string().c_str(), size, size, 4, pixels.data(), size * 4) != 0);
}

void waitPastRecheck() { std::this_thread::sleep_for(std::chrono::milliseconds(650)); }

std::filesystem::path scratch(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine_texture_cache_test";
    std::filesystem::create_directories(dir);
    return dir / name;
}

}  // namespace

TEST_CASE("Texture cache: a texture asked for again comes from memory, not from the disk", "[texturecache][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    auto& cache = GpuTextureCache::instance();
    const auto path = scratch("fast.png");
    writePicture(path, 120);

    RHI::Texture* first = cache.acquire(&gpu.device, path.string(), "");
    REQUIRE(first != nullptr);
    // The file is gone, but the entry was checked a moment ago: a frame's worth of draws must not touch the disk.
    std::filesystem::remove(path);
    CHECK(cache.acquire(&gpu.device, path.string(), "") == first);
    CHECK(cache.acquire(&gpu.device, path.string(), "") == first);
    // Another tier or colour space is another entry, and is not confused with this one.
    CHECK(cache.acquire(&gpu.device, path.string(), "", 0, true) == nullptr);  // not loaded yet and the file is gone
}

TEST_CASE("Texture cache: a missing picture is remembered, and found once it exists", "[texturecache][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    auto& cache = GpuTextureCache::instance();
    const auto path = scratch("late.png");
    std::filesystem::remove(path);

    CHECK(cache.acquire(&gpu.device, path.string(), "") == nullptr);
    writePicture(path, 200);
    // Remembered miss: asking the disk for a file that is not there every frame is the cost this avoids.
    CHECK(cache.acquire(&gpu.device, path.string(), "") == nullptr);
    waitPastRecheck();
    CHECK(cache.acquire(&gpu.device, path.string(), "") != nullptr);
    std::filesystem::remove(path);
}

TEST_CASE("Texture cache: a file rewritten on disk is reloaded after the recheck, or at once on invalidate",
          "[texturecache][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    auto& cache = GpuTextureCache::instance();
    const auto path = scratch("reload.png");
    writePicture(path, 50);
    RHI::Texture* first = cache.acquire(&gpu.device, path.string(), "");
    REQUIRE(first != nullptr);

    // A bake rewrites the file (a later timestamp, whatever the file system's resolution).
    writePicture(path, 220);
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    CHECK(cache.acquire(&gpu.device, path.string(), "") == first);  // inside the recheck window
    waitPastRecheck();
    RHI::Texture* second = cache.acquire(&gpu.device, path.string(), "");
    REQUIRE(second != nullptr);
    CHECK(second != first);

    // invalidate() does not wait for the window.
    writePicture(path, 90);
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10));
    CHECK(cache.acquire(&gpu.device, path.string(), "") == second);
    cache.invalidate(std::filesystem::absolute(path).string());
    RHI::Texture* third = cache.acquire(&gpu.device, path.string(), "");
    REQUIRE(third != nullptr);
    CHECK(third != second);
    std::filesystem::remove(path);
}

TEST_CASE("Texture cache: quality tiers are separate entries", "[texturecache][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    auto& cache = GpuTextureCache::instance();
    const auto path = scratch("tiers.png");
    writePicture(path, 140, 64);
    RHI::Texture* full = cache.acquire(&gpu.device, path.string(), "", 0);
    RHI::Texture* half = cache.acquire(&gpu.device, path.string(), "", 1);
    REQUIRE(full != nullptr);
    REQUIRE(half != nullptr);
    CHECK(full != half);
    CHECK(full->width == 64);
    CHECK(half->width == 32);
    CHECK(cache.acquire(&gpu.device, path.string(), "", 0) == full);
    CHECK(cache.acquire(&gpu.device, path.string(), "", 1) == half);
    std::filesystem::remove(path);
}
