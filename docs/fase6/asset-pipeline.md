# 🔧 Asset Pipeline

> **Fase:** 6 — O Olimpo  
> **Namespace:** `Caffeine::Tools`  
> **Status:** 📅 Planejado  
> **RFs:** RF6.5

---

## Visão Geral

O **Asset Pipeline** é a ferramenta de linha de comando (CLI) responsável por converter assets em formato bruto (PNG, WAV, OBJ, GLTF) para o formato binário optimizado `.caf` — o formato nativo da Caffeine Engine.

O pipeline opera **fora do runtime** — é executado antes do jogo, durante o build. Em modo editor (Fase 6), pode ser invocado automaticamente ao detectar mudanças em assets.

**Fluxo geral:**
```
assets/raw/          →     [ caf-encode CLI ]     →     assets/
hero.png                   (conversão + compressão)      hero.caf
bgm.wav                    (geração de mipmaps)          bgm.caf
level01.gltf               (validação de formato)        level01.caf
```

---

## API Planejada

```cpp
namespace Caffeine::Tools {

// ============================================================================
// @brief  Resultado de uma operação de conversão.
// ============================================================================
struct ConversionResult {
    bool        success = false;
    std::string errorMessage;
    u64         inputBytes  = 0;
    u64         outputBytes = 0;
    f32         compressionRatio = 0.0f;  // outputBytes / inputBytes
};

// ============================================================================
// @brief  Opções de conversão de textura.
// ============================================================================
struct TextureEncodeOptions {
    enum class Format { RGBA8, RGB8, BC1, BC3, BC7 } format = Format::RGBA8;
    bool generateMipmaps = true;
    u32  maxMipLevels    = 0;    // 0 = automático (log2 da dimensão maior)
    bool premultiplyAlpha = false;
    bool flipVertically   = false;
};

// ============================================================================
// @brief  Opções de conversão de áudio.
// ============================================================================
struct AudioEncodeOptions {
    enum class Format { PCM16, OGG_VORBIS } format = Format::PCM16;
    u32  targetSampleRate = 44100;
    bool mono             = false;   // mixar para mono (reduz tamanho)
    f32  normalizeGain    = 0.0f;    // 0 = sem normalização
};

// ============================================================================
// @brief  Opções de conversão de mesh.
// ============================================================================
struct MeshEncodeOptions {
    bool optimizeVertexCache = true;    // overdraw reduction
    bool generateTangents    = true;
    bool compressVertices    = false;   // quantização de vértices
    f32  lodReductionRatio   = 0.0f;    // 0 = sem LOD, 0.5 = 50% redução
};

// ============================================================================
// @brief  Conversor de textura PNG/BMP/TGA → .caf
// ============================================================================
class TextureEncoder {
public:
    // Converte um único arquivo de textura
    ConversionResult encode(std::string_view inputPath,
                            std::string_view outputPath,
                            const TextureEncodeOptions& opts = {});

    // Gera apenas os dados de mipmaps (sem reescrever o arquivo)
    std::vector<Assets::MipLevel> generateMipmaps(const u8* pixels,
                                                   u32 width, u32 height,
                                                   u32 channels);
private:
    ConversionResult writeCafTexture(std::string_view outputPath,
                                     const Assets::CafHeader& header,
                                     const std::vector<u8>& data,
                                     const TextureEncodeOptions& opts);
};

// ============================================================================
// @brief  Conversor de áudio WAV/MP3/OGG → .caf
// ============================================================================
class AudioEncoder {
public:
    ConversionResult encode(std::string_view inputPath,
                            std::string_view outputPath,
                            const AudioEncodeOptions& opts = {});
private:
    ConversionResult encodeWav(std::string_view inputPath,
                               std::string_view outputPath,
                               const AudioEncodeOptions& opts);
};

// ============================================================================
// @brief  Conversor de mesh OBJ/GLTF → .caf
// ============================================================================
class MeshEncoder {
public:
    ConversionResult encode(std::string_view inputPath,
                            std::string_view outputPath,
                            const MeshEncodeOptions& opts = {});
private:
    ConversionResult encodeObj(std::string_view inputPath,
                               std::string_view outputPath,
                               const MeshEncodeOptions& opts);
    ConversionResult encodeGltf(std::string_view inputPath,
                                std::string_view outputPath,
                                const MeshEncodeOptions& opts);
};

// ============================================================================
// @brief  Manifesto de assets do projeto.
//
//  O AssetManifest é um ficheiro JSON que lista todos os assets do projeto
//  com metadados (tipo, caminho, hash, tamanho). Permite ao AssetManager
//  da engine encontrar e carregar assets por nome lógico.
//
//  Exemplo de manifesto gerado:
//  {
//    "version": 1,
//    "assets": [
//      { "id": "hero",    "path": "sprites/hero.caf",    "type": "Texture" },
//      { "id": "bgm",     "path": "audio/bgm.caf",       "type": "Audio"   },
//      { "id": "level01", "path": "meshes/level01.caf",  "type": "Mesh"    }
//    ]
//  }
// ============================================================================
struct AssetManifestEntry {
    std::string         id;       // nome lógico (sem extensão)
    std::string         path;     // caminho relativo ao /assets/
    Assets::AssetType   type;
    u64                 sizeBytes = 0;
    u32                 crc32     = 0;     // para detecção de mudanças
};

class AssetManifest {
public:
    bool load(std::string_view manifestPath);
    bool save(std::string_view manifestPath) const;

    void addEntry(AssetManifestEntry entry);
    void removeEntry(std::string_view id);

    const AssetManifestEntry* find(std::string_view id) const;
    const std::vector<AssetManifestEntry>& entries() const { return m_entries; }

private:
    std::vector<AssetManifestEntry> m_entries;
};

// ============================================================================
// @brief  Pipeline de batch — converte uma pasta inteira de assets.
//
//  Comportamento:
//  1. Descobre todos os assets suportados em inputDir (recursivo)
//  2. Para cada asset, verifica se output .caf está desatualizado (via CRC)
//  3. Converte apenas os assets desatualizados (build incremental)
//  4. Atualiza o AssetManifest com as novas entradas
//  5. Gera relatório final (N convertidos, M ignorados, K erros)
// ============================================================================
class AssetPipeline {
public:
    struct BatchOptions {
        std::string inputDir;
        std::string outputDir;
        std::string manifestPath;   // se vazio, não gera manifesto
        bool        forceRebuild    = false;  // ignorar CRC, reconverter tudo
        bool        verbose         = false;
        u32         threadCount     = 4;      // conversões em paralelo
    };

    struct BatchReport {
        u32 converted = 0;
        u32 skipped   = 0;   // sem mudanças (CRC igual)
        u32 errors    = 0;
        std::vector<std::string> errorMessages;
        f64 totalTimeSeconds = 0.0;
        u64 totalInputBytes  = 0;
        u64 totalOutputBytes = 0;
    };

    BatchReport run(const BatchOptions& opts);

private:
    TextureEncoder m_textureEncoder;
    AudioEncoder   m_audioEncoder;
    MeshEncoder    m_meshEncoder;

    Assets::AssetType detectAssetType(const std::filesystem::path& path) const;
    bool isOutdated(const std::filesystem::path& src,
                    const std::filesystem::path& dst) const;
};

}  // namespace Caffeine::Tools
```

