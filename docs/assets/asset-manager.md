# 📦 Asset Manager

> **Fase:** 3 — O Olho da Engine  
> **Namespace:** `Caffeine::Assets`  
> **Arquivos:** `src/assets/AssetManager.hpp`, `src/assets/AssetManager.cpp`, `src/assets/AssetHandle.hpp`, `src/assets/AssetTypes.hpp`  
> **Status:** ✅ Implementado  
> **RFs:** RF3.8, RF3.9

---

## Visão Geral

O Asset Manager carrega, faz cache e fornece assets (texturas, áudio, shaders) de forma transparente ao jogo. Suporta **async loading** via [Job System](../concurrency/job-system.md) e **hot-reload** em builds debug.

**Filosofia:** O código do jogo pede um asset pelo caminho; o Asset Manager retorna um `AssetHandle<T>`. O jogo nunca vê raw bytes — só `AssetHandle<Texture>`.

**Carregamento zero-copy:** Os assets são carregados via `BlobLoader` que faz uma única `fread()` para um `LinearAllocator`. As structs `Texture`, `AudioClip` e `ShaderBlob` são *views* sobre esse buffer — sem cópia, sem deserialização.

---

## Arquitetura

```
Game code:
  auto tex = assets.loadAsync<Texture>("textures/hero.caf");

    └── 1. acquireOrCreate("textures/hero.caf", AssetType::Texture)
             ├── Já em cache? → retorna ID existente, incrementa cacheHit
             └── Novo? → cria AssetEntry, adiciona ao pathIndex

        2. scheduleLoad(id)
             ├── status = Loading
             ├── JobSystem::scheduleData(Background priority)
             │     └── loadInternal(id):
             │           ├── BlobLoader::load(path, allocator)
             │           ├── resolveEntry() → Texture/Audio/Shader
             │           └── status = Loaded
             └── (se jobs == nullptr: loadInternal roda inline)

        3. handle.isReady() → true
           handle.get()     → ponteiro const T* para o asset resolvido
```

---

## API Pública

```cpp
namespace Caffeine::Assets {

// ============================================================================
// LoadStatus — estado de carregamento de um asset
// ============================================================================
enum class LoadStatus : u8 {
    Unloaded,
    Loading,
    Loaded,
    Failed
};

// ============================================================================
// Tipos de asset runtime (zero-copy views)
// ============================================================================
struct Texture {
    u32       width;
    u32       height;
    u32       format;         // 0 = RGBA8
    u32       mipLevels;
    const u8* pixels;
    u64       pixelDataSize;
};

struct AudioClip {
    u32       sampleRate;
    u16       channels;
    u16       bitsPerSample;
    u32       sampleCount;
    const u8* pcmData;
    u64       pcmDataSize;
};

struct ShaderBlob {
    u32       stage;          // 0=vertex, 1=fragment, 2=compute
    const u8* bytecode;
    u64       bytecodeSize;
};

// ============================================================================
// CacheStats — snapshot do estado do cache
// ============================================================================
struct CacheStats {
    u64 totalCachedBytes;
    u64 maxCacheBytes;
    u32 textureCount;
    u32 audioCount;
    u32 pendingJobs;
    f32 cacheHitRate;
};

// ============================================================================
// AssetHandle<T> — handle RAII com ref counting
//
//  - Copiar um handle incrementa o ref count do asset.
//  - O destructor decrementa o ref count.
//  - collectGarbage() descarrega assets com refCount == 0.
// ============================================================================
template<typename T>
class AssetHandle {
public:
    AssetHandle();
    AssetHandle(AssetManager* mgr, u32 id);
    ~AssetHandle();

    AssetHandle(const AssetHandle& o);
    AssetHandle& operator=(const AssetHandle& o);

    AssetHandle(AssetHandle&& o) noexcept;
    AssetHandle& operator=(AssetHandle&& o) noexcept;

    bool     isValid()  const;   // handle aponta para uma entrada válida
    bool     isReady()  const;   // asset foi carregado (status == Loaded)
    const T* get()      const;   // nullptr se não estiver pronto ainda

    explicit operator bool() const;

    u32 id() const;
};

// ============================================================================
// AssetManager — gerenciador central
// ============================================================================
class AssetManager {
public:
    explicit AssetManager(Threading::JobSystem* jobs,
                          const char*           basePath,
                          u64                   maxCacheMB = 256);
    ~AssetManager();

    // ── Loading ────────────────────────────────────────────────
    // Async: retorna imediatamente, carrega em background
    template<typename T>
    AssetHandle<T> loadAsync(const char* path);

    // Sync: bloqueia até carregado (use somente no init)
    template<typename T>
    AssetHandle<T> loadSync(const char* path);

    // ── Gerenciamento de cache ─────────────────────────────────
    void       collectGarbage();   // descarrega assets sem referências
    CacheStats cacheStats() const; // estatísticas do cache

    // ── Frame tick ─────────────────────────────────────────────
    void tick(u64 frameIndex = 0); // atualiza frame index + hot-reload

private:
    // incRef / decRef / getStatus — usados por AssetHandle
    void       incRef(u32 id);
    void       decRef(u32 id);
    LoadStatus getStatus(u32 id) const;

    template<typename T>
    const T* getResolved(u32 id) const; // retorna Texture/Audio/Shader resolvido

    // ... (membros internos)
};

} // namespace Caffeine::Assets
```

