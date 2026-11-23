#include "catch.hpp"

#include "assets/MeshSubdivide.hpp"
#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/TextureMapGenerator.hpp"
#include "render/ReflectionBudget.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace Caffeine;
using namespace Caffeine::Assets;

TEST_CASE("CAFMAT2 roundtrip keeps maps and emission", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "painted.mat";

    MaterialSurface written;
    written.name = "Painted Metal";
    written.albedo = Vec4(0.2f, 0.4f, 0.8f, 1.0f);
    written.metallic = 0.85f;
    written.roughness = 0.3f;
    written.reflectance = 0.5f;
    written.emission = Vec3(1.0f, 0.2f, 0.0f);
    written.emissionStrength = 2.5f;
    written.albedoMap = "textures/paint.png";
    written.normalMap = "textures/paint_n.png";
    written.normalStrength = 1.25f;
    written.normalDirectX = true;
    written.ormMap = "textures/paint_orm.png";
    REQUIRE(saveMaterialFile(path, written));

    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.valid);
    REQUIRE(loaded.name == "Painted Metal");
    REQUIRE(loaded.albedo.x == Approx(0.2f));
    REQUIRE(loaded.metallic == Approx(0.85f));
    REQUIRE(loaded.roughness == Approx(0.3f));
    REQUIRE(loaded.reflectance == Approx(0.5f));
    REQUIRE(loaded.emission.y == Approx(0.2f));
    REQUIRE(loaded.emissionStrength == Approx(2.5f));
    REQUIRE(loaded.ormMap == "textures/paint_orm.png");
    REQUIRE(loaded.normalDirectX);
    REQUIRE(loaded.normalStrength == Approx(1.25f));

    const u64 before = MaterialCache::instance().revision();
    const MaterialSurface resolved = MaterialCache::instance().resolve(path.string());
    REQUIRE(resolved.valid);
    REQUIRE(resolved.albedoMap == "textures/paint.png");
    written.roughness = 0.9f;
    MaterialCache::instance().publish(path.string(), "", written);
    REQUIRE(MaterialCache::instance().revision() != before);
    REQUIRE(MaterialCache::instance().resolve(path.string()).roughness == Approx(0.9f));
    MaterialCache::instance().invalidate(path.string());
    std::filesystem::remove_all(dir);
}

TEST_CASE("CAFMAT1 header still resolves albedo", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-legacy";
    std::filesystem::create_directories(dir);
    const auto path = dir / "legacy.mat";
    {
        std::ofstream out(path);
        out << "CAFMAT1\n"
            << "name Legacy\n"
            << "albedo 0.1 0.2 0.3 1\n"
            << "roughness 0.4\n"
            << "metallic 0.6\n"
            << "node_count 1\n"
            << "node 1 OutputPBR\n";
    }
    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.name == "Legacy");
    REQUIRE(loaded.albedo.z == Approx(0.3f));
    REQUIRE(loaded.metallic == Approx(0.6f));
    REQUIRE(loaded.roughness == Approx(0.4f));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Published material without a file still resolves", "[material]") {
    const std::string root = (std::filesystem::temp_directory_path() / "caffeine-material-virtual").string();
    const std::string key = "__caffeine_material_preview__.mat";
    REQUIRE_FALSE(MaterialCache::instance().resolve(key, root).valid);

    MaterialSurface surface;
    surface.metallic = 1.0f;
    surface.reflectance = 0.9f;
    MaterialCache::instance().publish(key, root, surface);
    const MaterialSurface resolved = MaterialCache::instance().resolve(key, root);
    REQUIRE(resolved.valid);
    REQUIRE(resolved.metallic == Approx(1.0f));
    REQUIRE(resolved.reflectance == Approx(0.9f));
    MaterialCache::instance().invalidate();
}

