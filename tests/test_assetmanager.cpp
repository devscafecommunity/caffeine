#include "catch.hpp"
#include "../src/caffeine/assets/AssetManager.hpp"
#include "../src/caffeine/assets/HotReloader.hpp"
#include "../src/caffeine/core/io/CafWriter.hpp"
#include "../src/caffeine/core/io/CafTypes.hpp"

#include <atomic>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
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
    REQUIRE(mgr.getStatus(handle.id(), handle.generation()) == LoadStatus::Failed);
}

TEST_CASE("AssetManager - stale handle after eviction returns nullptr", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    u32 id  = 0;
    u16 gen = 0;
    {
        auto loaded = mgr.loadSync<Texture>(kTexturePath);
        id  = loaded.id();
        gen = loaded.generation();
        REQUIRE(loaded.isReady());
        REQUIRE(gen == 1);
    }
    mgr.collectGarbage();

    AssetHandle<Texture> stale(&mgr, id, gen);
    REQUIRE_FALSE(stale.isReady());
    REQUIRE(stale.get() == nullptr);
    REQUIRE(mgr.getStatus(id, gen) == LoadStatus::Invalid);
    REQUIRE(mgr.entryGeneration(id) == 2);
}

TEST_CASE("AssetManager - invalidation callback fires on collectGarbage", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    std::pair<u32, u16> captured{~0u, 0};
    mgr.registerInvalidationCallback(
        [](const InvalidatedAsset& info, void* userData) {
            auto* out = static_cast<std::pair<u32, u16>*>(userData);
            out->first  = info.id;
            out->second = info.generation;
        },
        &captured);

    u32 id = 0;
    u16 gen = 0;
    {
        auto handle = mgr.loadSync<Texture>(kTexturePath);
        id  = handle.id();
        gen = handle.generation();
        REQUIRE(gen == 1);
    }
    mgr.collectGarbage();

    REQUIRE(captured.first == id);
    REQUIRE(captured.second == gen);
}

TEST_CASE("AssetManager - LRU evicts unreferenced assets when over budget", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "", 0);

    {
        auto first = mgr.loadSync<Texture>(kTexturePath);
        REQUIRE(first.isReady());
    }

    auto second = mgr.loadSync<AudioClip>(kAudioPath);
    REQUIRE(second.isReady());

    CacheStats stats = mgr.cacheStats();
    REQUIRE(stats.evictedCount >= 1);
}

TEST_CASE("AssetManager - reload after eviction works with new generation", "[assets]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");

    u32 id  = 0;
    u16 gen = 0;
    {
        auto first = mgr.loadSync<Texture>(kTexturePath);
        id  = first.id();
        gen = first.generation();
    }
    mgr.collectGarbage();

    auto second = mgr.loadSync<Texture>(kTexturePath);
    REQUIRE(second.id() == id);
    REQUIRE(second.generation() == gen + 1);
    REQUIRE(second.isReady());
    REQUIRE(second.get() != nullptr);
}

TEST_CASE("AssetManager - tick advances frame index", "[assets]") {
    AssetManager mgr(nullptr, "");
    mgr.tick(42);
    CacheStats s = mgr.cacheStats();
    REQUIRE(s.totalCachedBytes == 0);
}

// ============================================================================
// HotReloader tests
// ============================================================================

TEST_CASE("HotReloader - default state is not running", "[hotreload]") {
    HotReloader hr;
    REQUIRE_FALSE(hr.IsRunning());
}

