# 📄 Formato .caf — Caffeine Asset Format

> **Fases:** 3 (fundação) + 4 (Prefabs/Scenes)
> **Namespace:** `Caffeine`, `Caffeine::IO`
> **Arquivos:** `src/core/io/CafTypes.hpp`, `src/core/io/BlobLoader.hpp/.cpp`, `src/core/io/CafWriter.hpp/.cpp`, `src/core/io/Crc32.hpp`
> **Status:** ✅ Implementado
> **RF:** RF3.0, RF4.5

---

## Visão Geral

O `.caf` (Caffeine Asset Format) é o formato binário nativo da engine. Filosofia: **Zero-parsing, Zero-copy** — o arquivo no disco é um espelho direto da memória RAM/VRAM.

Não há deserialização: o arquivo é lido como um bloco de bytes e os ponteiros são ajustados. Velocidade limitada apenas pelo SSD.

---

## Layout do Arquivo

```
Offset          Tamanho  Descrição
--------------  -------  -----------
0               32       CafHeader (magic, version, type, crc32, sizes)
32              N        Metadata block (struct dependente do AssetType)
32 + N          M        Payload binário (pixels, PCM, geometry, etc.)
32 + N + M      4        Footer CRC32 (CRC32 do arquivo inteiro)
```

O footer de 4 bytes protege contra corrupção silenciosa do arquivo completo. O `crc32` no header protege apenas o payload.

### Constantes do Formato

| Constante | Valor |
|-----------|-------|
| `CafHeader::kMagic` | `0xCAFECAFE` |
| `CafHeader::kVersionMajor` | `1` |
| `CafHeader::kVersionMinor` | `0` |
| `CafHeader::kHeaderSize` | `32` bytes |
| `CafHeader::kFooterSize` | `4` bytes |
| Alinhamento geral | `16` bytes (SIMD) |
| Alinhamento Mesh/Shader | `32` bytes (SIMD + GPU bus) |
| Endianness | Little-endian (x86/x64 nativo) |

---

## `CafHeader` — 32 bytes

```cpp
namespace Caffeine {

struct CafHeader {
    u32       magic;         // offset  0 — 0xCAFECAFE
    u16       versionMajor;  // offset  4 — breaking changes
    u16       versionMinor;  // offset  6 — backwards-compatible
    AssetType type;          // offset  8 — AssetType (u16)
    u16       flags;         // offset 10 — CafFlags bitmask
    u32       crc32;         // offset 12 — CRC32 do payload
    u64       metadataSize;  // offset 16 — bytes do bloco de metadata
    u64       dataSize;      // offset 24 — bytes do payload
};                           //            = 32 bytes total ✓

static_assert(sizeof(CafHeader) == 32);
}
```

**Layout em bytes:**
```
byte  0- 3   magic          (u32)
byte  4- 5   versionMajor   (u16)
byte  6- 7   versionMinor   (u16)
byte  8- 9   type           (u16, AssetType enum)
byte 10-11   flags          (u16, CafFlags bitmask)
byte 12-15   crc32          (u32, CRC32 do payload)
byte 16-23   metadataSize   (u64)
byte 24-31   dataSize       (u64)
```

---

## `AssetType`

```cpp
enum class AssetType : u16 {
    Unknown   = 0,
    Texture,       // RGBA8, BC7, ASTC → SDL_GPUTexture
    Audio,         // PCM, ADPCM       → SDL_AudioStream
    Mesh,          // Vertex/Index Buffers (Fase 5)
    Prefab,        // ECS Entity Template (binário)
    Scene,         // World State (Fase 4)
    Shader,        // SPIR-V / bytecode
    Animation,     // AnimationClip frames
    Font           // Bitmap/SDF font atlas
};
```

---

## `CafFlags`

```cpp
enum CafFlags : u16 {
    CAF_FLAG_NONE        = 0x0000,
    CAF_FLAG_COMPRESSED  = 0x0001,  // payload comprimido
    CAF_FLAG_ENCRYPTED   = 0x0002,  // payload encriptado
    CAF_FLAG_SRGB        = 0x0004,  // textura: espaço de cor sRGB
    CAF_FLAG_MIPCHAIN    = 0x0008,  // textura: mip-chain presente
    CAF_FLAG_STREAMED    = 0x0010,  // asset deve ser transmitido em streaming
};
```

---

## Structs de Metadata