TEST_CASE("texture map generator derives normals, AO and ORM", "[material]") {
    TextureImage height;
    height.width = 3;
    height.height = 3;
    height.rgba.resize(3 * 3 * 4);
    height.valid = true;
    for (u32 y = 0; y < 3; ++y) {
        for (u32 x = 0; x < 3; ++x) {
            const u8 value = static_cast<u8>(x * 80);
            const size_t index = (y * 3 + x) * 4;
            height.rgba[index + 0] = value;
            height.rgba[index + 1] = value;
            height.rgba[index + 2] = value;
            height.rgba[index + 3] = 255;
        }
    }

    const TextureImage normal = generateNormalFromHeight(height, 1.0f, false, false);
    REQUIRE(normal.valid);
    const u8* center = normal.pixel(1, 1);
    REQUIRE(center != nullptr);
    // The height rises to the right, so the normal leans left (-X).
    CHECK(center[0] < 128);
    CHECK(center[1] == Approx(128).margin(2));
    CHECK(center[2] > 128);
    TextureImage vertical = height;
    for (u32 y = 0; y < 3; ++y) {
        for (u32 x = 0; x < 3; ++x) {
            const u8 value = static_cast<u8>(y * 80);
            const size_t index = (y * 3 + x) * 4;
            vertical.rgba[index + 0] = value;
            vertical.rgba[index + 1] = value;
            vertical.rgba[index + 2] = value;
        }
    }
    const TextureImage directX = generateNormalFromHeight(vertical, 1.0f, true, false);
    CHECK(directX.pixel(1, 1)[1] > 128);

    // A valley: the centre is darker than its neighbours.
    TextureImage valley = height;
    for (u32 y = 0; y < 3; ++y) {
        for (u32 x = 0; x < 3; ++x) {
            const u8 value = (x == 1 && y == 1) ? 0 : 255;
            const size_t index = (y * 3 + x) * 4;
            valley.rgba[index + 0] = value;
            valley.rgba[index + 1] = value;
            valley.rgba[index + 2] = value;
        }
    }
    const TextureImage ao = generateAmbientOcclusion(valley, 1.0f, 1, false);
    REQUIRE(ao.valid);
    CHECK(ao.pixel(1, 1)[0] < ao.pixel(0, 0)[0]);

    TextureImage albedo;
    albedo.width = 1;
    albedo.height = 1;
    albedo.rgba = {128, 128, 128, 255};
    albedo.valid = true;
    const TextureImage roughness =
        generateScalarFromAlbedo(albedo, TextureMapKind::Roughness, 0.0f, 1.0f, false);
    REQUIRE(roughness.valid);
    CHECK(roughness.pixel(0, 0)[0] == Approx(128).margin(2));

    const TextureImage packed =
        packMaterialMap(&ao, &roughness, nullptr, TexturePackKind::Orm);
    REQUIRE(packed.valid);
    CHECK(packed.pixel(1, 1)[0] == ao.pixel(1, 1)[0]);
    CHECK(packed.pixel(1, 1)[1] == 128);
    CHECK(packed.pixel(1, 1)[2] == 0);

    const TextureImage hdrp =
        packMaterialMap(&ao, &roughness, nullptr, TexturePackKind::UnityHdrpMask);
    REQUIRE(hdrp.valid);
    CHECK(hdrp.pixel(1, 1)[0] == 0);
    CHECK(hdrp.pixel(1, 1)[1] == ao.pixel(1, 1)[0]);
    CHECK(hdrp.pixel(1, 1)[2] == 0);
    CHECK(hdrp.pixel(1, 1)[3] == Approx(127).margin(1));
}

TEST_CASE("texture map generator round-trips a PNG", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-mapgen";
    std::filesystem::remove_all(dir);
    TextureImage image;
    image.width = 2;
    image.height = 2;
    image.rgba = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    image.valid = true;
    const auto path = dir / "swatch.png";
    REQUIRE(saveTextureImage(path, image));
    TextureImage loaded;
    REQUIRE(loadTextureImage(path, loaded));
    REQUIRE(loaded.width == 2);
    REQUIRE(loaded.height == 2);
    CHECK(loaded.rgba == image.rgba);

    const auto path16 = dir / "swatch16.png";
    const uint16_t rgba16[16] = {
        65535, 0,     0,     65535, 0,     65535, 0,     65535,
        0,     0,     65535, 65535, 65535, 65535, 65535, 65535,
    };
    REQUIRE(saveTextureImage16(path16, 2, 2, rgba16));
    TextureImage loaded16;
    REQUIRE(loadTextureImage(path16, loaded16));
    REQUIRE(loaded16.width == 2);
    REQUIRE(loaded16.height == 2);
    CHECK(loaded16.pixel(0, 0)[0] > 240);
    CHECK(loaded16.pixel(1, 0)[1] > 240);
    CHECK_FALSE(saveTextureImage16(path16, 0, 2, rgba16));
    std::filesystem::remove_all(dir);
}