TEST_CASE("HotReloader - Start/Stop lifecycle", "[hotreload]") {
    auto tmpDir = std::filesystem::temp_directory_path() / "caffeine_hr_start_stop";
    std::filesystem::create_directories(tmpDir);

    HotReloader hr;
    REQUIRE_FALSE(hr.IsRunning());
    hr.Start(nullptr, tmpDir, 10);
    REQUIRE(hr.IsRunning());
    hr.Stop();
    REQUIRE_FALSE(hr.IsRunning());
    hr.Stop();
    REQUIRE_FALSE(hr.IsRunning());

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("HotReloader - Start twice does not crash", "[hotreload]") {
    auto tmpDir = std::filesystem::temp_directory_path() / "caffeine_hr_start_twice";
    std::filesystem::create_directories(tmpDir);

    HotReloader hr;
    hr.Start(nullptr, tmpDir, 10);
    REQUIRE(hr.IsRunning());
    hr.Start(nullptr, tmpDir, 10);
    REQUIRE(hr.IsRunning());
    hr.Stop();
    REQUIRE_FALSE(hr.IsRunning());

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("HotReloader - Update is safe when not running", "[hotreload]") {
    HotReloader hr;
    hr.Update();

    auto tmpDir = std::filesystem::temp_directory_path() / "caffeine_hr_update_safe";
    std::filesystem::create_directories(tmpDir);
    hr.Start(nullptr, tmpDir);
    hr.Stop();
    hr.Update();
    REQUIRE_FALSE(hr.IsRunning());

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("HotReloader - integration reload and callback", "[hotreload]") {
    auto tmpDir = std::filesystem::temp_directory_path() / "caffeine_hr_reload";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);
    auto cafPath = tmpDir / "test_reload.caf";

    {
        TextureMetadata meta{};
        meta.width = 2; meta.height = 2; meta.format = 0; meta.mipLevels = 1;
        u8 pixels[16];
        std::memset(pixels, 0xAB, sizeof(pixels));
        CafWriter::write(cafPath.string().c_str(), AssetType::Texture,
                         CAF_FLAG_NONE, &meta, sizeof(meta), pixels, sizeof(pixels));
    }

    AssetManager mgr(nullptr, "");
    auto handle = mgr.loadSync<Texture>(cafPath.string().c_str());
    REQUIRE(handle.isReady());
    REQUIRE(handle.get()->pixels[0] == 0xAB);

    bool callbackFired = false;
    Caffeine::Assets::HotReloadedAsset callbackInfo;
    {
        HotReloader hr;
        hr.SetNotificationCallback([&](const Caffeine::Assets::HotReloadedAsset& a) {
            callbackFired = true;
            callbackInfo  = a;
        });
        hr.Start(&mgr, tmpDir, 10);

        std::this_thread::sleep_for(std::chrono::milliseconds(600));

        {
            TextureMetadata meta{};
            meta.width = 2; meta.height = 2; meta.format = 0; meta.mipLevels = 1;
            u8 pixels[16];
            std::memset(pixels, 0xCD, sizeof(pixels));
            CafWriter::write(cafPath.string().c_str(), AssetType::Texture,
                             CAF_FLAG_NONE, &meta, sizeof(meta), pixels, sizeof(pixels));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        hr.Update();

        REQUIRE(handle.get()->pixels[0] == 0xCD);
        REQUIRE(callbackFired);
        REQUIRE(callbackInfo.assetId == handle.id());
        REQUIRE(callbackInfo.type    == AssetType::Texture);

        hr.Stop();
    }

    std::filesystem::remove_all(tmpDir);
}

// ============================================================================
// Review fixes
// ============================================================================

TEST_CASE("AssetManager - a load with a tiny budget is not evicted before the handle holds it", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "", 0);  // every unreferenced asset is over budget
    auto handle = mgr.loadSync<Texture>(kTexturePath);
    REQUIRE(handle.isReady());
    REQUIRE(handle.get() != nullptr);
    auto async = mgr.loadAsync<AudioClip>(kAudioPath);
    REQUIRE(async.isReady());
    REQUIRE(handle.isReady());  // the second load does not push out the one that is held
}

TEST_CASE("AssetManager - the same file under different spellings is one asset", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");
    auto a = mgr.loadSync<Texture>(kTexturePath);
    auto b = mgr.loadSync<Texture>("./am_test_texture.caf");
    auto c = mgr.loadSync<Texture>("sub/../am_test_texture.caf");
    REQUIRE(a.isReady());
    REQUIRE(a.id() == b.id());
    REQUIRE(a.id() == c.id());
}

TEST_CASE("AssetManager - a path that climbs out of the base folder gives no handle", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");
    REQUIRE_FALSE(mgr.loadSync<Texture>("../am_test_texture.caf").isValid());
    REQUIRE_FALSE(mgr.loadSync<Texture>("a/../../am_test_texture.caf").isValid());
    REQUIRE_FALSE(mgr.loadSync<Texture>("").isValid());
    REQUIRE_FALSE(mgr.loadSync<Texture>(nullptr).isValid());
}

TEST_CASE("AssetManager - a file loaded as one type is refused as another", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");
    auto texture = mgr.loadSync<Texture>(kTexturePath);
    REQUIRE(texture.isReady());
    REQUIRE_FALSE(mgr.loadSync<AudioClip>(kTexturePath).isValid());  // same slot, other type
    // A file that holds another kind of asset than asked for fails to load instead of resolving garbage.
    auto wrong = mgr.loadSync<Texture>(kAudioPath);
    REQUIRE_FALSE(wrong.isReady());
}

TEST_CASE("AssetManager - several threads loading the same asset load it once", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");
    std::vector<std::thread> threads;
    std::atomic<int> ready{0};
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            auto handle = mgr.loadSync<Texture>(kTexturePath);
            if (handle.isReady() && handle.get() && handle.get()->width == 16) ++ready;
        });
    }
    for (auto& t : threads) t.join();
    REQUIRE(ready == 8);
    REQUIRE(mgr.cacheStats().textureCount == 1);
}

