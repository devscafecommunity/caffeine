# 🎬 Game Loop

> **Fase:** 2 — O Pulso e a Concorrência  
> **Namespace:** `Caffeine::Core`  
> **Arquivo:** `src/core/GameLoop.hpp`  
> **Status:** 📅 Planejado  
> **RFs:** RF2.7, RF2.8

---

## Visão Geral

O Game Loop é o coração da engine — o ciclo que corre do início ao fim do jogo. A Caffeine implementa o padrão **Fixed Timestep com Acumulador**: desvincula a lógica de física (que precisa de tempo consistente) da renderização (que pode variar livremente).

**Padrão:** `accumulator += dt` → `while (accum >= fixedDt) update(fixedDt)` → `render(alpha)`

Inspirado em [Game Programming Patterns — Game Loop](https://gameprogrammingpatterns.com/game-loop.html).

---

## API Planejada

```cpp
namespace Caffeine::Core {

// ============================================================================
// @brief  Estados possíveis do game loop.
// ============================================================================
enum class GameState : u8 {
    Init,      // Inicializando recursos
    Running,   // Loop ativo
    Paused,    // Lógica pausada, render continua
    Shutdown   // Limpeza e encerramento
};

// ============================================================================
// @brief  Configuração do game loop.
// ============================================================================
struct GameLoopConfig {
    f64  fixedDeltaTime = 1.0 / 60.0;  // 60 updates lógicos/segundo
    f64  maxFrameTime   = 0.25;          // Clamp — evita "spiral of death"
    u32  targetFPS      = 0;             // 0 = unlimited
    bool vsync          = true;
    bool interpolation  = true;          // Interpolar posições para render
};

// ============================================================================
// @brief  Game loop principal com fixed timestep + interpolação.
//
//  Ciclo por frame:
//
//  1. processInput()       — SDL_PollEvent, alimenta Input System
//  2. accumulator += dt     — Acumula tempo real desde o último frame
//  3. while (accum >= fdt)  — Update em passos fixos:
//       - EventBus.dispatch()
//       - World.update(fdt)   (Fase 4)
//       - accumulator -= fdt
//  4. alpha = accum / fdt   — Fração do próximo step para interpolação
//  5. render(alpha)         — Render com posições interpoladas
//  6. endFrame()            — Stats, debug draw, profiler
// ============================================================================
class GameLoop {
public:
    explicit GameLoop(const GameLoopConfig& config = {});

    // Inicializa o timer e subsistemas
    void init();

    // Avança um frame completo (chame a cada iteração do while(running))
    void tick(f64 deltaTime);

    // Controle de estado
    void pause();
    void resume();
    void shutdown();

    // Consulta de estado
    GameState state()            const { return m_state; }
    f64       elapsedTime()      const;
    f64       interpolationAlpha() const { return m_alpha; }
    u64       frameCount()       const { return m_frameCount; }

    // Callbacks do jogo (injetar lógica aqui)
    std::function<void(f64 dt)>   onFixedUpdate;
    std::function<void(f64 alpha)> onRender;
    std::function<void()>          onBeginFrame;
    std::function<void()>          onEndFrame;

private:
    void processInput();
    void processFixedUpdate(f64 dt);
    void processInterpolation(f64 alpha);

    GameLoopConfig m_config;
    GameState      m_state       = GameState::Init;
    f64             m_accumulator = 0.0;
    f64             m_elapsedTime = 0.0;
    f64             m_alpha       = 0.0;
    u64             m_frameCount  = 0;
};

}  // namespace Caffeine::Core
```

---

## Fluxo Detalhado por Frame

```
┌──────────────────────────────────────────────────────────────┐
│                    FRAME N                                   │
│                                                              │
│  beginFrame()                                                │
│    └── limpa transients, inicia GPU frame                    │
│                                                              │
│  processInput()                                              │
│    └── SDL_PollEvent → InputManager.beginFrame()             │
│                                                              │
│  accumulator += realDeltaTime                                │
│  accumulator = min(accum, maxFrameTime)   ← spiral guard    │
│                                                              │
│  while (accumulator >= fixedDt):                             │
│    ├── EventBus.dispatch()                                   │
│    ├── World.update(fixedDt)    ← ECS systems (Fase 4)       │
│    │     ├── PhysicsSystem (priority 100)                    │
│    │     ├── MovementSystem (priority 150)                   │
│    │     └── AnimationSystem (priority 200)                  │
│    └── accumulator -= fixedDt                                │
│                                                              │
│  alpha = accumulator / fixedDt                               │
│  render(alpha)                  ← BatchRenderer (Fase 3)     │
│                                                              │
│  endFrame()                                                  │
│    └── debug stats, Profiler.report()                        │
└──────────────────────────────────────────────────────────────┘
```

---

## Spiral of Death Prevention

Se o update ficar para trás (sistema lento), o acumulador cresce indefinidamente levando a um loop infinito de updates:

```cpp
// Clamp para no máximo 250ms — pula frames se necessário
m_accumulator = min(m_accumulator, m_config.maxFrameTime);
```

**Efeito:** Em hardware lento, o jogo parece "slow motion" temporariamente, mas não trava.

---

## Interpolação de Renderização

Sem interpolação, entidades "teletransportam" entre posições a cada fixed step:

```cpp
// Com interpolação:
Vec2 renderPos = lerp(entity.prevPos, entity.currPos, alpha);
// alpha = 0.0 → posição no início do step atual
// alpha = 1.0 → posição no fim do step atual
```

**Requisito:** Sistemas de física devem salvar `prevPos` antes de atualizar `currPos`.

---

## Exemplos de Uso

```cpp
// ── Setup básico ──────────────────────────────────────────────
Caffeine::Core::GameLoopConfig cfg;
cfg.fixedDeltaTime = 1.0 / 60.0;
cfg.vsync          = true;
cfg.interpolation  = true;

Caffeine::Core::GameLoop loop(cfg);

loop.onFixedUpdate = [&](f64 dt) {
    world.update(dt);
    eventBus.dispatch();
};

loop.onRender = [&](f64 alpha) {
    batchRenderer.beginFrame();
    renderSystems.run(alpha);
    batchRenderer.endFrame(cmd);
};

loop.init();

// ── Main loop ─────────────────────────────────────────────────
Caffeine::Core::Timer timer;
while (loop.state() != GameState::Shutdown) {
    f64 dt = timer.tick();
    loop.tick(dt);
}
```

---

## Estados do Loop

```
 Init ──► Running ◄──► Paused
                 │
                 ▼
              Shutdown
```

| Estado | Comportamento |
|--------|-------------|
| `Init` | Carrega recursos, inicializa subsistemas |
| `Running` | Loop normal: input → update → render |
| `Paused` | Sem fixed update, render continua (UI, menu) |
| `Shutdown` | Cleanup, flush logs, exit code |

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Fixed timestep para lógica | Física determinística, replay, multiplayer |
| Acumulador com clamp | Evita "spiral of death" em hardware lento |
| `vsync` configurável | Permite benchmark sem vsync |
| `interpolation` configurável | Debug mais fácil sem interpolação |
| Callbacks `onFixedUpdate`/`onRender` | Loop não acopla a subsistemas específicos |

---

## Critério de Aceitação

- [ ] 3600 frames com `fixedDt` acumulado = 60.0 ± 0.001 (sem drift)
- [ ] Spiral of death não ocorre: FPS pode cair mas loop não trava
- [ ] `state()` transiciona corretamente Init → Running → Paused → Running → Shutdown
- [ ] `interpolationAlpha()` está sempre em [0.0, 1.0)

---

## Dependências

- **Upstream:** [Timer](timer.md), `SDL3::SDL_PollEvent`
- **Downstream:** [Fase 3 — BatchRenderer](../fase3/batch-renderer.md), [Fase 4 — ECS World](../fase4/ecs.md), [Input](input.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §1 Game Loop
- [`docs/fase2/timer.md`](timer.md) — Timer usado internamente
- [Game Programming Patterns — Game Loop](https://gameprogrammingpatterns.com/game-loop.html)
- [Fixed Timestep Demo](https://github.com/jakubtomsu/fixed-timestep-demo)