TEST_CASE("saveMaterialFile creates missing parent directories", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-nested" / "materials";
    std::filesystem::remove_all(dir.parent_path());
    const auto path = dir / "new.mat";
    MaterialSurface surface;
    surface.name = "New";
    surface.valid = true;
    REQUIRE(saveMaterialFile(path, surface));
    REQUIRE(std::filesystem::exists(path));
    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.name == "New");
}

TEST_CASE("reflection modules roundtrip and stay off by default", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "reflect.mat";

    MaterialSurface written;
    written.name = "Mirror";
    written.reflection = 0.8f;
    written.reflectionPerformance = 1;
    written.reflectionQuality = 1;
    written.reflectionPlanar = 1;
    written.ssrResolution = 0.5f;
    written.ssrMaxSteps = 32.0f;
    written.ssrTemporalFrames = 4.0f;
    written.ssrDistance = 15.0f;
    written.ssrSamples = 64.0f;
    written.ssrDenoise = 0.7f;
    written.ssrProbeBlend = 8.0f;
    written.ssrBounces = 1.0f;
    REQUIRE(saveMaterialFile(path, written));

    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.reflectionPerformance == 1);
    REQUIRE(loaded.reflectionQuality == 1);
    REQUIRE(loaded.reflectionPlanar == 1);
    REQUIRE(loaded.ssrResolution == Approx(0.5f));
    REQUIRE(loaded.ssrMaxSteps == Approx(32.0f));
    REQUIRE(loaded.ssrSamples == Approx(64.0f));
    REQUIRE(loaded.ssrDenoise == Approx(0.7f));
    REQUIRE(loaded.ssrBounces == Approx(1.0f));

    MaterialSurface plain;
    REQUIRE(plain.reflectionPerformance == 0);
    REQUIRE(plain.reflectionQuality == 0);
}

TEST_CASE("legacy reflection budget maps onto optional modules", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "legacy-budget.mat";
    {
        std::ofstream out(path);
        out << "CAFMAT3\n"
            << "name Legacy\n"
            << "reflection 0.4\n"
            << "reflection_budget 1\n";
    }
    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.reflectionPerformance == 1);
    REQUIRE(loaded.reflectionQuality == 0);
}

TEST_CASE("reflection march steps follow the optional modules", "[material]") {
    using Caffeine::Render::ReflectionModules;
    using Caffeine::Render::reflectionMarchSteps;

    ReflectionModules off;
    REQUIRE(reflectionMarchSteps(24.0f, 0.2f, off) == 24);

    ReflectionModules performance;
    performance.performance = true;
    performance.resolution = 0.5f;
    performance.maxSteps = 32.0f;
    const i32 smooth = reflectionMarchSteps(48.0f, 0.0f, performance);
    const i32 rough = reflectionMarchSteps(48.0f, 1.0f, performance);
    REQUIRE(smooth < 32);
    REQUIRE(rough < smooth);

    ReflectionModules quality;
    quality.quality = true;
    quality.samples = 64.0f;
    REQUIRE(reflectionMarchSteps(16.0f, 0.4f, quality) == 64);

    ReflectionModules both = performance;
    both.quality = true;
    both.samples = 64.0f;
    REQUIRE(reflectionMarchSteps(48.0f, 1.0f, both) == 64);
}

TEST_CASE("parallax depth roundtrips through the .mat file and stays clamped", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-parallax";
    std::filesystem::create_directories(dir);
    const auto path = dir / "bricks.mat";

    MaterialSurface written;
    written.name = "Bricks";
    written.normalMap = "textures/bricks_n.png"; // height lives in this map's alpha
    written.heightScale = 0.06f;
    written.parallaxSteps = 40.0f;
    REQUIRE(saveMaterialFile(path, written));

    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.heightScale == Approx(0.06f));
    REQUIRE(loaded.parallaxSteps == Approx(40.0f));

    // Materials saved before depth existed keep it off.
    MaterialSurface legacy;
    legacy.name = "Legacy";
    REQUIRE(legacy.heightScale == 0.0f);

    MaterialSurface wild;
    wild.heightScale = 9.0f;
    wild.parallaxSteps = 100000.0f;
    sanitizeMaterialSurface(wild);
    REQUIRE(wild.heightScale <= 0.5f);
    REQUIRE(wild.parallaxSteps <= 128.0f);
    wild.heightScale = -1.0f;
    wild.parallaxSteps = 0.0f;
    sanitizeMaterialSurface(wild);
    REQUIRE(wild.heightScale == 0.0f);
    REQUIRE(wild.parallaxSteps >= 4.0f);

    // The cache must notice a depth-only edit.
    MaterialCache::instance().publish(path.string(), "", written);
    const u64 before = MaterialCache::instance().revision();
    written.heightScale = 0.12f;
    MaterialCache::instance().publish(path.string(), "", written);
    REQUIRE(MaterialCache::instance().revision() != before);
    MaterialCache::instance().invalidate(path.string());
    std::filesystem::remove_all(dir);
}