---

## Estrutura Interna

Cada asset carregado é representado por um `AssetEntry`:

```
AssetEntry
├── path              : std::string (caminho completo no disco)
├── cafType           : AssetType
├── status            : std::atomic<LoadStatus>
├── allocator         : unique_ptr<LinearAllocator> (dono do buffer)
├── header            : const CafHeader*
├── metadata          : const void*      (TextureMetadata, AudioMetadata, etc.)
├── payload           : const void*      (pixels brutos, PCM, bytecode)
├── resolved          : ResolvedData { Texture, AudioClip, ShaderBlob }
├── refCount          : std::atomic<u32>
├── sizeBytes         : u64
└── lastAccessFrame   : u64
```

O `LinearAllocator` aloca um buffer de `fileSize + 64` bytes. O `BlobLoader` faz a `fread()` única e ajusta os ponteiros `header`, `metadata` e `payload` dentro desse buffer. As structs `resolved` são preenchidas com os valores extraídos do metadata + payload.

---

## Hot-Reload (RF3.9)

Em builds **debug** (`#ifdef CF_DEBUG`):

1. `tick()` chama `checkHotReload()` a cada frame
2. Para cada asset carregado, compara `last_write_time` do arquivo no disco
3. Se mudou: descarrega o asset antigo e recarrega via `loadInternal`
4. `AssetHandle<T>::get()` automaticamente aponta para a nova versão

**Zero overhead em release:** O hot-reload é compilado condicionalmente — não existe em builds de produção.

---

## Exemplos de Uso

```cpp
#include <Caffeine.hpp>

using namespace Caffeine;
using namespace Caffeine::Assets;

// ── Init ──────────────────────────────────────────────────────
Threading::JobSystem jobs(4);
AssetManager assets(&jobs, "assets/", 256); // max 256 MB cache

// ── Async loading (não bloqueia) ──────────────────────────────
auto heroTex = assets.loadAsync<Texture>("textures/hero.caf");
auto bgMusic = assets.loadAsync<AudioClip>("audio/level1.caf");

// ── Game loop ─────────────────────────────────────────────────
while (running) {
    assets.tick(frameCounter);

    if (heroTex.isReady()) {
        const Texture* t = heroTex.get();
        // t->width, t->height, t->pixels, t->pixelDataSize ...
    }

    // ── Coleta lixo a cada 60 frames ──────────────────────────
    if (frameCounter % 60 == 0) {
        assets.collectGarbage();
    }

    ++frameCounter;
}

// ── Sync no init (aceitável no boot) ─────────────────────────
auto uiAtlas = assets.loadSync<Texture>("textures/ui.caf");
const Texture* atlas = uiAtlas.get();  // pronto imediatamente
```

