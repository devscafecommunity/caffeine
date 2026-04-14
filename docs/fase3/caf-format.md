# 📄 Formato .caf — Caffeine Asset Format

> **Fases:** 3 (fundação) + 4 (Prefabs/Scenes)  
> **Namespace:** `Caffeine`  
> **Arquivo:** `src/core/io/CafTypes.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF3.0, RF4.5

---

## Visão Geral

O `.caf` (Caffeine Asset Format) é o formato binário nativo da engine. Filosofia: **Zero-parsing, Zero-copy** — o arquivo no disco é um espelho direto da memória RAM/VRAM.

Não há deserialização: o arquivo é lido como um bloco de bytes e os ponteiros são ajustados. Velocidade limitada apenas pela velocidade do SSD.

---

## Estrutura do Header

```cpp
namespace Caffeine {

// ============================================================================
// @brief  Tipos de asset suportados.
// ============================================================================
enum class AssetType : u16 {
    Unknown   = 0,
    Texture,       // RGBA8, BC7, ASTC → SDL_GPUTexture
    Audio,         // PCM, ADPCM → SDL_AudioStream
    Mesh,          // Vertex/Index Buffers (Fase 5)
    Prefab,        // ECS Entity Template (Binário)
    Scene,         // World State (Fase 4)
    Shader,        // SPIR-V / Bytecode
    Animation,     // AnimationClip frames
    Font           // Bitmap/SDF font atlas
};

// ============================================================================
// @brief  Header de todo arquivo .caf.
//
//  Tamanho: 32 bytes (2x alinhamento SIMD)
//  Magic:   0xCAFECAFE
// ============================================================================
struct CafHeader {
    u32       magic;           // 0xCAFECAFE — valida arquivo válido
    u16       versionMajor;    // Versão do formato (breaking changes)
    u16       versionMinor;    // Versão do formato (backwards compat)
    AssetType type;            // Tipo de dado armazenado
    u16       flags;           // Flags bitmask (compressed, encrypted, etc.)
    u64       metadataSize;    // Tamanho do bloco de metadados
    u64       dataSize;        // Tamanho do payload binário
    u64       alignment;       // 16 (geral) ou 32 bytes (SIMD/GPU mesh)
    u32       crc32;           // CRC32 do payload (para validação)
    u32       reserved;        // Padding para alinhamento 32 bytes
};

static_assert(sizeof(CafHeader) == 32, "CafHeader must be 32 bytes");

}  // namespace Caffeine
```

---

## Layout do Arquivo

```
Offset  Tamanho  Descrição
------  -------  -----------
0       32       CafHeader (magic + version + type + sizes + crc)
32      N        Metadata block (JSON ou binário, depende do tipo)
32+N    M        Payload binário (dados brutos — textura, mesh, etc.)
32+N+M  4        CRC32 do arquivo inteiro (verificação de integridade)
```

### Por Tipo de Asset

#### Texture (`AssetType::Texture`)
```
Metadata (24 bytes):
  u32 width
  u32 height
  u32 format    (SDL_GPUTextureFormat)
  u32 mipLevels
  u64 reserved

Payload:
  raw pixels (RGBA8 ou BC7 compressed)
```

#### Audio (`AssetType::Audio`)
```
Metadata (16 bytes):
  u32 sampleRate    (44100, 48000, etc.)
  u16 channels      (1 = mono, 2 = stereo)
  u16 bitsPerSample (8, 16, 32)
  u32 sampleCount
  u32 reserved

Payload:
  raw PCM data
```

#### Scene/Prefab (`AssetType::Scene` / `AssetType::Prefab`)
```
Scene Header (20 bytes após CafHeader):
  u32 entityCount
  u32 archetypeCount
  u32 stringTableOffset
  u32 assetRefTableOffset
  u32 reserved

Archetypes:
  [archetype_count × ArchetypeDesc]
  ArchetypeDesc:
    u32 componentMask
    u32 entityCount
    u32 dataOffset

Entity Data:
  [packed component data por archetype]

String Table:
  [null-terminated strings para nomes de entidades]

Asset Reference Table:
  [paths para assets referenciados pela cena]