namespace {
AssetManager* g_callbackManager = nullptr;
int g_callbacks = 0;
void callbackThatCallsBack(const InvalidatedAsset& info, void*) {
    ++g_callbacks;
    // Used to deadlock: callbacks ran with the manager's lock held.
    (void)g_callbackManager->isHandleAlive(info.id, info.generation);
    (void)g_callbackManager->cacheStats();
}
}  // namespace

TEST_CASE("AssetManager - an invalidation callback may call back into the manager", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");
    g_callbackManager = &mgr;
    g_callbacks = 0;
    mgr.registerInvalidationCallback(callbackThatCallsBack);
    {
        auto handle = mgr.loadSync<Texture>(kTexturePath);
        REQUIRE(handle.isReady());
    }
    mgr.collectGarbage();
    REQUIRE(g_callbacks == 1);
    mgr.unregisterInvalidationCallback(callbackThatCallsBack);
}

TEST_CASE("AssetManager - the cache counts every kind of asset", "[assets][review]") {
    TestFixture fix;
    AssetManager mgr(nullptr, "");
    auto texture = mgr.loadSync<Texture>(kTexturePath);
    auto audio = mgr.loadSync<AudioClip>(kAudioPath);
    auto shader = mgr.loadSync<ShaderBlob>(kShaderPath);
    const CacheStats stats = mgr.cacheStats();
    REQUIRE(stats.textureCount == 1);
    REQUIRE(stats.audioCount == 1);
    REQUIRE(stats.shaderCount == 1);
}

// ============================================================================
// Project references
// ============================================================================

namespace {

namespace fs = std::filesystem;

struct ProjectFixture {
    fs::path root;
    fs::path outside;
    fs::path engine;
    ProjectFixture() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path base = fs::temp_directory_path() / ("caffeine-refs-" + std::to_string(stamp));
        root = base / "project";
        outside = base / "elsewhere";
        engine = base / "engine";
        fs::create_directories(root / "assets" / "Textures");
        fs::create_directories(outside);
        fs::create_directories(engine / "builtin");
        write(root / "assets" / "Textures" / "brick.png", "brick");
        write(root / "top.png", "top");
        write(outside / "stone.png", "stone");
        write(outside / "song.wav", "song");
        write(engine / "builtin" / "grass.png", "grass");
    }
    ~ProjectFixture() {
        std::error_code ec;
        fs::remove_all(root.parent_path(), ec);
    }
    static void write(const fs::path& path, const std::string& text) {
        std::ofstream(path, std::ios::binary) << text;
    }
    void configure(AssetManager& mgr) const {
        mgr.setProjectRoot(root.string());
        mgr.setAssetRoot((root / "assets").string());
        mgr.setEngineAssetRoot(engine.string());
    }
};

}  // namespace