TEST_CASE("midpoint subdivision quadruples triangles and keeps the surface welded", "[material]") {
    using namespace Caffeine;
    Assets::Mesh3D quad;
    auto vert = [](f32 x, f32 z) {
        Assets::Vertex3D v;
        v.position = Vec3(x, 0, z);
        v.normal = Vec3(0, 1, 0);
        v.texcoord = Vec2(x, z);
        v.tangent = Vec4(1, 0, 0, 1);
        return v;
    };
    quad.vertices = {vert(0, 0), vert(1, 0), vert(1, 1), vert(0, 1)};
    quad.indices = {0, 1, 2, 0, 2, 3};

    Assets::Mesh3D out;
    REQUIRE(Assets::subdivideMesh(quad, 3, out));
    CHECK(out.indices.size() == 6 * 64);
    CHECK(out.vertices.size() == 81); // (2^3+1)^2: shared edge vertices are welded
    const Assets::MeshDensity density = Assets::measureMeshDensity(out);
    CHECK(density.metersPerUv == Approx(1.0f));
    CHECK(density.averageEdgeUv < 0.2f);
    CHECK_FALSE(Assets::subdivideMesh(quad, 3, out, 100)); // over the index budget
}

TEST_CASE("uv_object_scale defaults on, round-trips and older materials keep the default", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine_uv_object_scale";
    std::filesystem::create_directories(dir);
    const auto path = dir / "scaled.mat";

    MaterialSurface fresh;
    REQUIRE(fresh.uvObjectScale == 1);

    MaterialSurface off;
    off.uvObjectScale = 0;
    REQUIRE(saveMaterialFile(path, off));
    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.uvObjectScale == 0);

    // A material written before the key existed behaves like a new one: UVs follow the scale.
    {
        std::ofstream legacy(dir / "legacy.mat");
        legacy << "CAFMAT3\nname Legacy\nroughness 0.5\n";
    }
    MaterialSurface old;
    REQUIRE(loadMaterialFile(dir / "legacy.mat", old));
    REQUIRE(old.uvObjectScale == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("foliage wind and translucency round-trip, stay clamped and default to still", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-foliage";
    std::filesystem::create_directories(dir);
    const auto path = dir / "leaf.mat";

    MaterialSurface fresh;
    REQUIRE(fresh.windStrength == Approx(0.0f));
    REQUIRE(fresh.windFlutter == Approx(0.0f));
    REQUIRE(fresh.translucency == Approx(0.0f));

    MaterialSurface written;
    written.name = "Leaf";
    written.windStrength = 0.012f;
    written.windSpeed = 1.5f;
    written.windFlutter = 0.6f;
    written.translucency = 0.55f;
    REQUIRE(saveMaterialFile(path, written));

    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.windStrength == Approx(0.012f));
    REQUIRE(loaded.windSpeed == Approx(1.5f));
    REQUIRE(loaded.windFlutter == Approx(0.6f));
    REQUIRE(loaded.translucency == Approx(0.55f));

    // Absurd values are clamped on load.
    {
        std::ofstream file(path, std::ios::app);
        file << "wind_strength 9\nwind_flutter 4\ntranslucency -2\n";
    }
    MaterialSurface clamped;
    REQUIRE(loadMaterialFile(path, clamped));
    REQUIRE(clamped.windStrength <= 0.5f);
    REQUIRE(clamped.windFlutter <= 1.0f);
    REQUIRE(clamped.translucency >= 0.0f);

    std::filesystem::remove_all(dir);
}

