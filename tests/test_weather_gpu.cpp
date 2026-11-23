#include "catch.hpp"

#include "../src/caffeine/render/GpuTextureCache.hpp"
#include "../src/caffeine/render/PostProcessStack.hpp"
#include "../src/caffeine/rhi/CommandBuffer.hpp"
#include "../src/caffeine/rhi/RenderDevice.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::RHI;
using Caffeine::Render::PostProcessInputs;
using Caffeine::Render::PostProcessStack;
using Caffeine::Render::PostWeather;

// The composite pass draws the weather: exponential height fog (distance, sky) and screen-space rain. These run the
// real post stack on a known scene (a flat grey, two depths and the sky) and read the picture back. Needs a GPU
// (skipped otherwise).

namespace {

constexpr u32 kSize = 64;

struct Gpu {
    RenderDevice device;
    PostProcessStack post;
    Texture* scene = nullptr;
    Texture* depthNear = nullptr;  // every pixel 5 m away
    Texture* depthSplit = nullptr; // left half 5 m, right half 300 m
    Texture* depthSky = nullptr;   // the sky
    Texture* output = nullptr;
    Mat4 proj;
    bool ok = false;

    Gpu() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
        RenderConfig config;
        config.width = kSize;
        config.height = kSize;
        if (!device.initOffscreen(config)) return;
        if (!post.init(&device)) return;
        proj = Mat4::perspective(1.0f, 1.0f, 0.1f, 1000.0f);

        // Mid grey scene colour.
        std::vector<u8> grey(kSize * kSize * 4, 128);
        for (u32 i = 0; i < kSize * kSize; ++i) grey[i * 4 + 3] = 255;
        scene = makeTexture(TextureFormat::R8G8B8A8_UNORM, TextureUsage::Sampler);
        if (!scene || !device.uploadTexture(scene, grey.data(), kSize, kSize, 4)) return;

        depthNear = depthTexture([&](u32, u32) { return ndcDepth(5.0f); });
        depthSplit = depthTexture([&](u32 x, u32) { return ndcDepth(x < kSize / 2 ? 5.0f : 300.0f); });
        depthSky = depthTexture([&](u32, u32) { return 1.0f; });
        output = makeTexture(TextureFormat::R8G8B8A8_UNORM, TextureUsage::Sampler | TextureUsage::ColorTarget);
        ok = depthNear && depthSplit && depthSky && output;
    }

    ~Gpu() {
        post.shutdown();
        device.shutdown();
    }

    Texture* makeTexture(TextureFormat format, TextureUsage usage) {
        TextureDesc desc;
        desc.width = kSize;
        desc.height = kSize;
        desc.format = format;
        desc.usage = usage;
        return device.createTexture(desc);
    }

    /// The projected depth (NDC z) of a point `distance` metres straight ahead.
    f32 ndcDepth(f32 distance) const {
        const f32 clipZ = proj(2, 2) * -distance + proj(2, 3);
        return clipZ / distance;
    }

    template <typename F>
    Texture* depthTexture(F value) {
        Texture* texture = makeTexture(TextureFormat::R32_FLOAT, TextureUsage::Sampler);
        if (!texture) return nullptr;
        std::vector<f32> data(kSize * kSize);
        for (u32 y = 0; y < kSize; ++y)
            for (u32 x = 0; x < kSize; ++x) data[y * kSize + x] = value(x, y);
        return device.uploadTexture(texture, data.data(), kSize, kSize, 4) ? texture : nullptr;
    }

    /// One composite pass with the weather and a depth image; returns the RGBA8 result.
    std::vector<u8> run(Texture* depth, const PostWeather& weather) {
        ECS::PostProcessComponent settings;
        settings.enabled = false;  // the composite alone: no tone-mapping choices, no history
        PostProcessInputs in;
        in.sceneColor = scene;
        in.sceneDepth = depth;
        in.sceneWidth = kSize;
        in.sceneHeight = kSize;
        in.output = output;
        in.outputWidth = kSize;
        in.outputHeight = kSize;
        in.proj = proj;
        in.viewProj = proj;  // the camera sits at the origin looking down -Z
        in.view = Mat4::identity();
        in.prevViewProj = proj;
        in.cameraPos = Vec3(0.0f, 0.0f, 0.0f);
        in.time = 1.0f;
        in.viewId = 1;
        in.weather = weather;
        CommandBuffer* cmd = device.beginOffscreen();
        post.execute(cmd, settings, in);
        device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(device.readTexture(output, pixels));
        device.releaseOffscreenResources();
        return pixels;
    }
};

