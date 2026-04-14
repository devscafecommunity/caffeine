# 🎵 Audio System

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::Audio`  
> **Arquivo:** `src/audio/AudioSystem.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF4.8

---

## Visão Geral

O Audio System usa `SDL_AudioStream` para streaming de música e pré-carregamento de SFX. Suporta posicionamento espacial 2D (pan + volume por distância).

**Modelo:**
- **SFX** — carregados inteiros em `PoolAllocator` (buffer fixo por canal)
- **Música** — streaming em chunks de 64KB para economizar memória
- **Spatial** — pan e volume calculados por distância listener ↔ emitter

---

## API Planejada

```cpp
namespace Caffeine::Audio {

// ============================================================================
// @brief  Clipe de áudio (dados brutos em memória).
// ============================================================================
struct AudioClip {
    const u8* data        = nullptr;
    u64       size        = 0;
    u32       sampleRate  = 44100;
    u16       channels    = 2;
    u16       bitsPerSample = 16;
    f32       duration    = 0.0f;  // segundos
};

// ============================================================================
// @brief  Source de áudio — instância de som tocando.
// ============================================================================
class AudioSource {
public:
    void play();
    void pause();
    void stop();
    void seek(f32 seconds);

    void setVolume(f32 v);      // 0.0 a 1.0
    void setPan(f32 pan);       // -1.0 (left) a +1.0 (right)
    void setPitch(f32 pitch);   // 0.5 (half speed) a 2.0 (double)
    void setLoop(bool loop);

    bool isPlaying()     const;
    bool isPaused()      const;
    f32  currentTime()   const;
    f32  duration()      const { return m_clip ? m_clip->duration : 0; }

private:
    SDL_AudioStream* m_stream = nullptr;
    AudioClip*        m_clip   = nullptr;
    f32               m_volume = 1.0f;
    f32               m_pan    = 0.0f;
    f32               m_pitch  = 1.0f;
    bool              m_loop   = false;
};

// ============================================================================
// @brief  Sistema de áudio.
//
//  Channels:
//  - sfxChannelCount   = 32 (SFX simultâneos)
//  - musicChannelCount = 2  (crossfade entre músicas)
//
//  Memory:
//  - SFX pool: PoolAllocator (tamanho fixo por clip, zero alloc em runtime)
//  - Música: streaming (64KB chunks)
// ============================================================================
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool init(u32 sfxChannelCount = 32, u32 musicChannelCount = 2);
    void shutdown();

    // ── SFX ────────────────────────────────────────────────────
    AudioClip*   loadClip(const char* path);
    AudioSource* playSFX(AudioClip* clip,
                         f32 volume = 1.0f,
                         f32 pan    = 0.0f);
    AudioSource* playSFXAt(AudioClip* clip, Vec2 worldPos,
                            f32 maxDistance = 500.0f);

    // ── Música ─────────────────────────────────────────────────
    void playMusic(const char* path,
                   f32 volume = 0.8f,
                   bool loop  = true);
    void stopMusic(f32 fadeOutSecs = 0.5f);
    void fadeMusic(f32 fadeOutSecs,
                   const char* nextPath,
                   f32 fadeInSecs = 0.5f);
    void setMusicVolume(f32 volume);

    // ── Spatial audio ──────────────────────────────────────────
    void setListenerPosition(Vec2 pos, Vec2 forward = {1, 0});
    void setSourcePosition(AudioSource* source, Vec2 worldPos,
                           f32 maxDistance = 500.0f);

    // ── Master controls ────────────────────────────────────────
    void setMasterVolume(f32 volume);
    void setMasterPaused(bool paused);

    // ── Update (processa fade, libera sources finalizados) ─────
    void update(f64 dt);

    struct Stats {
        u32 activeSFX;
        u32 sfxPoolUsed;
        u32 sfxPoolTotal;
        bool musicPlaying;
        f32  musicVolume;
    };
    Stats stats() const;

private:
    SDL_AudioDeviceID              m_device;
    std::vector<AudioSource>       m_sfxPool;
    AudioSource*                    m_musicCurrent = nullptr;
    AudioSource*                    m_musicNext    = nullptr;  // crossfade
    HashMap<u64, AudioClip>         m_clipCache;
    Vec2                            m_listenerPos   = {0, 0};
    f32                             m_masterVolume  = 1.0f;
    bool                            m_masterPaused  = false;
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
// ── Init ──────────────────────────────────────────────────────
Caffeine::Audio::AudioSystem audio;
audio.init(32, 2);

// ── Carregar clips ────────────────────────────────────────────
auto* jumpSFX    = audio.loadClip("audio/jump.caf");
auto* impactSFX  = audio.loadClip("audio/impact.caf");

// ── Tocar SFX simples ─────────────────────────────────────────
audio.playSFX(jumpSFX, 0.8f);

// ── Tocar SFX com posição ─────────────────────────────────────
audio.playSFXAt(impactSFX, explosionPos, 300.0f);

// ── Música com crossfade ──────────────────────────────────────
audio.playMusic("music/level1.caf");
// ... ao chegar em boss:
audio.fadeMusic(1.0f, "music/boss.caf", 0.5f);

// ── Listener segue câmera ─────────────────────────────────────
audio.setListenerPosition(camera.position());

// ── Responder a eventos ───────────────────────────────────────
eventBus.subscribe<OnCollision2D>([&](const OnCollision2D& e) {
    audio.playSFXAt(impactSFX, e.contactPoint, 200.0f);
});

// ── Update no Game Loop ───────────────────────────────────────
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

- **Upstream:** [Asset Manager](../fase3/asset-manager.md), `SDL3::SDL_AudioStream`
- **Downstream:** [Event Bus](events.md) (responde a OnCollision2D, OnDeath)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §11 Audio System
- [`docs/fase4/events.md`](events.md) — Audio reage a eventos de gameplay
- [SDL3 Audio Documentation](https://wiki.libsdl.org/SDL3/CategoryAudio)