TEST_CASE("run-time modifiers round-trip, stay clamped and default to none", "[material]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-material-modifiers";
    std::filesystem::create_directories(dir);
    const auto path = dir / "rock.mat";

    MaterialSurface fresh;
    REQUIRE(fresh.modTint.x == Approx(1.0f));
    REQUIRE(fresh.modWetness == Approx(0.0f));
    REQUIRE(fresh.modSnow == Approx(0.0f));
    REQUIRE(fresh.modAge == Approx(0.0f));

    MaterialSurface written;
    written.name = "Rock";
    written.modTint = Vec3(0.8f, 0.9f, 1.0f);
    written.modWetness = 0.4f;
    written.modSnow = 0.6f;
    written.modAge = 0.2f;
    REQUIRE(saveMaterialFile(path, written));
    MaterialSurface loaded;
    REQUIRE(loadMaterialFile(path, loaded));
    REQUIRE(loaded.modTint.x == Approx(0.8f));
    REQUIRE(loaded.modTint.z == Approx(1.0f));
    REQUIRE(loaded.modWetness == Approx(0.4f));
    REQUIRE(loaded.modSnow == Approx(0.6f));
    REQUIRE(loaded.modAge == Approx(0.2f));

    {
        std::ofstream file(path, std::ios::app);
        file << "mod_wetness 7\nmod_snow -3\nmod_tint 2 2 2\n";
    }
    MaterialSurface clamped;
    REQUIRE(loadMaterialFile(path, clamped));
    REQUIRE(clamped.modWetness <= 1.0f);
    REQUIRE(clamped.modSnow >= 0.0f);
    REQUIRE(clamped.modTint.x <= 1.0f);

    // A material written before modifiers existed loads as "no modifiers".
    {
        std::ofstream legacy(dir / "old.mat");
        legacy << "CAFMAT3\nname Old\nroughness 0.5\n";
    }
    MaterialSurface old;
    REQUIRE(loadMaterialFile(dir / "old.mat", old));
    REQUIRE(old.modWetness == Approx(0.0f));
    REQUIRE(old.modTint.y == Approx(1.0f));
    std::filesystem::remove_all(dir);
}

// A draw asks for its material every frame. The cache answers from memory and looks at the disk again only every
// half second; a missing material is remembered; a published material (the editor preview) takes effect at once.
TEST_CASE("MaterialCache: lookups come from memory, with a recheck for files that change", "[material][cache]") {
    using namespace Caffeine;
    using Assets::MaterialCache;
    using Assets::MaterialSurface;
    const auto dir = std::filesystem::temp_directory_path() / "caffeine_material_cache_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "cache.mat";
    std::filesystem::remove(path);
    auto& cache = MaterialCache::instance();
    cache.invalidate();

    // Missing: nothing, and the miss is remembered (the file appears but the answer holds for the window).
    CHECK_FALSE(cache.resolve(path.string()).valid);
    MaterialSurface made;
    made.name = "grass";
    made.roughness = 0.33f;
    REQUIRE(Assets::saveMaterialFile(path, made));
    CHECK_FALSE(cache.resolve(path.string()).valid);
    std::this_thread::sleep_for(std::chrono::milliseconds(650));
    const MaterialSurface found = cache.resolve(path.string());
    REQUIRE(found.valid);
    CHECK(found.roughness == Approx(0.33f));

    // Asked again: from memory, even with the file gone.
    const u64 revision = cache.revision();
    std::filesystem::remove(path);
    CHECK(cache.resolve(path.string()).roughness == Approx(0.33f));
    CHECK(cache.revision() == revision);

    // Rewritten: noticed after the window (a later timestamp whatever the file system's resolution).
    made.roughness = 0.9f;
    REQUIRE(Assets::saveMaterialFile(path, made));
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    CHECK(cache.resolve(path.string()).roughness == Approx(0.33f));
    std::this_thread::sleep_for(std::chrono::milliseconds(650));
    CHECK(cache.resolve(path.string()).roughness == Approx(0.9f));
    CHECK(cache.revision() > revision);

    // invalidate() forgets everything at once.
    made.roughness = 0.1f;
    REQUIRE(Assets::saveMaterialFile(path, made));
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10));
    cache.invalidate(path.string());
    CHECK(cache.resolve(path.string()).roughness == Approx(0.1f));

    // A published material (no file) is served at once, and a later publish replaces it at once.
    const auto virtualPath = (dir / "preview.mat").string();
    std::filesystem::remove(virtualPath);
    CHECK_FALSE(cache.resolve(virtualPath).valid);
    MaterialSurface preview;
    preview.roughness = 0.5f;
    cache.publish(virtualPath, "", preview);
    CHECK(cache.resolve(virtualPath).roughness == Approx(0.5f));
    preview.roughness = 0.7f;
    cache.publish(virtualPath, "", preview);
    CHECK(cache.resolve(virtualPath).roughness == Approx(0.7f));

    cache.invalidate();
    std::filesystem::remove(path);
}
