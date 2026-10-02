#include "catch.hpp"

#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"

#include <filesystem>
#include <fstream>

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
