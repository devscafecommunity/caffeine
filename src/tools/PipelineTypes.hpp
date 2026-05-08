#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include "assets/AssetTypes.hpp"
#include <string>
#include <vector>
#include <filesystem>

namespace Caffeine::Tools {
using namespace Caffeine;

struct MipLevel {
    u32 width  = 0;
    u32 height = 0;
    std::vector<u8> data;
};

struct MeshMetadata {
    u32 vertexCount  = 0;
    u32 indexCount   = 0;
    u32 subMeshCount = 0;
    u32 reserved     = 0;
};

static_assert(sizeof(MeshMetadata) == 16, "MeshMetadata must be 16 bytes");

struct ConversionResult {
    bool        success          = false;
    std::string errorMessage;
    u64         inputBytes       = 0;
    u64         outputBytes      = 0;
    f32         compressionRatio = 0.0f;
};

struct TextureEncodeOptions {
    enum class Format { RGBA8, RGB8, BC1, BC3, BC7 } format = Format::RGBA8;
    bool generateMipmaps  = true;
    u32  maxMipLevels     = 0;
    bool premultiplyAlpha = false;
    bool flipVertically   = false;
};

struct AudioEncodeOptions {
    enum class Format { PCM16, OGG_VORBIS } format = Format::PCM16;
    u32  targetSampleRate = 44100;
    bool mono             = false;
    f32  normalizeGain    = 0.0f;
};

struct MeshEncodeOptions {
    bool optimizeVertexCache = true;
    bool generateTangents    = true;
    bool compressVertices    = false;
    f32  lodReductionRatio   = 0.0f;
};

struct AssetManifestEntry {
    std::string       id;
    std::string       path;
    AssetType         type      = AssetType::Unknown;
    u64               sizeBytes = 0;
    u32               crc32     = 0;
};

} // namespace Caffeine::Tools