---

## Testes

12 casos de teste em `tests/test_assetmanager.cpp`:

| Teste | O que verifica |
|-------|---------------|
| `AssetManager - construction with null JobSystem` | Construtor sem jobs não crasha |
| `AssetManager - loadSync Texture` | Textura carregada, campos width/height/pixels corretos |
| `AssetManager - loadSync AudioClip` | AudioClip carregado, campos sampleRate/channels corretos |
| `AssetManager - loadSync ShaderBlob` | ShaderBlob carregado, campos stage/bytecode corretos |
| `AssetManager - second load is cache hit` | Cache hit rate > 0 no segundo load |
| `AssetManager - cacheStats counts textures and audio` | textureCount/audioCount corretos |
| `AssetManager - collectGarbage unloads unreferenced assets` | Asset descarregado após handle sair de escopo |
| `AssetManager - collectGarbage does not unload referenced` | Asset mantido enquanto handle existe |
| `AssetManager - handle copy increments ref` | Cópia de handle mantém asset vivo |
| `AssetManager - loadAsync becomes ready` | Async loading via JobSystem completa |
| `AssetManager - failed load returns Failed status` | Path inexistente retorna Failed |
| `AssetManager - tick advances frame index` | tick() não crasha |

**Total:** 46 assertions.

---

## Dependências

| Dependência | Uso |
|-------------|-----|
| [`Caffeine::Threading::JobSystem`](../concurrency/job-system.md) | Background jobs para async loading |
| [`Caffeine::IO::BlobLoader`](caf-format.md) | Leitura zero-copy de arquivos .caf |
| [`Caffeine::IO::CafTypes`](caf-format.md) | CafHeader, TextureMetadata, AudioMetadata, ShaderMetadata |
| [`Caffeine::LinearAllocator`](../memory/allocators.md) | Alocação linear para o buffer do asset |
| `std::filesystem` (CF_DEBUG) | Verificação de hot-reload via last_write_time |

---

## Decisões de Design

| Decisão | Justificativa |
|---------|--------------|
| `AssetHandle<T>` tipado | Erro de tipo pego em compile time |
| `loadAsync` aceita `JobSystem*` nullable | Fallback sync quando não há JobSystem (útil em testes) |
| Zero-copy resolved views | Sem overhead de conversão; structs são preenchidas no load |
| `collectGarbage()` explícito | Evita pausas inesperadas; jogo controla quando descarregar |
| `tick()` como ponto único | Unifica frame counter e hot-reload num só método |
| Hot-reload só em CF_DEBUG | Zero custo em release |
| `std::unordered_map` + `std::vector` | Simplicidade sobre performance de cache; HashMap customizado seria over-engineering nesta fase |
| `std::mutex` (não lock-free) | Contenção baixa; simplicidade de implementação |

---

## Critério de Aceitação

- [x] `loadAsync` retorna imediatamente, carrega em background via JobSystem
- [x] `loadSync` bloqueia até o carregamento completar
- [x] Cache com ref counting: handle copy incrementa, destructor decrementa
- [x] `collectGarbage()` descarrega assets sem referências
- [x] `cacheStats()` retorna contagem por tipo, bytes totais, hit rate
- [x] Hot-reload em debug builds via `tick()` → `checkHotReload()`
- [x] Zero-copy: `BlobLoader` → `LinearAllocator` → resolved fields
- [x] Tipos suportados: Texture, AudioClip, ShaderBlob

---

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Assets & Pipeline]() | Asset Manager como runtime do pipeline |
| [Concorrência & Runtime]() | Async loading via Job System |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §7 Asset Manager
- [`assets/caf-format.md`](caf-format.md) — Formato binário .caf
- [`concurrency/job-system.md`](../concurrency/job-system.md) — Background jobs para loading
- [Índice de Tópicos Transversais]()
- [Issue #26](https://github.com/devscafecommunity/caffeine/issues/26) — Asset Manager task
