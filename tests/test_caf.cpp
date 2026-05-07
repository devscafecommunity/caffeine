#include "catch.hpp"
#include "../src/core/io/CafTypes.hpp"
#include "../src/core/io/Crc32.hpp"
#include "../src/core/io/CafWriter.hpp"
#include "../src/core/io/BlobLoader.hpp"
#include "../src/memory/LinearAllocator.hpp"

#include <cstring>

using namespace Caffeine;
using namespace Caffeine::IO;

TEST_CASE("CafHeader - size is exactly 32 bytes", "[caf][types]") {
    REQUIRE(sizeof(CafHeader) == 32);
}

TEST_CASE("CafHeader - magic constant is 0xCAFECAFE", "[caf][types]") {
    REQUIRE(CafHeader::kMagic == 0xCAFECAFEu);
}

TEST_CASE("CafHeader - totalFileSize computes correctly", "[caf][types]") {
    CafHeader h{};
    h.metadataSize = 24;
    h.dataSize     = 512;
    REQUIRE(h.totalFileSize() == 32 + 24 + 512 + 4);
}

TEST_CASE("CafHeader - payloadAlignment returns 16 for Texture", "[caf][types]") {
    CafHeader h{};
    h.type = AssetType::Texture;
    REQUIRE(h.payloadAlignment() == 16u);
}

TEST_CASE("CafHeader - payloadAlignment returns 32 for Mesh", "[caf][types]") {
    CafHeader h{};
    h.type = AssetType::Mesh;
    REQUIRE(h.payloadAlignment() == 32u);
}

TEST_CASE("CafHeader - payloadAlignment returns 32 for Shader", "[caf][types]") {
    CafHeader h{};
    h.type = AssetType::Shader;
    REQUIRE(h.payloadAlignment() == 32u);
}

TEST_CASE("TextureMetadata - size is 24 bytes", "[caf][types]") {
    REQUIRE(sizeof(TextureMetadata) == 24);
}

TEST_CASE("AudioMetadata - size is 16 bytes", "[caf][types]") {
    REQUIRE(sizeof(AudioMetadata) == 16);
}

TEST_CASE("SceneMetadata - size is 20 bytes", "[caf][types]") {
    REQUIRE(sizeof(SceneMetadata) == 20);
}

TEST_CASE("ArchetypeDesc - size is 12 bytes", "[caf][types]") {
    REQUIRE(sizeof(ArchetypeDesc) == 12);
}

TEST_CASE("ShaderMetadata - size is 8 bytes", "[caf][types]") {
    REQUIRE(sizeof(ShaderMetadata) == 8);
}

TEST_CASE("crc32 - empty buffer produces 0x00000000", "[caf][crc32]") {
    REQUIRE(crc32(nullptr, 0) == 0x00000000u);
}

TEST_CASE("crc32 - known vector '123456789' == 0xCBF43926", "[caf][crc32]") {
    const char* data = "123456789";
    REQUIRE(crc32(data, 9) == 0xCBF43926u);
}

TEST_CASE("crc32 - same data produces same result", "[caf][crc32]") {
    const char data[] = "Caffeine Engine";
    REQUIRE(crc32(data, sizeof(data)) == crc32(data, sizeof(data)));
}

TEST_CASE("crc32 - different data produces different result", "[caf][crc32]") {
    const char a[] = "hello";
    const char b[] = "world";
    REQUIRE(crc32(a, sizeof(a)) != crc32(b, sizeof(b)));
}

TEST_CASE("crc32Continue - streaming equals single-shot", "[caf][crc32]") {
    const char full[]  = "HelloWorld";
    const char part1[] = "Hello";
    const char part2[] = "World";

    const u32 single   = crc32(full, 10);
    const u32 first    = crc32(part1, 5);
    const u32 combined = crc32Continue(first, part2, 5);

    REQUIRE(single == combined);
}

