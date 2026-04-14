# 📦 Asset Manager

> **Fase:** 3 — O Olho da Engine  
> **Namespace:** `Caffeine::Assets`  
> **Arquivos:** `src/assets/AssetManager.hpp`, `src/assets/AssetHandle.hpp`  
> **Status:** 📅 Planejado  
> **RFs:** RF3.8, RF3.9

---

## Visão Geral

O Asset Manager carrega, faz cache e fornece assets (texturas, áudio, shaders, modelos) de forma transparente ao jogo. Suporta **async loading** via [Job System](../fase2/job-system.md) e **hot-reload** em desenvolvimento.

**Filosofia:** O código do jogo pede um asset pelo caminho; o Asset Manager retorna um handle. O jogo nunca vê raw bytes — só `AssetHandle<Texture>`.

---

## API Planejada

```cpp
namespace Caffeine::Assets {

// ============================================================================
// @brief  Tipos de asset suportados.
// ============================================================================
enum class AssetType : u8 {
    Texture,    // Imagem → SDL_GPUTexture
    Audio,      // PCM/ADPCM → SDL_AudioStream
    Shader,     // SPIR-V/HLSL bytecode
    Font,       // TrueType/Bitmap font
    Scene,      // ECS world snapshot (.caf)
    Mesh,       // Vertex/Index buffers (Fase 5)
    Animation   // AnimationClip sequence
};

// ============================================================================
// @brief  Status de carregamento de um asset.
// ============================================================================
enum class LoadStatus : u8 {
    Unloaded,  // Não iniciou
    Loading,   // Job em progresso (async)
    Loaded,    // Disponível para uso
    Failed     // Erro ao carregar (path inválido, arquivo corrompido)
};

// ============================================================================
// @brief  Handle tipado para um asset.
//
//  Não é null — sempre aponta para uma entrada no registry.
//  Use isReady() para saber se o asset foi carregado.
//  Use wait() para bloquear até o carregamento completar.
// ============================================================================
template<typename T>
class AssetHandle {
public:
    AssetHandle() = default;
    explicit AssetHandle(u32 id, AssetManager* mgr);

    T*   get()       const;          // nullptr se não carregado ainda
    bool isReady()   const;          // true se LoadStatus::Loaded
    bool hasFailed() const;          // true se LoadStatus::Failed
    f32  progress()  const;          // 0.0 a 1.0 (async progress)
    T*   wait()      const;          // bloqueia até Loaded ou Failed

    explicit operator bool()  const { return isReady(); }
    T*       operator->()     const { return get(); }
    T&       operator*()      const { return *get(); }

private:
    u32          m_id  = u32_max;
    AssetManager* m_mgr = nullptr;
};

// Alias convenientes
using TextureHandle   = AssetHandle<RHI::Texture>;
using AudioHandle     = AssetHandle<AudioClip>;
using ShaderHandle    = AssetHandle<RHI::Shader>;

// ============================================================================
// @brief  Gerenciador de assets.
//
//  - Cache em memória com LRU eviction quando >maxCacheBytes
//  - Async loading via Job System (Background priority)
//  - Hot-reload: file watcher, recarrega automaticamente em dev
//  - Reference counting: asset descarregado quando handle count → 0
// ============================================================================
class AssetManager {
public:
    AssetManager(const char* basePath, u64 maxCacheMB = 256);
    ~AssetManager();

    // ── Loading ────────────────────────────────────────────────
    // Async: retorna imediatamente, carrega em background
    template<typename T>
    AssetHandle<T> loadAsync(const char* path);

    // Sync: bloqueia até carregado (use somente no init)
    template<typename T>
    T* loadSync(const char* path);

    // ── Cache management ───────────────────────────────────────
    void unload(u32 assetId);
    void collectGarbage();  // descarrega assets sem referências
    void preload(const char* manifestPath);  // carrega lista de assets

    // ── Hot-reload (dev only) ──────────────────────────────────
    void enableHotReload(bool enabled);
    void pollFileChanges();  // chama no endFrame() em dev builds

    // ── Stats ──────────────────────────────────────────────────
    struct CacheStats {
        u64 totalCachedBytes;
        u64 maxCacheBytes;
        u32 textureCount;
        u32 audioCount;
        u32 pendingJobs;
        f32 cacheHitRate;
    };
    CacheStats stats() const;

private:
    void loadSyncInternal(u32 id, AssetType type);
    u64  hashPath(const char* path) const;
    void evictLRU();

    const char* m_basePath;
    u64          m_maxCacheBytes;

    HashMap<u64, u32>   m_pathToId;       // path hash → asset id
    HashMap<u32, Asset> m_assets;         // id → asset data
    HashMap<u32, u32>   m_refCounts;      // id → ref count

    bool m_hotReloadEnabled = false;
    // File watcher implementation (platform-specific)
};

}  // namespace Caffeine::Assets
```

