// ============================================================================
// @file    CafTypes.hpp
// @brief   Caffeine Asset Format (.caf) — type definitions
//
//  Philosophy: Zero-parsing, Zero-copy.
//  The file on disk is a direct mirror of RAM/VRAM layout.
//  No deserialization: file is read as a raw byte block.
//
//  File layout:
//    [0 ..  31]  CafHeader        (32 bytes, SIMD-aligned)
//    [32 .. 32+N-1]  Metadata block   (N = header.metadataSize)
//    [32+N .. 32+N+M-1]  Payload      (M = header.dataSize)
//    [32+N+M .. 32+N+M+3]  CRC32 footer (whole-file integrity)
//
//  Endianness: Little-endian (x86/x64 native, zero byte-swap)
//  Magic:      0xCAFECAFE
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include <functional>

namespace Caffeine {

// ============================================================================
// @brief  Asset type discriminator stored in every .caf header.
// ============================================================================
enum class AssetType : u16 {
    Unknown   = 0,
    Texture,       // RGBA8, BC7, ASTC  → SDL_GPUTexture
    Audio,         // PCM, ADPCM        → SDL_AudioStream
    Mesh,          // Vertex/Index Buffers (Fase 5)
    Prefab,        // ECS Entity Template (binary)
    Scene,         // World State (Fase 4)
    Shader,        // SPIR-V / bytecode
    Animation,     // AnimationClip frames
    Font           // Bitmap/SDF font atlas
};

// ============================================================================
// @brief  Header flags bitmask (stored in CafHeader::flags).
// ============================================================================
enum CafFlags : u16 {
    CAF_FLAG_NONE        = 0x0000,
    CAF_FLAG_COMPRESSED  = 0x0001,  // payload is compressed
    CAF_FLAG_ENCRYPTED   = 0x0002,  // payload is encrypted
    CAF_FLAG_SRGB        = 0x0004,  // texture: sRGB colour space
    CAF_FLAG_MIPCHAIN    = 0x0008,  // texture: mip-chain present
    CAF_FLAG_STREAMED    = 0x0010,  // asset should be streamed
};

// ============================================================================
// @brief  32-byte header for every .caf file.
//
//  Layout (natural alignment, little-endian):
//    offset  0  u32  magic          4 bytes
//    offset  4  u16  versionMajor   2 bytes
//    offset  6  u16  versionMinor   2 bytes
//    offset  8  u16  type           2 bytes  (AssetType)
//    offset 10  u16  flags          2 bytes  (CafFlags)
//    offset 12  u32  crc32          4 bytes  (CRC32 of payload only)
//    offset 16  u64  metadataSize   8 bytes
//    offset 24  u64  dataSize       8 bytes
//                                  --------
//                                  32 bytes  ✓
// ============================================================================
struct CafHeader {
    u32       magic;         // 0xCAFECAFE — file identity marker
    u16       versionMajor;  // breaking format changes
    u16       versionMinor;  // backwards-compatible changes
    AssetType type;          // asset kind
    u16       flags;         // CafFlags bitmask
    u32       crc32;         // CRC32 of the raw payload bytes
    u64       metadataSize;  // size of the metadata block in bytes
    u64       dataSize;      // size of the binary payload in bytes

    // -------------------------------------------------------------------------
    // Compile-time constants
    // -------------------------------------------------------------------------
    static constexpr u32  kMagic          = 0xCAFECAFEu;
    static constexpr u16  kVersionMajor   = 1;
    static constexpr u16  kVersionMinor   = 0;
    static constexpr u32  kHeaderSize     = 32;
    static constexpr u32  kFooterSize     = 4;   // trailing whole-file CRC32

    // -------------------------------------------------------------------------
    // @brief  Required payload alignment for this asset type.
    //         Mesh/Shader: 32 bytes (SIMD + GPU bus)
    //         All others:  16 bytes (SIMD)
    // -------------------------------------------------------------------------
    [[nodiscard]] constexpr u32 payloadAlignment() const noexcept {
        return (type == AssetType::Mesh || type == AssetType::Shader) ? 32u : 16u;
    }

    // -------------------------------------------------------------------------
    // @brief  Total byte size of the full .caf file on disk.
    // -------------------------------------------------------------------------
    [[nodiscard]] constexpr u64 totalFileSize() const noexcept {
        return kHeaderSize + metadataSize + dataSize + kFooterSize;
    }
};

static_assert(sizeof(CafHeader)  == 32, "CafHeader must be exactly 32 bytes");
static_assert(alignof(CafHeader) ==  8, "CafHeader must be 8-byte aligned");

// ============================================================================
// Metadata structs — one per asset type.
// These are written immediately after the CafHeader in the metadata block.
// ============================================================================

// ----------------------------------------------------------------------------
// @brief  Texture metadata (24 bytes, offset 32 in file).
// ----------------------------------------------------------------------------
struct TextureMetadata {
    u32 width;       // pixels
    u32 height;      // pixels
    u32 format;      // SDL_GPUTextureFormat (or 0 for RGBA8)
    u32 mipLevels;   // 1 = no mips
    u64 reserved;    // future use, must be zero
};
static_assert(sizeof(TextureMetadata) == 24, "TextureMetadata must be 24 bytes");

// ----------------------------------------------------------------------------
// @brief  Audio metadata (16 bytes, offset 32 in file).
// ----------------------------------------------------------------------------
struct AudioMetadata {
    u32 sampleRate;    // e.g. 44100, 48000
    u16 channels;      // 1 = mono, 2 = stereo
    u16 bitsPerSample; // 8, 16, or 32
    u32 sampleCount;   // total sample frames
    u32 reserved;      // future use, must be zero
};
static_assert(sizeof(AudioMetadata) == 16, "AudioMetadata must be 16 bytes");

// ----------------------------------------------------------------------------
// @brief  Scene / Prefab metadata header (20 bytes, offset 32 in file).
// ----------------------------------------------------------------------------
struct SceneMetadata {
    u32 entityCount;          // total entities in the scene
    u32 archetypeCount;       // number of distinct archetypes
    u32 stringTableOffset;    // byte offset from start of metadata block
    u32 assetRefTableOffset;  // byte offset from start of metadata block
    u32 reserved;             // future use, must be zero
};
static_assert(sizeof(SceneMetadata) == 20, "SceneMetadata must be 20 bytes");

// ----------------------------------------------------------------------------
// @brief  Per-archetype descriptor (12 bytes, follows SceneMetadata).
// ----------------------------------------------------------------------------
struct ArchetypeDesc {
    u32 componentMask;  // bitmask of component types present
    u32 entityCount;    // entities belonging to this archetype
    u32 dataOffset;     // byte offset into the payload where data begins
};
static_assert(sizeof(ArchetypeDesc) == 12, "ArchetypeDesc must be 12 bytes");

// ----------------------------------------------------------------------------
// @brief  Shader metadata (8 bytes, offset 32 in file).
// ----------------------------------------------------------------------------
struct ShaderMetadata {
    u32 stage;    // shader stage (0=vertex, 1=fragment, 2=compute)
    u32 reserved; // future use, must be zero
};
static_assert(sizeof(ShaderMetadata) == 8, "ShaderMetadata must be 8 bytes");

} // namespace Caffeine