/// Whole-picture equality as a bool, so a failure does not print the pixels.
bool same(const std::vector<u8>& a, const std::vector<u8>& b) { return a == b; }

f32 mean(const std::vector<u8>& img, u32 x0, u32 x1) {
    f64 sum = 0.0;
    u32 n = 0;
    for (u32 y = 0; y < kSize; ++y)
        for (u32 x = x0; x < x1; ++x)
            for (u32 c = 0; c < 3; ++c) {
                sum += img[(y * kSize + x) * 4 + c];
                ++n;
            }
    return static_cast<f32>(sum / n);
}

PostWeather fog(f32 density) {
    PostWeather w;
    w.fog = true;
    w.fogDensity = density;
    w.fogHeightFalloff = 0.0f;  // the same density at every height: the test is about distance
    w.fogMaxOpacity = 1.0f;
    w.fogColor = Vec3(0.0f, 0.0f, 0.0f);  // black: the grey scene fades towards it, whatever the tone curve does
    w.fogSkyBlend = 0.0f;
    w.fogSunScatter = 0.0f;
    return w;
}

PostWeather rain(f32 intensity) {
    PostWeather w;
    w.rain = true;
    w.rainIntensity = intensity;
    w.rainBrightness = 1.0f;
    w.rainColor = Vec3(1.0f, 1.0f, 1.0f);
    return w;
}

}  // namespace

TEST_CASE("Weather post: nothing on leaves the picture alone", "[weather][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    const std::vector<u8> a = gpu.run(gpu.depthSplit, PostWeather{});
    const std::vector<u8> b = gpu.run(gpu.depthSplit, PostWeather{});
    CHECK(same(a, b));
    // A fog or rain that is configured but switched off changes nothing either.
    PostWeather inert = fog(0.5f);
    inert.fog = false;
    inert.rainIntensity = 1.0f;
    inert.rain = false;
    CHECK(same(gpu.run(gpu.depthSplit, inert), a));
    // The picture is the grey scene, whatever the grading does to it.
    CHECK(mean(a, 0, kSize / 2) == Approx(mean(a, kSize / 2, kSize)).margin(0.5));
}

TEST_CASE("Weather post: fog hides what is far and leaves what is near", "[weather][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    const std::vector<u8> clear = gpu.run(gpu.depthSplit, PostWeather{});
    const std::vector<u8> foggy = gpu.run(gpu.depthSplit, fog(0.01f));
    const f32 nearClear = mean(clear, 0, kSize / 2);
    const f32 nearFog = mean(foggy, 0, kSize / 2);
    const f32 farClear = mean(clear, kSize / 2, kSize);
    const f32 farFog = mean(foggy, kSize / 2, kSize);
    INFO("near " << nearClear << " -> " << nearFog << ", far " << farClear << " -> " << farFog);
    // 5 m of this fog is 5 percent, 300 m is 95: the far half goes to the (black) fog colour, the near one hardly moves.
    CHECK(nearClear - nearFog < 0.2f * nearClear);
    CHECK(farClear - farFog > 0.6f * farClear);
    CHECK(nearFog > farFog + 20.0f);

    // Denser fog hides more at the same distance.
    const std::vector<u8> thicker = gpu.run(gpu.depthSplit, fog(0.05f));
    CHECK(mean(thicker, 0, kSize / 2) < nearFog);

    // The start distance keeps the fog off what is closer than it.
    PostWeather started = fog(0.05f);
    started.fogStart = 50.0f;
    const std::vector<u8> late = gpu.run(gpu.depthSplit, started);
    CHECK(mean(late, 0, kSize / 2) == Approx(nearClear).margin(1.0));
    CHECK(mean(late, kSize / 2, kSize) < farClear - 20.0f);

    // A maximum opacity caps how far the picture goes to the fog colour.
    PostWeather capped = fog(0.05f);
    capped.fogMaxOpacity = 0.3f;
    const std::vector<u8> cappedImage = gpu.run(gpu.depthSplit, capped);
    CHECK(mean(cappedImage, kSize / 2, kSize) > farFog);
    CHECK(mean(cappedImage, kSize / 2, kSize) < farClear);
}