Cada tipo de asset tem um struct de metadata fixo escrito imediatamente após o `CafHeader` (offset 32).

### `TextureMetadata` — 24 bytes

```cpp
struct TextureMetadata {
    u32 width;       // largura em pixels
    u32 height;      // altura em pixels
    u32 format;      // SDL_GPUTextureFormat (0 = RGBA8)
    u32 mipLevels;   // 1 = sem mips
    u64 reserved;    // zero
};
```

**Payload:** pixels brutos (RGBA8 ou comprimido BC7/ASTC).

### `AudioMetadata` — 16 bytes

```cpp
struct AudioMetadata {
    u32 sampleRate;    // 44100, 48000, etc.
    u16 channels;      // 1 = mono, 2 = stereo
    u16 bitsPerSample; // 8, 16 ou 32
    u32 sampleCount;   // total de sample frames
    u32 reserved;      // zero
};
```

**Payload:** PCM raw.

### `SceneMetadata` — 20 bytes

```cpp
struct SceneMetadata {
    u32 entityCount;          // total de entidades
    u32 archetypeCount;       // arquétipos distintos
    u32 stringTableOffset;    // offset relativo ao início do bloco de metadata
    u32 assetRefTableOffset;  // offset relativo ao início do bloco de metadata
    u32 reserved;             // zero
};
```

Seguido por `archetypeCount × ArchetypeDesc`:

```cpp
struct ArchetypeDesc {  // 12 bytes
    u32 componentMask;  // bitmask dos tipos de componente
    u32 entityCount;    // entidades neste arquétipo
    u32 dataOffset;     // offset no payload onde os dados começam
};
```

### `ShaderMetadata` — 8 bytes

```cpp
struct ShaderMetadata {
    u32 stage;    // 0=vertex, 1=fragment, 2=compute
    u32 reserved; // zero
};
```

---

## `Crc32` — `src/core/io/Crc32.hpp`

IEEE 802.3 CRC32 com lookup table `constexpr`. Zero dependências externas.

```cpp
namespace Caffeine::IO {

// CRC32 de um bloco de memória
constexpr u32 crc32(const void* data, usize size);

// CRC32 incremental (streaming)
constexpr u32 crc32Continue(u32 runningCrc, const void* data, usize size);

} // namespace Caffeine::IO
```

---

## `BlobLoader` — `src/core/io/BlobLoader.hpp`

Loader de baixo nível. Um único `fread`, zero seeks, zero cópias extras.

```cpp
namespace Caffeine::IO {

class BlobLoader {
public:
    struct LoadResult {
        const CafHeader* header   = nullptr; // aponta para início do buffer
        const void*      metadata = nullptr; // aponta para offset 32
        const void*      payload  = nullptr; // aponta para offset 32 + metadataSize
        bool             valid    = false;
    };

    // Carrega e valida o arquivo inteiro. Aloca via allocator.
    static LoadResult load(const char* path, IAllocator* allocator);

    // Valida magic + version sem carregar o payload.
    static bool validateHeader(const char* path);

private:
    static bool verifyCRC32(const void* data, u64 size, u32 expected);
};

} // namespace Caffeine::IO
```

### Sequência de Validação

```
BlobLoader::load(path, allocator)
    │
    ├── fopen()
    ├── fread() [single shot — todo o arquivo de uma vez]
    │
    ├── magic == 0xCAFECAFE ?            ← rejeita arquivo inválido
    ├── versionMajor == 1 ?              ← rejeita versão incompatível
    ├── CRC32(payload) == header.crc32 ? ← rejeita payload corrompido
    └── CRC32(file[0..end-4]) == footer? ← rejeita arquivo corrompido
          │
          ▼
    LoadResult { header, metadata, payload, valid=true }
```

---

## `CafWriter` — `src/core/io/CafWriter.hpp`

Escreve um arquivo `.caf` completo com header, metadata, payload e footer CRC32.

```cpp
namespace Caffeine::IO {

class CafWriter {
public:
    struct WriteResult {
        bool success      = false;
        u64  bytesWritten = 0;
    };

    static WriteResult write(
        const char* path,
        AssetType   type,
        u16         flags,
        const void* metadata,
        u64         metadataSize,
        const void* payload,
        u64         payloadSize);
};

} // namespace Caffeine::IO
```

### Sequência de Escrita

