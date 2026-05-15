#include "catch.hpp"
#include "../src/Caffeine.hpp"
#include "../src/core/io/FileWatcher.hpp"
#include "../src/assets/AssetPipeline.hpp"
#include "../src/tools/PipelineTypes.hpp"
#include "../src/tools/TextureEncoder.hpp"
#include "../src/tools/AudioEncoder.hpp"
#include "../src/tools/MeshEncoder.hpp"
#include "../src/tools/AssetManifest.hpp"
#include "../src/tools/AssetPipeline.hpp"

using namespace Caffeine;
using namespace Caffeine::Tools;

TEST_CASE("ConversionResult - default values", "[pipeline]") {
    ConversionResult r;
    REQUIRE(r.success == false);
    REQUIRE(r.inputBytes == 0);
    REQUIRE(r.outputBytes == 0);
    REQUIRE(r.compressionRatio == 0.0f);
}

TEST_CASE("TextureEncodeOptions - defaults", "[pipeline]") {
    TextureEncodeOptions opts;
    REQUIRE(opts.format == TextureEncodeOptions::Format::RGBA8);
    REQUIRE(opts.generateMipmaps == true);
    REQUIRE(opts.maxMipLevels == 0);
    REQUIRE(opts.premultiplyAlpha == false);
    REQUIRE(opts.flipVertically == false);
}

TEST_CASE("AudioEncodeOptions - defaults", "[pipeline]") {
    AudioEncodeOptions opts;
    REQUIRE(opts.format == AudioEncodeOptions::Format::PCM16);
    REQUIRE(opts.targetSampleRate == 44100);
    REQUIRE(opts.mono == false);
    REQUIRE(opts.normalizeGain == 0.0f);
}

TEST_CASE("MeshEncodeOptions - defaults", "[pipeline]") {
    MeshEncodeOptions opts;
    REQUIRE(opts.optimizeVertexCache == true);
    REQUIRE(opts.generateTangents == true);
    REQUIRE(opts.compressVertices == false);
    REQUIRE(opts.lodReductionRatio == 0.0f);
}

TEST_CASE("MipLevel - default values", "[pipeline]") {
    MipLevel mip;
    REQUIRE(mip.width == 0);
    REQUIRE(mip.height == 0);
    REQUIRE(mip.data.empty());
}

TEST_CASE("MeshMetadata - default values and size", "[pipeline]") {
    MeshMetadata meta;
    REQUIRE(meta.vertexCount == 0);
    REQUIRE(meta.indexCount == 0);
    REQUIRE(meta.subMeshCount == 0);
    REQUIRE(meta.reserved == 0);
    REQUIRE(sizeof(MeshMetadata) == 16);
}

TEST_CASE("AssetManifestEntry - defaults", "[pipeline]") {
    AssetManifestEntry entry;
    REQUIRE(entry.type == AssetType::Unknown);
    REQUIRE(entry.sizeBytes == 0);
    REQUIRE(entry.crc32 == 0);
}

TEST_CASE("BatchOptions - defaults", "[pipeline]") {
    AssetPipeline::BatchOptions opts;
    REQUIRE(opts.forceRebuild == false);
    REQUIRE(opts.verbose == false);
    REQUIRE(opts.threadCount == 4);
}

TEST_CASE("BatchReport - defaults", "[pipeline]") {
    AssetPipeline::BatchReport report;
    REQUIRE(report.converted == 0);
    REQUIRE(report.skipped == 0);
    REQUIRE(report.errors == 0);
    REQUIRE(report.errorMessages.empty());
    REQUIRE(report.totalTimeSeconds == 0.0);
    REQUIRE(report.totalInputBytes == 0);
    REQUIRE(report.totalOutputBytes == 0);
}

TEST_CASE("TextureEncoder::generateMipmaps - 2x2 input", "[pipeline]") {
    u8 pixels[16] = {
        255, 0, 0, 255,  0, 255, 0, 255,
        0, 0, 255, 255,  255, 255, 0, 255
    };
    
    auto mips = TextureEncoder::generateMipmaps(pixels, 2, 2, 4, 0);
    
    REQUIRE(mips.size() == 2);
    REQUIRE(mips[0].width == 2);
    REQUIRE(mips[0].height == 2);
    REQUIRE(mips[1].width == 1);
    REQUIRE(mips[1].height == 1);
}