---

## Componente ECS para Assets

```cpp
namespace Caffeine::Components {

// ============================================================================
// @brief  Referência a asset em uma entidade.
//
//  Ex: entidade com TextureRef será automaticamente carregada pelo AssetSystem.
// ============================================================================
template<typename T>
struct AssetRef {
    FixedString<128>         path;
    Caffeine::Assets::AssetHandle<T> handle;
    bool autoLoad = true;
};

using TextureRef   = AssetRef<Caffeine::RHI::Texture>;
using AudioClipRef = AssetRef<AudioClip>;

}  // namespace Caffeine::Components
```

---

## "The Loading Chain" — Fluxo de Carregamento

```
Game code:
  auto tex = assets.loadAsync<Texture>("textures/hero.caf");

    └── 1. FileSystem::open("assets/textures/hero.caf")
             └── Valida magic number: 0xCAFECAFE

        2. BlobLoader::load()
             └── JobSystem.schedule(Background priority)
                   └── LinearAllocator::alloc(dataSize)
                       └── fread(file, ptr, size)

        3. AssetResolver::cast(type == Texture)
             └── SDL_GPUUploadTexture(device, pixels)
                   └── handle.status = Loaded

    └── Game checks: if (tex.isReady()) { sprite.textureId = tex->id; }
```

---

## Hot-Reload (RF3.9)

```
FileWatcher → modifica "textures/hero.png"
                │
                ▼
            AssetManager.pollFileChanges()
                │
                ▼
            Re-encode → novo .caf → upload à GPU
                │
                ▼
            Todos os AssetHandle<Texture> apontando para "hero.png"
            automaticamente apontam para a nova textura
```

**Suportado em dev builds:** `#if CF_DEV_MODE`

---

## Exemplos de Uso

```cpp
// ── Init ──────────────────────────────────────────────────────
Caffeine::Assets::AssetManager assets("assets/");
assets.enableHotReload(true);

// ── Async loading (não bloqueia) ──────────────────────────────
auto heroTex  = assets.loadAsync<Texture>("textures/hero.caf");
auto bgMusic  = assets.loadAsync<AudioClip>("audio/level1.caf");
auto fontData = assets.loadAsync<Font>("fonts/pixel.caf");

// ── Verificação não-bloqueante ────────────────────────────────
if (heroTex.isReady()) {
    world.add<TextureRef>(player, { .handle = heroTex });
}

// ── Loading screen: progresso ────────────────────────────────
float progress = (heroTex.progress() + bgMusic.progress()) / 2.0f;
progressBar.setValue(progress * 100.0f);

// ── Sync no init (aceitável no boot) ─────────────────────────
Texture* uiAtlas = assets.loadSync<Texture>("textures/ui.caf");

// ── Hot-reload no game loop ───────────────────────────────────
#if CF_DEV_MODE
    assets.pollFileChanges();
#endif
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| `AssetHandle<T>` tipado | Erro de tipo pego em compile time |
| LRU eviction | Cache não cresce indefinidamente |
| Background job priority | Loading nunca atrasa física/render |
| Hot-reload opt-in | Zero overhead em release builds |
| Reference counting | Assets descarregados automaticamente quando não usados |

---

## Critério de Aceitação

- [ ] 100 texturas 512×512 carregadas async em < 2s
- [ ] 200MB em background sem freeze do game loop
- [ ] Hot-reload: textura atualizada no mesmo frame em que o arquivo muda
- [ ] Cache hit rate > 90% em jogo normal
- [ ] `collectGarbage()` descarrega assets sem referências

---

## Dependências

- **Upstream:** [Job System](../fase2/job-system.md), [Formato .caf](caf-format.md), [RHI](rhi.md)
- **Downstream:** [Batch Renderer](batch-renderer.md), [Fase 4 — Audio](../fase4/audio.md), [Fase 4 — Animation](../fase4/animation.md), [Fase 5 — Mesh Loading](../fase5/mesh-loading.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §7 Asset Manager
- [`docs/fase3/caf-format.md`](caf-format.md) — formato binário dos assets
- [`docs/fase2/job-system.md`](../fase2/job-system.md) — Background jobs para loading