```
CafWriter::write(...)
    │
    ├── Monta CafHeader:
    │     magic         = 0xCAFECAFE
    │     versionMajor  = 1, versionMinor = 0
    │     type          = type
    │     flags         = flags
    │     crc32         = CRC32(payload)
    │     metadataSize  = metadataSize
    │     dataSize      = payloadSize
    │
    ├── fwrite(header)
    ├── fwrite(metadata)
    ├── fwrite(payload)
    └── fwrite(footer = CRC32(header + metadata + payload))
          │
          ▼
    WriteResult { success=true, bytesWritten=totalSize }
```

---

## Fluxo de Carregamento de Asset (Fase 3+)

```
AssetManager::loadAsync<Texture>("hero.caf")
      │
      ▼  Job System (background thread)
BlobLoader::validateHeader("hero.caf")
      ├── magic ✓, version ✓
      │
      ▼
LinearAllocator::alloc(totalFileSize, 16)
      │
      ▼
fread(file, buffer, totalFileSize)   ← 1 syscall
      │
      ▼
BlobLoader::load() — valida CRCs
      │
      ▼
AssetResolver::resolveTexture(blob, device)
      └── SDL_GPUUploadTexture(device, pixels, width, height)
            │
            ▼
      AssetHandle<Texture> { status = Loaded }
```

---

## Ferramenta de Criação (Fase 6)

Na Fase 6, o [Asset Pipeline](../assets/asset-pipeline.md) converterá formatos convencionais:

```
PNG  → textures/hero.caf      (CafWriter::write, AssetType::Texture)
WAV  → audio/jump.caf         (CafWriter::write, AssetType::Audio)
OBJ  → meshes/player.caf      (CafWriter::write, AssetType::Mesh)
GLSL → shaders/sprite.caf     (CafWriter::write, AssetType::Shader)
```

---

## Testes

`tests/test_caf.cpp` — 27 test cases, 53 assertions:

| Categoria | Cobertura |
|-----------|-----------|
| `CafHeader` layout | `static_assert` tamanho, alinhamento, endianness |
| `CafHeader` helpers | `payloadAlignment()`, `totalFileSize()` |
| `AssetType` discriminação | Todos os 8 tipos |
| `CafFlags` bitmask | Combinações |
| `TextureMetadata` | Layout 24 bytes |
| `AudioMetadata` | Layout 16 bytes |
| `SceneMetadata` + `ArchetypeDesc` | Layout 20 + 12 bytes |
| `ShaderMetadata` | Layout 8 bytes |
| CRC32 | Vetores IEEE 802.3, streaming, continue |
| `CafWriter::write` | Round-trip completo |
| `BlobLoader::load` | Round-trip completo |
| `BlobLoader` corrupção | Header CRC inválido, payload CRC inválido, footer CRC inválido |

---

## Requisitos de Engenharia

| Categoria | Requisito | Status |
|-----------|-----------|--------|
| Endianness | Little-endian (x86/x64) — `static_assert` em compile time | ✅ |
| Alinhamento | Geral 16B, Mesh/Shader 32B — `payloadAlignment()` | ✅ |
| IO Pattern | Single-shot `fread` — zero seeks, zero cópias extras | ✅ |
| Versioning | `versionMajor`/`versionMinor` no header — rejeição em load | ✅ |
| CRC32 payload | `header.crc32` — detecta corrupção do payload | ✅ |
| CRC32 footer | Footer de 4 bytes — detecta corrupção de qualquer byte do arquivo | ✅ |
| Header size | 32 bytes — 2× alinhamento SIMD — `static_assert` | ✅ |

---

## Dependências

- **Upstream:** [Job System](../concurrency/job-system.md), [Memory — LinearAllocator](../memory/allocators.md)
- **Downstream:** [Asset Manager](asset-manager.md), [Fase 4 — Scene Serialization](../ecs/scene.md), [Fase 5 — Mesh Loading](../assets/mesh-loading.md), [Fase 6 — Asset Pipeline](../assets/asset-pipeline.md)

---

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Assets & Pipeline]() | .caf como formato nativo de assets |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §5 Scene Serialization, §7 Asset Manager
- [`docs/HISTORY.md`](../HISTORY.md) — §4.2 Sistema de Assets — O Formato .caf
- [`assets/asset-manager.md`](asset-manager.md) — usa `BlobLoader` internamente
- [`assets/asset-pipeline.md`](../assets/asset-pipeline.md) — produz arquivos `.caf`
- [Índice de Tópicos Transversais]()