TEST_CASE("Weather post: the sky fades into the fog and rain falls on it", "[weather][gpu]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    const std::vector<u8> clear = gpu.run(gpu.depthSky, PostWeather{});
    // Rain adds bright streaks on the sky (no geometry hides a layer there) and changes with time.
    const std::vector<u8> wet = gpu.run(gpu.depthSky, rain(1.0f));
    size_t brighter = 0;
    for (size_t i = 0; i < clear.size(); i += 4) {
        const int a = (clear[i] + clear[i + 1] + clear[i + 2]) / 3;
        const int b = (wet[i] + wet[i + 1] + wet[i + 2]) / 3;
        if (b - a >= 10) ++brighter;
    }
    INFO(brighter << " pixels brighter");
    CHECK(brighter > 20);
    CHECK_FALSE(same(wet, clear));
    CHECK(same(gpu.run(gpu.depthSky, rain(0.0f)), clear));

    // Near geometry hides the far layers: 1 m away nothing of the rain shows (the nearest layer is at 3 m).
    Gpu& g = gpu;
    Texture* veryNear = g.depthTexture([&](u32, u32) { return g.ndcDepth(1.0f); });
    REQUIRE(veryNear != nullptr);
    const std::vector<u8> shelterClear = g.run(veryNear, PostWeather{});
    const std::vector<u8> sheltered = g.run(veryNear, rain(1.0f));
    size_t streaks = 0;
    for (size_t i = 0; i < shelterClear.size(); i += 4) {
        if (sheltered[i] > shelterClear[i] + 10) ++streaks;
    }
    CHECK(streaks == 0);

    // Fog on the sky: it fades towards the (black) fog colour.
    const std::vector<u8> hazy = gpu.run(gpu.depthSky, fog(0.01f));
    CHECK(mean(hazy, 0, kSize) < mean(clear, 0, kSize) - 20.0f);
}

TEST_CASE("Weather post: a lightning flash lights the rain and the fog", "[weather][gpu][lightning]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    // The rain flares for the length of the flash.
    PostWeather storm = rain(0.8f);
    const std::vector<u8> dark = gpu.run(gpu.depthSky, storm);
    storm.flash = 1.0f;
    storm.flashColor = Vec3(1.0f, 1.0f, 1.0f);
    const std::vector<u8> lit = gpu.run(gpu.depthSky, storm);
    INFO("mean " << mean(dark, 0, kSize) << " -> " << mean(lit, 0, kSize));
    CHECK(mean(lit, 0, kSize) > mean(dark, 0, kSize) + 1.0f);

    // The fog glows: a grey scene seen through a (black) fog gets brighter when the flash is in it.
    PostWeather mist = fog(0.01f);
    const std::vector<u8> mistDark = gpu.run(gpu.depthSplit, mist);
    mist.flash = 1.0f;
    mist.flashColor = Vec3(1.0f, 1.0f, 1.0f);
    const std::vector<u8> mistLit = gpu.run(gpu.depthSplit, mist);
    CHECK(mean(mistLit, kSize / 2, kSize) > mean(mistDark, kSize / 2, kSize));
    // No flash, no change.
    mist.flash = 0.0f;
    CHECK(same(gpu.run(gpu.depthSplit, mist), mistDark));
}