TEST_CASE("CafWriter - writes valid texture .caf file", "[caf][writer]") {
    TextureMetadata meta{};
    meta.width     = 64;
    meta.height    = 64;
    meta.format    = 0;
    meta.mipLevels = 1;
    meta.reserved  = 0;

    const u32 pixelCount = 64 * 64 * 4;
    u8 pixels[64 * 64 * 4];
    std::memset(pixels, 0xFF, pixelCount);

    auto result = CafWriter::write(
        "test_texture.caf",
        AssetType::Texture,
        CAF_FLAG_NONE,
        &meta, sizeof(meta),
        pixels, pixelCount);

    REQUIRE(result.success);
    REQUIRE(result.bytesWritten == 32 + sizeof(meta) + pixelCount + 4);

    std::remove("test_texture.caf");
}

TEST_CASE("CafWriter - writes valid audio .caf file", "[caf][writer]") {
    AudioMetadata meta{};
    meta.sampleRate    = 44100;
    meta.channels      = 2;
    meta.bitsPerSample = 16;
    meta.sampleCount   = 1000;
    meta.reserved      = 0;

    const u32 audioBytes = meta.sampleCount * meta.channels * (meta.bitsPerSample / 8);
    u8 pcm[1000 * 2 * 2];
    std::memset(pcm, 0, audioBytes);

    auto result = CafWriter::write(
        "test_audio.caf",
        AssetType::Audio,
        CAF_FLAG_NONE,
        &meta, sizeof(meta),
        pcm, audioBytes);

    REQUIRE(result.success);

    std::remove("test_audio.caf");
}

TEST_CASE("CafWriter - fails on invalid path", "[caf][writer]") {
    u8 dummy[4] = {1, 2, 3, 4};
    auto result = CafWriter::write(
        "/nonexistent/dir/test.caf",
        AssetType::Texture,
        CAF_FLAG_NONE,
        nullptr, 0,
        dummy, sizeof(dummy));

    REQUIRE_FALSE(result.success);
    REQUIRE(result.bytesWritten == 0);
}

TEST_CASE("BlobLoader::validateHeader - rejects nonexistent file", "[caf][loader]") {
    REQUIRE_FALSE(BlobLoader::validateHeader("no_such_file.caf"));
}

TEST_CASE("BlobLoader::validateHeader - accepts valid .caf file", "[caf][loader]") {
    u8 payload[16];
    std::memset(payload, 0xAB, sizeof(payload));

    auto wr = CafWriter::write(
        "test_validate.caf",
        AssetType::Shader,
        CAF_FLAG_NONE,
        nullptr, 0,
        payload, sizeof(payload));
    REQUIRE(wr.success);

    REQUIRE(BlobLoader::validateHeader("test_validate.caf"));

    std::remove("test_validate.caf");
}

TEST_CASE("BlobLoader::load - loads texture asset correctly", "[caf][loader]") {
    TextureMetadata meta{};
    meta.width     = 4;
    meta.height    = 4;
    meta.mipLevels = 1;

    const u32 payloadSize = 4 * 4 * 4;
    u8 pixels[4 * 4 * 4];
    for (u32 i = 0; i < payloadSize; ++i) pixels[i] = static_cast<u8>(i);

    auto wr = CafWriter::write(
        "test_load.caf",
        AssetType::Texture,
        CAF_FLAG_NONE,
        &meta, sizeof(meta),
        pixels, payloadSize);
    REQUIRE(wr.success);

    LinearAllocator alloc(4096);
    auto lr = BlobLoader::load("test_load.caf", &alloc);

    REQUIRE(lr.valid);
    REQUIRE(lr.header   != nullptr);
    REQUIRE(lr.metadata != nullptr);
    REQUIRE(lr.payload  != nullptr);

    REQUIRE(lr.header->magic    == CafHeader::kMagic);
    REQUIRE(lr.header->type     == AssetType::Texture);
    REQUIRE(lr.header->dataSize == payloadSize);

    const auto* loadedMeta = static_cast<const TextureMetadata*>(lr.metadata);
    REQUIRE(loadedMeta->width     == 4);
    REQUIRE(loadedMeta->height    == 4);
    REQUIRE(loadedMeta->mipLevels == 1);

    REQUIRE(std::memcmp(lr.payload, pixels, payloadSize) == 0);

    std::remove("test_load.caf");
}

TEST_CASE("BlobLoader::load - rejects nonexistent file", "[caf][loader]") {
    LinearAllocator alloc(4096);
    auto lr = BlobLoader::load("no_such_file.caf", &alloc);
    REQUIRE_FALSE(lr.valid);
}