---

## CLI: caf-encode

```bash
# Converter um único arquivo:
caf-encode texture hero.png assets/sprites/hero.caf --mipmaps --format BC3

# Converter áudio:
caf-encode audio bgm.wav assets/audio/bgm.caf --format ogg --sample-rate 44100

# Converter mesh:
caf-encode mesh level01.gltf assets/meshes/level01.caf --optimize --tangents

# Processar pasta inteira (build incremental):
caf-encode batch ./raw/ ./assets/ --manifest assets/manifest.json --jobs 8

# Forçar reconversão de tudo:
caf-encode batch ./raw/ ./assets/ --force-rebuild

# Apenas verificar o que seria convertido (dry-run):
caf-encode batch ./raw/ ./assets/ --dry-run
```

---

## Estrutura de Diretórios

```
project/
├── raw/                    ← assets originais (não vão no build final)
│   ├── sprites/
│   │   ├── hero.png
│   │   └── enemy.png
│   ├── audio/
│   │   ├── bgm.wav
│   │   └── jump.wav
│   └── meshes/
│       ├── level01.gltf
│       └── level01_textures/
│           └── diffuse.png
│
├── assets/                 ← assets convertidos (incluídos no build)
│   ├── sprites/
│   │   ├── hero.caf        ← textura RGBA8 + mipmaps
│   │   └── enemy.caf
│   ├── audio/
│   │   ├── bgm.caf         ← OGG Vorbis
│   │   └── jump.caf        ← PCM16
│   ├── meshes/
│   │   └── level01.caf     ← Mesh3D + materiais + texturas embutidas
│   └── manifest.json       ← índice de todos os assets
│
└── build.sh / CMakeLists   ← invoca caf-encode batch antes do link
```

---

## Formato .caf — Estrutura do Header