TEST_CASE("Weather post: the wind slants the rain and the picture stays alive", "[weather][gpu][wind]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    PostWeather still = rain(1.0f);
    PostWeather windy = rain(1.0f);
    windy.rainWind = Vec3(25.0f, 0.0f, 0.0f);
    const std::vector<u8> a = gpu.run(gpu.depthSky, still);
    const std::vector<u8> b = gpu.run(gpu.depthSky, windy);
    CHECK_FALSE(same(a, b));
    // Gusting changes the streaks, not how much rain falls.
    const f32 ma = mean(a, 0, kSize);
    const f32 mb = mean(b, 0, kSize);
    INFO(ma << " vs " << mb);
    CHECK(mb > ma * 0.6f);
    CHECK(mb < ma * 1.6f);
    // Splashes live where the rain meets an upward-facing surface near the camera: a floor 1.5 m below the camera
    // gets rings and sparks, and with the strength at 0 it gets none.
    Texture* floor = gpu.depthTexture([&](u32, u32 y) {
        const f32 ndcY = 1.0f - 2.0f * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kSize);
        const f32 dirY = ndcY * std::tan(0.5f);
        return dirY < -0.05f ? gpu.ndcDepth(1.5f / -dirY) : 1.0f;
    });
    REQUIRE(floor != nullptr);
    PostWeather splashy = rain(1.0f);
    splashy.rainSplashes = 1.0f;
    PostWeather dry = rain(1.0f);
    dry.rainSplashes = 0.0f;
    const std::vector<u8> wet = gpu.run(floor, splashy);
    const std::vector<u8> bare = gpu.run(floor, dry);
    size_t sparks = 0;
    for (size_t i = 0; i < wet.size(); i += 4) {
        if (wet[i] > bare[i] + 6) ++sparks;
    }
    INFO(sparks << " splash pixels");
    CHECK(sparks > 3);
    CHECK(mean(wet, 0, kSize) > mean(bare, 0, kSize));
}

TEST_CASE("Weather post: light shafts stream from the sun and only while it is ahead", "[weather][gpu][shafts]") {
    Gpu gpu;
    if (!gpu.ok) {
        WARN("no GPU: skipped");
        return;
    }
    // An HDR sky: a dim grey with a very bright disc where the sun is.
    std::vector<f32> hdr(kSize * kSize * 4);
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const f32 dx = static_cast<f32>(x) - 32.0f;
            const f32 dy = static_cast<f32>(y) - 32.0f;
            const f32 v = (dx * dx + dy * dy < 25.0f) ? 8.0f : 0.3f;
            for (u32 c = 0; c < 3; ++c) hdr[(y * kSize + x) * 4 + c] = v;
            hdr[(y * kSize + x) * 4 + 3] = 1.0f;
        }
    }
    TextureDesc desc;
    desc.width = kSize;
    desc.height = kSize;
    desc.format = TextureFormat::R32G32B32A32_FLOAT;
    desc.usage = TextureUsage::Sampler;
    Texture* sky = gpu.device.createTexture(desc);
    REQUIRE(sky != nullptr);
    REQUIRE(gpu.device.uploadTexture(sky, hdr.data(), kSize, kSize, 16));
    Texture* original = gpu.scene;
    gpu.scene = sky;

    PostWeather none;
    PostWeather shafts;
    shafts.shafts = 1.0f;
    shafts.shaftSun = Vec2(0.5f, 0.5f);
    shafts.sunAhead = true;
    const std::vector<u8> plain = gpu.run(gpu.depthSky, none);
    const std::vector<u8> lit = gpu.run(gpu.depthSky, shafts);
    // A pixel between the screen's edge and the sun gets light from the disc.
    auto at = [&](const std::vector<u8>& img, u32 x, u32 y) { return static_cast<int>(img[(y * kSize + x) * 4]); };
    INFO("plain " << at(plain, 12, 32) << " shafts " << at(lit, 12, 32));
    CHECK(at(lit, 12, 32) > at(plain, 12, 32));
    // The sun behind the camera, or the strength at 0: the picture is untouched.
    PostWeather behind = shafts;
    behind.sunAhead = false;
    CHECK(same(gpu.run(gpu.depthSky, behind), plain));
    PostWeather zero = shafts;
    zero.shafts = 0.0f;
    CHECK(same(gpu.run(gpu.depthSky, zero), plain));
    // Geometry in front of the sky blocks the shafts: with the near wall everywhere the picture is the plain one.
    CHECK(same(gpu.run(gpu.depthNear, shafts), gpu.run(gpu.depthNear, none)));

    gpu.scene = original;
}
