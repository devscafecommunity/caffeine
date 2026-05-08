#include "catch.hpp"
#include "../src/assets/AssetManager.hpp"
#include "../src/core/io/CafWriter.hpp"
#include "../src/core/io/CafTypes.hpp"

#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>

using namespace Caffeine;
using namespace Caffeine::IO;
using namespace Caffeine::Assets;

namespace {

const char* kTexturePath = "am_test_texture.caf";
const char* kAudioPath   = "am_test_audio.caf";
const char* kShaderPath  = "am_test_shader.caf";

void writeTextureCaf(const char* path, u32 w, u32 h) {
    TextureMetadata meta{};
    meta.width     = w;
    meta.height    = h;
    meta.format    = 0;
    meta.mipLevels = 1;

    const u32 pixelCount = w * h * 4;
    u8* pixels = new u8[pixelCount];
    std::memset(pixels, 0xAB, pixelCount);

    CafWriter::write(path, AssetType::Texture, CAF_FLAG_NONE,
                     &meta, sizeof(meta), pixels, pixelCount);
    delete[] pixels;
}

void writeAudioCaf(const char* path) {
    AudioMetadata meta{};
    meta.sampleRate    = 44100;
    meta.channels      = 2;
    meta.bitsPerSample = 16;
    meta.sampleCount   = 512;

    const u32 dataSize = meta.sampleCount * meta.channels * (meta.bitsPerSample / 8);
    u8* pcm = new u8[dataSize];
    std::memset(pcm, 0, dataSize);

    CafWriter::write(path, AssetType::Audio, CAF_FLAG_NONE,
                     &meta, sizeof(meta), pcm, dataSize);
    delete[] pcm;
}

void writeShaderCaf(const char* path) {
    ShaderMetadata meta{};
    meta.stage    = 0;
    meta.reserved = 0;

    u8 bytecode[64]{};
    std::memset(bytecode, 0xCC, sizeof(bytecode));

    CafWriter::write(path, AssetType::Shader, CAF_FLAG_NONE,
                     &meta, sizeof(meta), bytecode, sizeof(bytecode));
}

struct TestFixture {
    TestFixture() {
        writeTextureCaf(kTexturePath, 16, 16);
        writeAudioCaf(kAudioPath);
        writeShaderCaf(kShaderPath);
    }
    ~TestFixture() {
        std::remove(kTexturePath);
        std::remove(kAudioPath);
        std::remove(kShaderPath);
    }
};

} // anonymous namespace

TEST_CASE("AssetManager - construction with null JobSystem", "[assets]") {
    AssetManager mgr(nullptr, "");
    CacheStats s = mgr.cacheStats();
    REQUIRE(s.totalCachedBytes == 0);
    REQUIRE(s.textureCount == 0);
    REQUIRE(s.audioCount == 0);
    REQUIRE(s.pendingJobs == 0);
}

TEST_CASE("AssetManager - loadSync Texture", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto handle = mgr.loadSync<Texture>(kTexturePath);

    REQUIRE(handle.isValid());
    REQUIRE(handle.isReady());
    REQUIRE(handle.get() != nullptr);

    const Texture* tex = handle.get();
    REQUIRE(tex->width        == 16);
    REQUIRE(tex->height       == 16);
    REQUIRE(tex->mipLevels    == 1);
    REQUIRE(tex->pixels       != nullptr);
    REQUIRE(tex->pixelDataSize == 16 * 16 * 4);
}

TEST_CASE("AssetManager - loadSync AudioClip", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto handle = mgr.loadSync<AudioClip>(kAudioPath);

    REQUIRE(handle.isReady());
    const AudioClip* clip = handle.get();
    REQUIRE(clip != nullptr);
    REQUIRE(clip->sampleRate    == 44100);
    REQUIRE(clip->channels      == 2);
    REQUIRE(clip->bitsPerSample == 16);
    REQUIRE(clip->sampleCount   == 512);
    REQUIRE(clip->pcmData       != nullptr);
}

TEST_CASE("AssetManager - loadSync ShaderBlob", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto handle = mgr.loadSync<ShaderBlob>(kShaderPath);

    REQUIRE(handle.isReady());
    const ShaderBlob* sh = handle.get();
    REQUIRE(sh != nullptr);
    REQUIRE(sh->stage        == 0);
    REQUIRE(sh->bytecode     != nullptr);
    REQUIRE(sh->bytecodeSize == 64);
}

TEST_CASE("AssetManager - second load of same path is cache hit", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto h1 = mgr.loadSync<Texture>(kTexturePath);
    auto h2 = mgr.loadSync<Texture>(kTexturePath);

    REQUIRE(h1.id() == h2.id());

    CacheStats s = mgr.cacheStats();
    REQUIRE(s.cacheHitRate > 0.0f);
}

TEST_CASE("AssetManager - cacheStats counts textures and audio", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto th = mgr.loadSync<Texture>(kTexturePath);
    auto ah = mgr.loadSync<AudioClip>(kAudioPath);

    CacheStats s = mgr.cacheStats();
    REQUIRE(s.textureCount    == 1);
    REQUIRE(s.audioCount      == 1);
    REQUIRE(s.totalCachedBytes > 0);
}

TEST_CASE("AssetManager - collectGarbage unloads unreferenced assets", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    {
        auto handle = mgr.loadSync<Texture>(kTexturePath);
        REQUIRE(handle.isReady());
    }

    mgr.collectGarbage();

    CacheStats s = mgr.cacheStats();
    REQUIRE(s.textureCount    == 0);
    REQUIRE(s.totalCachedBytes == 0);
}

TEST_CASE("AssetManager - collectGarbage does not unload referenced assets", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto handle = mgr.loadSync<Texture>(kTexturePath);
    REQUIRE(handle.isReady());

    mgr.collectGarbage();

    REQUIRE(handle.isReady());
    CacheStats s = mgr.cacheStats();
    REQUIRE(s.textureCount == 1);
}

TEST_CASE("AssetManager - handle copy increments ref, original still valid", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    auto h1 = mgr.loadSync<Texture>(kTexturePath);
    auto h2 = h1;

    REQUIRE(h1.isReady());
    REQUIRE(h2.isReady());
    REQUIRE(h1.id() == h2.id());
}

TEST_CASE("AssetManager - loadAsync returns handle that becomes ready", "[assets]") {
    TestFixture fix;
    Threading::JobSystem jobs(2);
    AssetManager mgr(&jobs, "");

    auto handle = mgr.loadAsync<Texture>(kTexturePath);

    REQUIRE(handle.isValid());

    const int maxWaitMs = 2000;
    int waited = 0;
    while (!handle.isReady() && waited < maxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10;
    }

    REQUIRE(handle.isReady());
    REQUIRE(handle.get() != nullptr);
    REQUIRE(handle.get()->width == 16);
}

TEST_CASE("AssetManager - failed load returns handle with Failed status", "[assets]") {
    AssetManager mgr(nullptr, "");

    auto handle = mgr.loadSync<Texture>("nonexistent_file_xyz.caf");

    REQUIRE(handle.isValid());
    REQUIRE_FALSE(handle.isReady());
    REQUIRE(handle.get() == nullptr);
}

TEST_CASE("AssetManager - tick advances frame index", "[assets]") {
    AssetManager mgr(nullptr, "");
    mgr.tick(42);
    CacheStats s = mgr.cacheStats();
    REQUIRE(s.totalCachedBytes == 0);
}
