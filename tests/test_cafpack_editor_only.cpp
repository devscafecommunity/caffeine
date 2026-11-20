#ifdef CF_HAS_CAF_PACK
#include "catch.hpp"

#include "caf-pack/Packer.hpp"
#include "caf-pack/Registry.hpp"

#include <filesystem>
#include <fstream>

using CafPack::Registry;

TEST_CASE("caf-pack treats generator recipes and graph sidecars as editor-only", "[cafpack]") {
    CHECK(Registry::isEditorOnly("Bark.generator.texture"));
    CHECK(Registry::isEditorOnly("assets/Rock.Generator.Material"));
    CHECK(Registry::isEditorOnly("Oak.generator.nature"));
    CHECK(Registry::isEditorOnly("brick.mgraph"));
    CHECK(Registry::isEditorOnly("flame.texgraph"));
    CHECK(Registry::isEditorOnly("alpine.terrain-graph"));
    CHECK_FALSE(Registry::isEditorOnly("tri.obj"));
    CHECK_FALSE(Registry::isEditorOnly("generator.png"));
    CHECK_FALSE(Registry::isEditorOnly("rock.obj"));
}

TEST_CASE("caf-pack skips recipes instead of failing the whole package", "[cafpack]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "cafpack_editor_only";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream(dir / "Bark.generator.texture") << "{}";
        std::ofstream(dir / "Bark.texgraph") << "graph";
        std::ofstream(dir / "tri.obj") << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    CafPack::Packer::Config config;
    config.inputDir = dir;
    config.outputFile = fs::temp_directory_path() / "cafpack_editor_only.cap";
    CafPack::Packer packer(config);
    const bool ok = packer.pack();
    INFO(packer.getError());
    REQUIRE(ok);
    CHECK(packer.getAssetCount() == 1);
    CHECK(packer.getSkipped().size() == 2);
    fs::remove_all(dir);
}
#endif