TEST_CASE("AssetManager references - a project file is referenced by its project-relative path", "[assets][references]") {
    ProjectFixture fx;
    AssetManager mgr(nullptr, "");
    fx.configure(mgr);

    const auto fromRoot = mgr.resolveReference("top.png");
    REQUIRE(fromRoot.ok());
    REQUIRE(fromRoot.projectPath == "top.png");
    REQUIRE(fromRoot.kind == "image");

    // Relative to the assets folder or to the project: the stored form is always from the project root.
    const auto fromAssets = mgr.resolveReference("Textures/brick.png");
    REQUIRE(fromAssets.ok());
    REQUIRE(fromAssets.projectPath == "assets/Textures/brick.png");
    REQUIRE(fromAssets.assetPath == "Textures/brick.png");  // what the picker lists and components store
    REQUIRE(fromRoot.assetPath == "top.png");               // outside the assets folder: from the project root
    const auto fromProject = mgr.resolveReference("assets/Textures/brick.png");
    REQUIRE(fromProject.projectPath == "assets/Textures/brick.png");
    // Spelling does not matter: slashes, dots, double slashes.
    REQUIRE(mgr.resolveReference("assets\\Textures\\.\\brick.png").projectPath == "assets/Textures/brick.png");
    REQUIRE(mgr.resolveReference("assets//Textures/../Textures/brick.png").projectPath == "assets/Textures/brick.png");
    // An absolute path inside the project becomes relative.
    const auto absolute = mgr.resolveReference((fx.root / "assets" / "Textures" / "brick.png").string());
    REQUIRE(absolute.ok());
    REQUIRE(absolute.projectPath == "assets/Textures/brick.png");
    REQUIRE(absolute.origin == ReferenceOrigin::Project);
}

TEST_CASE("AssetManager references - engine built-ins are valid references", "[assets][references]") {
    ProjectFixture fx;
    AssetManager mgr(nullptr, "");
    fx.configure(mgr);
    const auto builtin = mgr.resolveReference("builtin/grass.png");
    REQUIRE(builtin.ok());
    REQUIRE(builtin.origin == ReferenceOrigin::Engine);
    REQUIRE(builtin.projectPath == "builtin/grass.png");
    // A project file wins over an engine file of the same relative path.
    ProjectFixture::write(fx.root / "builtin_dup.png", "x");
    REQUIRE(mgr.resolveReference("builtin/grass.png").origin == ReferenceOrigin::Engine);
}

TEST_CASE("AssetManager references - what is not in the project is reported, never accepted", "[assets][references]") {
    ProjectFixture fx;
    AssetManager mgr(nullptr, "");
    fx.configure(mgr);

    REQUIRE(mgr.resolveReference("").status == ReferenceStatus::Empty);
    REQUIRE(mgr.resolveReference("nope.png").status == ReferenceStatus::Missing);
    REQUIRE(mgr.resolveReference("../elsewhere/stone.png").status == ReferenceStatus::Missing);  // climbs out
    const auto outside = mgr.resolveReference((fx.outside / "stone.png").string());
    REQUIRE(outside.status == ReferenceStatus::OutsideProject);
    REQUIRE(outside.kind == "image");
    REQUIRE_FALSE(outside.ok());
    REQUIRE(mgr.resolveReference((fx.outside / "missing.png").string()).status == ReferenceStatus::Missing);
    REQUIRE(mgr.resolveReference((fx.root / "assets").string()).status == ReferenceStatus::NotAFile);
    REQUIRE(mgr.resolveReference("assets").status == ReferenceStatus::NotAFile);
}