TEST_CASE("TextureEncoder::generateMipmaps - 4x4 input", "[pipeline]") {
    std::vector<u8> pixels(4 * 4 * 4, 128);
    
    auto mips = TextureEncoder::generateMipmaps(pixels.data(), 4, 4, 4, 0);
    
    REQUIRE(mips.size() == 3);
    REQUIRE(mips[0].width == 4);
    REQUIRE(mips[1].width == 2);
    REQUIRE(mips[2].width == 1);
}

TEST_CASE("TextureEncoder::generateMipmaps - level 0 is copy", "[pipeline]") {
    u8 pixels[16] = {
        1, 2, 3, 4,  5, 6, 7, 8,
        9, 10, 11, 12,  13, 14, 15, 16
    };
    
    auto mips = TextureEncoder::generateMipmaps(pixels, 2, 2, 4, 0);
    
    REQUIRE(mips[0].data.size() == 16);
    for (usize i = 0; i < 16; ++i) {
        REQUIRE(mips[0].data[i] == pixels[i]);
    }
}

TEST_CASE("TextureEncoder::encodeRaw - 2x2 RGBA8", "[pipeline]") {
    u8 pixels[16] = {
        255, 0, 0, 255,  0, 255, 0, 255,
        0, 0, 255, 255,  255, 255, 0, 255
    };
    
    auto tempPath = std::filesystem::temp_directory_path() / "test_texture.caf";
    
    TextureEncodeOptions opts;
    opts.generateMipmaps = false;
    
    auto result = TextureEncoder::encodeRaw(tempPath.string(), pixels, 2, 2, 4, opts);
    
    REQUIRE(result.success == true);
    REQUIRE(result.outputBytes > 0);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("TextureEncoder::encodeRaw - null pixels", "[pipeline]") {
    auto tempPath = std::filesystem::temp_directory_path() / "test_texture_null.caf";
    
    auto result = TextureEncoder::encodeRaw(tempPath.string(), nullptr, 2, 2, 4);
    
    REQUIRE(result.success == false);
    REQUIRE(!result.errorMessage.empty());
}

TEST_CASE("AudioEncoder::encodeRaw - simple PCM", "[pipeline]") {
    i16 samples[100];
    for (int i = 0; i < 100; ++i) {
        samples[i] = static_cast<i16>(i * 100);
    }
    
    auto tempPath = std::filesystem::temp_directory_path() / "test_audio.caf";
    
    auto result = AudioEncoder::encodeRaw(tempPath.string(), samples, 100, 44100, 2);
    
    REQUIRE(result.success == true);
    REQUIRE(result.outputBytes > 0);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("AudioEncoder::encodeRaw - zero samples", "[pipeline]") {
    i16 samples[10];
    auto tempPath = std::filesystem::temp_directory_path() / "test_audio_zero.caf";
    
    auto result = AudioEncoder::encodeRaw(tempPath.string(), samples, 0, 44100, 2);
    
    REQUIRE(result.success == false);
    REQUIRE(!result.errorMessage.empty());
}

TEST_CASE("MeshEncoder::encodeRaw - valid mesh", "[pipeline]") {
    Assets::Mesh3D mesh;
    
    Assets::Vertex3D v1;
    v1.position = Vec3(0.0f, 0.0f, 0.0f);
    v1.normal = Vec3(0.0f, 1.0f, 0.0f);
    v1.texcoord = Vec2(0.0f, 0.0f);
    
    Assets::Vertex3D v2;
    v2.position = Vec3(1.0f, 0.0f, 0.0f);
    v2.normal = Vec3(0.0f, 1.0f, 0.0f);
    v2.texcoord = Vec2(1.0f, 0.0f);
    
    Assets::Vertex3D v3;
    v3.position = Vec3(0.0f, 1.0f, 0.0f);
    v3.normal = Vec3(0.0f, 1.0f, 0.0f);
    v3.texcoord = Vec2(0.0f, 1.0f);
    
    mesh.vertices = {v1, v2, v3};
    mesh.indices = {0, 1, 2};
    
    Assets::SubMesh submesh;
    submesh.indexOffset = 0;
    submesh.indexCount = 3;
    mesh.subMeshes.push_back(submesh);
    
    auto tempPath = std::filesystem::temp_directory_path() / "test_mesh.caf";
    
    auto result = MeshEncoder::encodeRaw(tempPath.string(), mesh);
    
    REQUIRE(result.success == true);
    REQUIRE(result.outputBytes > 0);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("MeshEncoder::encodeRaw - empty mesh", "[pipeline]") {
    Assets::Mesh3D mesh;
    
    auto tempPath = std::filesystem::temp_directory_path() / "test_mesh_empty.caf";
    
    auto result = MeshEncoder::encodeRaw(tempPath.string(), mesh);
    
    REQUIRE(result.success == false);
    REQUIRE(!result.errorMessage.empty());
}

TEST_CASE("AssetManifest::addEntry - increases count", "[pipeline]") {
    AssetManifest manifest;
    
    AssetManifestEntry entry;
    entry.id = "test";
    entry.path = "test.caf";
    entry.type = AssetType::Texture;
    
    manifest.addEntry(entry);
    
    REQUIRE(manifest.entryCount() == 1);
}

TEST_CASE("AssetManifest::find - returns correct entry", "[pipeline]") {
    AssetManifest manifest;
    
    AssetManifestEntry entry;
    entry.id = "test";
    entry.path = "test.caf";
    entry.type = AssetType::Texture;
    
    manifest.addEntry(entry);
    
    const auto* found = manifest.find("test");
    REQUIRE(found != nullptr);
    REQUIRE(found->id == "test");
    REQUIRE(found->path == "test.caf");
}

TEST_CASE("AssetManifest::removeEntry - decreases count", "[pipeline]") {
    AssetManifest manifest;
    
    AssetManifestEntry entry;
    entry.id = "test";
    entry.path = "test.caf";
    
    manifest.addEntry(entry);
    REQUIRE(manifest.entryCount() == 1);
    
    manifest.removeEntry("test");
    REQUIRE(manifest.entryCount() == 0);
}

TEST_CASE("AssetManifest::find - missing id returns nullptr", "[pipeline]") {
    AssetManifest manifest;
    
    const auto* found = manifest.find("nonexistent");
    REQUIRE(found == nullptr);
}

TEST_CASE("AssetManifest - save and load roundtrip", "[pipeline]") {
    AssetManifest manifest;
    
    AssetManifestEntry e1;
    e1.id = "texture1";
    e1.path = "assets/texture1.caf";
    e1.type = AssetType::Texture;
    e1.sizeBytes = 12345;
    e1.crc32 = 0xCAFEBABE;
    
    AssetManifestEntry e2;
    e2.id = "audio1";
    e2.path = "assets/audio1.caf";
    e2.type = AssetType::Audio;
    e2.sizeBytes = 54321;
    e2.crc32 = 0xDEADBEEF;
    
    manifest.addEntry(e1);
    manifest.addEntry(e2);
    
    auto tempPath = std::filesystem::temp_directory_path() / "test_manifest.json";
    
    REQUIRE(manifest.save(tempPath.string()));
    
    AssetManifest loaded;
    REQUIRE(loaded.load(tempPath.string()));
    
    REQUIRE(loaded.entryCount() == 2);
    
    const auto* found1 = loaded.find("texture1");
    REQUIRE(found1 != nullptr);
    REQUIRE(found1->path == "assets/texture1.caf");
    REQUIRE(found1->type == AssetType::Texture);
    
    const auto* found2 = loaded.find("audio1");
    REQUIRE(found2 != nullptr);
    REQUIRE(found2->path == "assets/audio1.caf");
    REQUIRE(found2->type == AssetType::Audio);
    
    std::filesystem::remove(tempPath);
}

TEST_CASE("AssetPipeline::detectAssetType - .png", "[pipeline]") {
    AssetPipeline pipeline;
    auto type = pipeline.detectAssetType("texture.png");
    REQUIRE(type == AssetType::Texture);
}

TEST_CASE("AssetPipeline::detectAssetType - .wav", "[pipeline]") {
    AssetPipeline pipeline;
    auto type = pipeline.detectAssetType("sound.wav");
    REQUIRE(type == AssetType::Audio);
}

TEST_CASE("AssetPipeline::detectAssetType - .obj", "[pipeline]") {
    AssetPipeline pipeline;
    auto type = pipeline.detectAssetType("model.obj");
    REQUIRE(type == AssetType::Mesh);
}

TEST_CASE("AssetPipeline::detectAssetType - unknown", "[pipeline]") {
    AssetPipeline pipeline;
    auto type = pipeline.detectAssetType("file.xyz");
    REQUIRE(type == AssetType::Unknown);
}

TEST_CASE("AssetPipeline::isOutdated - nonexistent dst", "[pipeline]") {
    AssetPipeline pipeline;
    
    auto src = std::filesystem::temp_directory_path() / "src_file.txt";
    auto dst = std::filesystem::temp_directory_path() / "dst_file_nonexistent.caf";
    
    {
        std::FILE* f = std::fopen(src.string().c_str(), "w");
        std::fprintf(f, "test");
        std::fclose(f);
    }
    
    REQUIRE(pipeline.isOutdated(src, dst) == true);
    
    std::filesystem::remove(src);
}

// ============================================================================
// FileWatcher tests
// ============================================================================

TEST_CASE("FileWatcher - Create and destroy", "[pipeline]") {
    IO::FileWatcher fw;
    REQUIRE_FALSE(fw.isRunning());
}

// ============================================================================
// AssetPipeline tests
// ============================================================================

using namespace Caffeine::Assets;

namespace {

class NullCompiler : public IAssetCompiler {
public:
    bool Compile(AssetImportContext& ctx) override {
        ctx.Logs.push_back("NullCompiler: " + ctx.SourcePath.string());
        ctx.Success = true;
        return true;
    }
    std::vector<std::string> GetSupportedExtensions() override {
        return { ".null" };
    }
};

} // anonymous namespace

TEST_CASE("AssetPipeline - Register and find compiler", "[pipeline]") {
    AssetPipeline pipeline;
    pipeline.RegisterCompiler(std::make_unique<NullCompiler>());
    REQUIRE_FALSE(pipeline.IsProcessing());
    REQUIRE(pipeline.GetProgress() == 0.0f);
}

TEST_CASE("AssetPipeline - Initialize creates directories", "[pipeline]") {
    auto tmp = std::filesystem::temp_directory_path() / "caffeine_pipeline_test";
    std::filesystem::remove_all(tmp);

    AssetPipeline pipeline;
    pipeline.Initialize(tmp);

    REQUIRE(std::filesystem::exists(tmp / "assets" / "raw"));
    REQUIRE(std::filesystem::exists(tmp / "assets" / "processed"));

    std::filesystem::remove_all(tmp);
}

TEST_CASE("AssetPipeline - Manifest roundtrip", "[pipeline]") {
    auto tmp = std::filesystem::temp_directory_path() / "caffeine_manifest_test";
    std::filesystem::remove_all(tmp);

    {
        AssetPipeline pipeline;
        pipeline.RegisterCompiler(std::make_unique<NullCompiler>());
        pipeline.Initialize(tmp);

        // Create a dummy source file
        auto srcDir = tmp / "assets" / "raw";
        std::filesystem::create_directories(srcDir);
        auto dummySrc = srcDir / "test.null";
        {
            std::ofstream f(dummySrc.string());
            f << "dummy";
        }

        pipeline.Reimport(dummySrc);

        auto manifest = pipeline.GetManifest();
        REQUIRE(manifest.size() == 1);
        REQUIRE(manifest[0].sourcePath.find("test.null") != std::string::npos);

        // Verify manifest file was saved
        REQUIRE(std::filesystem::exists(tmp / "assets" / "manifest.caf"));
    }

    // Create new pipeline and verify manifest loads
    {
        AssetPipeline pipeline2;
        pipeline2.RegisterCompiler(std::make_unique<NullCompiler>());
        pipeline2.Initialize(tmp);

        auto manifest2 = pipeline2.GetManifest();
        REQUIRE(manifest2.size() == 1);
        REQUIRE(manifest2[0].sourcePath.find("test.null") != std::string::npos);
    }

    std::filesystem::remove_all(tmp);
}