```

---

## BlobLoader — Carregamento

```cpp
namespace Caffeine::IO {

// ============================================================================
// @brief  Loader de baixo nível para arquivos .caf.
//
//  Responsabilidades:
//  - Validar magic number e CRC32
//  - Alocar memória alinhada via LinearAllocator
//  - Ler arquivo em uma única chamada de IO (Single-Shot Read)
//  - Não faz parsing — retorna ponteiro para dados brutos
// ============================================================================
class BlobLoader {
public:
    struct LoadResult {
        const CafHeader* header    = nullptr;
        const void*      metadata  = nullptr;  // aponta para (data + 32)
        const void*      payload   = nullptr;  // aponta para (data + 32 + metadataSize)
        bool             valid     = false;
    };

    // Carregamento síncrono (usado pelo AssetManager em background job)
    static LoadResult load(const char* path, IAllocator* allocator);

    // Valida apenas o header sem carregar payload
    static bool validateHeader(const char* path);

private:
    static bool verifyCRC32(const void* data, u64 size, u32 expected);
};

}  // namespace Caffeine::IO
```

---

## AssetResolver — Despacho por Tipo

```cpp
namespace Caffeine::Assets {

// ============================================================================
// @brief  Converte blob bruto em objeto de asset tipado.
// ============================================================================
class AssetResolver {
public:
    static RHI::Texture* resolveTexture(
        const IO::BlobLoader::LoadResult& blob,
        RHI::RenderDevice* device);

    static AudioClip* resolveAudio(
        const IO::BlobLoader::LoadResult& blob);

    static ECS::World* resolveScene(
        const IO::BlobLoader::LoadResult& blob,
        ECS::World* targetWorld);

    static void resolvePrefab(
        const IO::BlobLoader::LoadResult& blob,
        ECS::World* world,
        Vec2 spawnPosition);
};

}  // namespace Caffeine::Assets
```

---

## Fluxo Completo de Carregamento

```
assets.loadAsync<Texture>("hero.caf")
          │
          ▼ Job System (Background)
    FileSystem::open("assets/hero.caf")
          │
          ▼
    BlobLoader::validateHeader()
     ├── magic == 0xCAFECAFE? ✓
     ├── version compatible?  ✓
     └── type == Texture?     ✓
          │
          ▼
    LinearAllocator::alloc(header.dataSize, header.alignment)
          │
          ▼
    Single-Shot fread(file, ptr, dataSize)   ← 1 syscall
          │
          ▼
    AssetResolver::resolveTexture(blob, device)
     └── SDL_GPUUploadTexture(device, pixels, width, height)
          │
          ▼
    handle.status = Loaded
    // Game code: tex.get() retorna RHI::Texture*
```

---

## Ferramentas de Criação de Assets (Fase 6)

Na Fase 6, o [Asset Pipeline](../fase6/asset-pipeline.md) converte formatos convencionais para `.caf`:

```
PNG → textures/hero.caf      (via asset-pipeline)
WAV → audio/jump.caf         (via asset-pipeline)
OBJ → meshes/player.caf      (via asset-pipeline, Fase 5)
```

---

## Requisitos de Engenharia

| Categoria | Requisito | Motivação |
|-----------|-----------|-----------|
| **Endianness** | Little-endian (x86/x64) | Zero byte-swap em x86 |
| **Alignment** | Geral: 16 bytes / Mesh: 32 bytes | SIMD + GPU bus |
| **IO Pattern** | Single-Shot Read | Minimiza seeks HDD/SSD |
| **Versioning** | Major.minor no header | Detecção de assets obsoletos |
| **CRC32** | Verificação de integridade | Detecta corrupção silenciosa |

---

## Benchmark de Aceitação

**Mega-Bundle:** 1GB de assets mistos (texturas, áudios, meshes)

| Métrica | Alvo |
|---------|------|
| Throughput | Limitado apenas pela velocidade do SSD |
| CPU usage durante load | < 2% |
| Time to first asset | < 50ms |
| Zero copies | CPU não copia dados em memória separada |

---

## Dependências

- **Upstream:** [Job System](../fase2/job-system.md), [Memory — LinearAllocator](../architecture/memory.md)
- **Downstream:** [Asset Manager](asset-manager.md), [Fase 4 — Scene Serialization](../fase4/scene.md), [Fase 5 — Mesh Loading](../fase5/mesh-loading.md), [Fase 6 — Asset Pipeline](../fase6/asset-pipeline.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §5 Scene Serialization, §7 Asset Manager
- [`docs/ROADMAP.md`](../ROADMAP.md) — §4.2 Sistema de Assets — O Formato .caf
- [`docs/fase3/asset-manager.md`](asset-manager.md) — usa BlobLoader internamente
- [`docs/fase6/asset-pipeline.md`](../fase6/asset-pipeline.md) — criação de .caf