TEST_CASE("BlobLoader::load - rejects corrupt header (footer CRC mismatch)", "[caf][loader]") {
    u8 payload[16] = {0};

    CafWriter::write(
        "test_corrupt_hdr.caf",
        AssetType::Texture,
        CAF_FLAG_NONE,
        nullptr, 0,
        payload, sizeof(payload));

    {
        std::FILE* f = std::fopen("test_corrupt_hdr.caf", "r+b");
        REQUIRE(f != nullptr);
        std::fseek(f, 4, SEEK_SET);
        u16 bad = 0xDEAD;
        std::fwrite(&bad, sizeof(u16), 1, f);
        std::fclose(f);
    }

    LinearAllocator alloc(1024);
    auto lr = BlobLoader::load("test_corrupt_hdr.caf", &alloc);
    REQUIRE_FALSE(lr.valid);

    std::remove("test_corrupt_hdr.caf");
}

TEST_CASE("BlobLoader::load - rejects corrupt payload (CRC mismatch)", "[caf][loader]") {
    u8 payload[32];
    std::memset(payload, 0x55, sizeof(payload));

    auto wr = CafWriter::write(
        "test_corrupt.caf",
        AssetType::Audio,
        CAF_FLAG_NONE,
        nullptr, 0,
        payload, sizeof(payload));
    REQUIRE(wr.success);

    {
        std::FILE* f = std::fopen("test_corrupt.caf", "r+b");
        REQUIRE(f != nullptr);
        std::fseek(f, 32 + 5, SEEK_SET);
        u8 bad = 0xDE;
        std::fwrite(&bad, 1, 1, f);
        std::fclose(f);
    }

    LinearAllocator alloc(4096);
    auto lr = BlobLoader::load("test_corrupt.caf", &alloc);
    REQUIRE_FALSE(lr.valid);

    std::remove("test_corrupt.caf");
}

TEST_CASE("BlobLoader::load - round-trips audio asset", "[caf][loader]") {
    AudioMetadata meta{};
    meta.sampleRate    = 48000;
    meta.channels      = 1;
    meta.bitsPerSample = 16;
    meta.sampleCount   = 256;
    meta.reserved      = 0;

    const u32 audioBytes = 256 * 1 * 2;
    u8 pcm[256 * 2];
    for (u32 i = 0; i < audioBytes; ++i) pcm[i] = static_cast<u8>(i & 0xFF);

    CafWriter::write(
        "test_audio_rt.caf",
        AssetType::Audio,
        CAF_FLAG_NONE,
        &meta, sizeof(meta),
        pcm, audioBytes);

    LinearAllocator alloc(8192);
    auto lr = BlobLoader::load("test_audio_rt.caf", &alloc);

    REQUIRE(lr.valid);
    REQUIRE(lr.header->type == AssetType::Audio);

    const auto* m = static_cast<const AudioMetadata*>(lr.metadata);
    REQUIRE(m->sampleRate    == 48000u);
    REQUIRE(m->channels      == 1u);
    REQUIRE(m->bitsPerSample == 16u);
    REQUIRE(m->sampleCount   == 256u);

    REQUIRE(std::memcmp(lr.payload, pcm, audioBytes) == 0);

    std::remove("test_audio_rt.caf");
}

TEST_CASE("BlobLoader::load - asset flags are preserved", "[caf][loader]") {
    u8 payload[8] = {0};

    CafWriter::write(
        "test_flags.caf",
        AssetType::Texture,
        CAF_FLAG_SRGB | CAF_FLAG_MIPCHAIN,
        nullptr, 0,
        payload, sizeof(payload));

    LinearAllocator alloc(1024);
    auto lr = BlobLoader::load("test_flags.caf", &alloc);

    REQUIRE(lr.valid);
    REQUIRE((lr.header->flags & CAF_FLAG_SRGB)     != 0);
    REQUIRE((lr.header->flags & CAF_FLAG_MIPCHAIN)  != 0);
    REQUIRE((lr.header->flags & CAF_FLAG_COMPRESSED) == 0);

    std::remove("test_flags.caf");
}