TEST_CASE("AssetManager references - importing copies a file into the project, once", "[assets][references]") {
    ProjectFixture fx;
    AssetManager mgr(nullptr, "");
    fx.configure(mgr);

    const auto first = mgr.importReference((fx.outside / "stone.png").string());
    REQUIRE(first.ok());
    REQUIRE(first.imported);
    REQUIRE(first.projectPath == "assets/Textures/stone.png");
    REQUIRE(first.assetPath == "Textures/stone.png");
    REQUIRE(fs::exists(fx.root / "assets" / "Textures" / "stone.png"));
    REQUIRE(mgr.resolveReference(first.projectPath).ok());

    // The same file again: the copy that is there is used.
    const auto again = mgr.importReference((fx.outside / "stone.png").string());
    REQUIRE(again.ok());
    REQUIRE_FALSE(again.imported);
    REQUIRE(again.projectPath == first.projectPath);

    // A different file with the same name does not overwrite the first.
    const fs::path other = fx.outside / "other" / "stone.png";
    fs::create_directories(other.parent_path());
    ProjectFixture::write(other, "a different stone");
    const auto renamed = mgr.importReference(other.string());
    REQUIRE(renamed.ok());
    REQUIRE(renamed.imported);
    REQUIRE(renamed.projectPath == "assets/Textures/stone_1.png");

    // Each kind goes to its own folder, and a file that is already in the project is just referenced.
    const auto song = mgr.importReference((fx.outside / "song.wav").string());
    REQUIRE(song.projectPath == "assets/Audio/song.wav");
    const auto inside = mgr.importReference("Textures/brick.png");
    REQUIRE(inside.ok());
    REQUIRE_FALSE(inside.imported);
    // Things that are not there stay unresolved: nothing is invented.
    REQUIRE(mgr.importReference("nope.png").status == ReferenceStatus::Missing);
    REQUIRE(mgr.importReference("").status == ReferenceStatus::Empty);
}

TEST_CASE("AssetManager references - an existing folder is reused whatever its case", "[assets][references]") {
    ProjectFixture fx;
    fs::create_directories(fx.root / "assets" / "audio");  // lower-case twin of Audio
    AssetManager mgr(nullptr, "");
    fx.configure(mgr);
    const auto song = mgr.importReference((fx.outside / "song.wav").string());
    REQUIRE(song.projectPath == "assets/audio/song.wav");
}

TEST_CASE("AssetManager references - without a project nothing can be imported", "[assets][references]") {
    ProjectFixture fx;
    AssetManager mgr(nullptr, "");
    const auto outside = mgr.importReference((fx.outside / "stone.png").string());
    REQUIRE(outside.status == ReferenceStatus::OutsideProject);
    REQUIRE_FALSE(fs::exists(fx.root / "assets" / "Textures" / "stone.png"));
}

TEST_CASE("AssetManager references - kinds and path normalisation", "[assets][references]") {
    REQUIRE(AssetManager::classify("a/b.PNG") == "image");
    REQUIRE(AssetManager::classify("m.glb") == "mesh");
    REQUIRE(AssetManager::classify("s.ogg") == "audio");
    REQUIRE(AssetManager::classify("x.mat") == "material");
    REQUIRE(AssetManager::classify("Rock.generator.material") == "generator");
    REQUIRE(AssetManager::classify("run.lua") == "script");
    REQUIRE(AssetManager::classify("data.bin") == "file");
    REQUIRE(AssetManager::classify("dir", true) == "folder");

    std::string out;
    REQUIRE(AssetManager::normalizeRelative("a\\b//./c/../d.png", out));
    REQUIRE(out == "a/b/d.png");
    REQUIRE_FALSE(AssetManager::normalizeRelative("../x", out));
    REQUIRE_FALSE(AssetManager::normalizeRelative("a/../../x", out));
    REQUIRE(AssetManager::normalizeRelative("", out));
    REQUIRE(out.empty());
}