```cpp
// Header de 32 bytes (definido em fase3/caf-format.md):
struct CafHeader {
    u32  magic;        // 0xCAFECAFE
    u8   version;      // 1
    u8   assetType;    // Texture=0, Audio=1, Mesh=2, etc.
    u16  flags;        // COMPRESSED=0x01, MIPMAPS=0x02, etc.
    u32  dataSize;     // tamanho do payload em bytes
    u32  crc32;        // checksum do payload
    u32  reserved[4];  // alinhamento + uso futuro
};
```

Para a especificação completa do formato, ver [`docs/fase3/caf-format.md`](../fase3/caf-format.md).

---

## Integração com o Build (CMake)

```cmake
# CMakeLists.txt — executar pipeline antes de compilar o jogo:
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/assets/manifest.json
    COMMAND caf-encode batch
            ${CMAKE_SOURCE_DIR}/raw/
            ${CMAKE_BINARY_DIR}/assets/
            --manifest ${CMAKE_BINARY_DIR}/assets/manifest.json
            --jobs 4
    DEPENDS ${RAW_ASSET_FILES}
    COMMENT "Converting raw assets to .caf format..."
    VERBATIM
)

add_custom_target(assets DEPENDS ${CMAKE_BINARY_DIR}/assets/manifest.json)
add_dependencies(${GAME_TARGET} assets)
```

---

## Build Incremental (CRC-based)

```
raw/hero.png  → CRC: 0xABCD1234  →  assets/hero.caf existe?
                                          │
                                     ┌────┴────┐
                                     │  Existe  │
                                     └────┬─────┘
                                          │
                              CRC stored == CRC actual?
                                     │          │
                                    SIM         NÃO
                                     │          │
                                  SKIP       CONVERTER
                                (0% CPU)   (TextureEncoder)
```

---

## Geração de Mipmaps

```
Textura original: 512×512 (RGBA8, 1MB)
│
├── Mip 0: 512×512 (1MB)
├── Mip 1: 256×256 (256KB)
├── Mip 2: 128×128 (64KB)
├── Mip 3:  64×64  (16KB)
├── Mip 4:  32×32  (4KB)
├── Mip 5:  16×16  (1KB)
├── Mip 6:   8×8   (256B)
├── Mip 7:   4×4   (64B)
├── Mip 8:   2×2   (16B)
└── Mip 9:   1×1   (4B)

Total: ~1.33MB (apenas 33% de overhead)

Algoritmo de filtragem: Lanczos-3 (alta qualidade) ou Box (rápido)
Configurável via --mip-filter lanczos|box
```

---

## Critério de Aceitação

- [ ] `caf-encode texture` converte PNG → .caf com mipmaps correctos
- [ ] `caf-encode audio` converte WAV → .caf PCM16 ou OGG
- [ ] `caf-encode mesh` converte OBJ e GLTF → .caf Mesh3D
- [ ] `caf-encode batch` processa pasta inteira em paralelo
- [ ] Build incremental: só reconverte assets modificados (CRC)
- [ ] Manifesto JSON gerado com todos os assets convertidos
- [ ] AssetManager da engine consegue carregar assets pelo manifesto
- [ ] Hot-reload (RF6.6): mudança em raw/ → re-conversão automática → reload no editor

---

## Dependências

- **Upstream:**
  - [Asset Manager](../fase3/asset-manager.md) — formato CafHeader, AssetType
  - [CAF Format](../fase3/caf-format.md) — especificação binária do .caf
  - [Mesh Loading](../fase5/mesh-loading.md) — Vertex3D, Mesh3D, SubMesh
  - [Audio System](../fase4/audio.md) — AudioClip, AudioFormat
- **Downstream:**
  - — (executado antes do runtime, não há downstream em runtime)

---

## Referências

- [`docs/fase3/caf-format.md`](../fase3/caf-format.md) — Formato binário .caf
- [`docs/fase3/asset-manager.md`](../fase3/asset-manager.md) — AssetManager + AssetHandle
- [`docs/fase5/mesh-loading.md`](../fase5/mesh-loading.md) — Mesh3D + Vertex3D
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [stb_image](https://github.com/nothings/stb) — decodificação de PNG/JPG/BMP
- [dr_wav](https://github.com/mackron/dr_libs) — decodificação de WAV
- [cgltf](https://github.com/jkuhlmann/cgltf) — parser de GLTF
- [meshoptimizer](https://github.com/zeux/meshoptimizer) — optimização de vertex cache
- [BC7Enc](https://github.com/richgel999/bc7enc) — compressão de texturas BC1/BC3/BC7
