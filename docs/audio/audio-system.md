# 🎵 Audio System

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::Audio`  
> **Arquivo:** `src/audio/AudioSystem.hpp`  
> **Status:** ✅ Implementado  
> **RF:** RF4.8

---

## Visão Geral

O Audio System usa `SDL_AudioStream` para streaming de música e pré-carregamento de SFX. Suporta posicionamento espacial 2D (pan + volume por distância).

**Modelo:**
- **SFX** — carregados inteiros em `PoolAllocator` (buffer fixo por canal)
- **Música** — streaming em chunks de 64KB para economizar memória
- **Spatial** — pan e volume calculados por distância listener ↔ emitter

---

## API

```cpp
namespace Caffeine::Audio {

struct AudioClip {
    const u8* data          = nullptr;
    u64       size          = 0;
    u32       sampleRate    = 44100;
    u16       channels      = 2;
    u16       bitsPerSample = 16;
    f32       duration      = 0.0f;
};

struct AudioEmitter {
    FixedString<128> clipPath;
    f32              volume      = 1.0f;
    f32              maxDistance = 500.0f;
    bool             loop        = false;
    bool             playOnSpawn = true;
    bool             spatial     = true;
};

class AudioSource {
public:
    void play();
    void pause();
    void stop();
    void seek(f32 seconds);

    void setVolume(f32 v);
    void setPan(f32 pan);
    void setPitch(f32 pitch);
    void setLoop(bool loop);

    bool isPlaying()   const;
    bool isPaused()    const;
    bool isFree()      const;
    f32  currentTime() const;
    f32  duration()    const;
    f32  volume()      const;
    f32  pan()         const;
    f32  pitch()       const;
    bool loop()        const;
};

class AudioSystem {
public:
    bool init(u32 sfxChannelCount = 32, u32 musicChannelCount = 2);
    void shutdown();

    AudioClip*   registerClip(const u8* data, u64 size,
                               u32 sampleRate = 44100, u16 channels = 2,
                               u16 bitsPerSample = 16, f32 duration = 0.0f);

    AudioSource* playSFX(const AudioClip* clip,
                         f32 volume = 1.0f, f32 pan = 0.0f);
    AudioSource* playSFXAt(const AudioClip* clip, Vec2 worldPos,
                            f32 maxDistance = 500.0f);

    void setListenerPosition(Vec2 pos, Vec2 forward = {1.0f, 0.0f});
    void setSourcePosition(AudioSource* source, Vec2 worldPos,
                           f32 maxDistance = 500.0f);

    void setMasterVolume(f32 volume);
    void setMasterPaused(bool paused);

    void update(f64 dt);

    bool isInitialized()    const;
    f32  masterVolume()     const;
    bool masterPaused()     const;
    Vec2 listenerPosition() const;

    struct Stats {
        u32  activeSFX;
        u32  sfxPoolUsed;
        u32  sfxPoolTotal;
        bool musicPlaying;
        f32  musicVolume;
    };
    Stats stats() const;
};

}  // namespace Caffeine::Audio
```

---

## Componente ECS para Audio

```cpp
namespace Caffeine::Components {

// ============================================================================
// @brief  Emitter de som no mundo 2D.
//
//  Usado pelo AudioSystem para calcular pan/volume por distância ao listener.
// ============================================================================
struct AudioEmitter {
    FixedString<128> clipPath;      // path do SFX
    f32              volume       = 1.0f;
    f32              maxDistance  = 500.0f;  // pixels
    bool             loop         = false;
    bool             playOnSpawn  = true;
    bool             spatial      = true;    // false = sem atenuação
};

}  // namespace Caffeine::Components
```

---

## Spatial Audio

```
Listener (Camera2D position)
    │
    │← distance →│
    │              │
    │         AudioEmitter (worldPos)
    │
volume  = clamp(1 - distance/maxDistance, 0, 1)
pan     = clamp((emitterX - listenerX) / maxDistance, -1, 1)
```

---

## Exemplos de Uso

```cpp
Caffeine::Audio::AudioSystem audio;
audio.init(32, 2);

static const u8 jumpPCM[64] = {};
auto* jumpSFX = audio.registerClip(jumpPCM, sizeof(jumpPCM), 44100, 2, 16, 0.5f);

audio.playSFX(jumpSFX, 0.8f);

audio.playSFXAt(jumpSFX, {300.0f, 150.0f}, 500.0f);

audio.setListenerPosition(camera.position());

audio.update(dt);
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Pool de sources | Zero alloc em runtime — n sources pre-alocadas |
| SFX inteiros em memória | Latência mínima — sem streaming de SFX curtos |
| Música em streaming | Arquivos grandes não consomem RAM inteiros |
| `update()` na main thread | Callbacks do SDL audio rodam em thread separada — sem race conditions |

---

## Critério de Aceitação

- [ ] 32 SFX simultâneos sem crackle/distorção
- [ ] Audio pool sem alloc em runtime (PoolAllocator)
- [ ] Spatial audio: volume/pan corretos por distância
- [ ] Crossfade suave entre duas músicas
- [ ] `update()` overhead < 0.5ms

---

## Dependências

- **Upstream:** [Asset Manager](../assets/asset-manager.md), `SDL3::SDL_AudioStream`
- **Downstream:** [Event Bus](events.md) (responde a OnCollision2D, OnDeath)

---

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Sistemas de Jogo]() | Audio como sistema ECS |
| [Assets & Pipeline]() | Audio assets carregados via Asset Manager |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §11 Audio System
- [`core/events.md`](events.md) — Audio reage a eventos de gameplay
- [SDL3 Audio Documentation](https://wiki.libsdl.org/SDL3/CategoryAudio)
- [Índice de Tópicos Transversais]()
