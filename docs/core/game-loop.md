# 🎬 Game Loop

> **Fase:** 2 — O Pulso e a Concorrência  
> **Namespace:** `Caffeine` (não `Caffeine::Core`)  
> **Arquivos:** `src/core/GameLoop.hpp`, `src/core/GameLoop.cpp`  
> **Status:** ✅ Implementado  
> **Testes:** `tests/test_gameloop.cpp`  
> **RFs:** RF2.7, RF2.8

---

## Visão Geral

O Game Loop é o coração da engine — o ciclo que corre do início ao fim do jogo. A Caffeine implementa o padrão **Fixed Timestep com Acumulador**: desvincula a lógica de física (que precisa de tempo consistente) da renderização (que pode variar livremente).

**Padrão:** `accumulator += dt` → `while (accum >= fixedDt) update(fixedDt)` → `render(alpha)`

Inspirado em [Game Programming Patterns — Game Loop](https://gameprogrammingpatterns.com/game-loop.html).

> **Responsabilidade do chamador:** o `GameLoop` não possui clock próprio e não
> faz polling de input — ele recebe `deltaTime` pronto via `tick(dt)`. Medir o
> tempo real (ex: com [`Timer`](timer.md)) e bombear `SDL_PollEvent` é tarefa do
> código que chama `tick()`, normalmente o `main` da aplicação.

---

## API Real

```cpp
namespace Caffeine {

enum class GameState : u8 {
    Init,      // tick() é ignorado; aguardando init()
    Running,   // update + render
    Paused,    // sem fixed update; render continua (UI, menu)
    Shutdown   // tick() é ignorado
};

struct GameLoopConfig {
    f64  fixedDeltaTime = 1.0 / 60.0;  // 60 updates lógicos/segundo
    f64  maxFrameTime   = 0.25;          // clamp do dt — evita "spiral of death"
    u32  targetFPS      = 0;             // 0 = unlimited (armazenado; não aplicado pelo loop)
    bool vsync          = true;          // idem — controle real é do RHI/janela
    bool interpolation  = true;          // calcula alpha; se false, alpha = 0
};

// ============================================================================
// @brief  Interface alternativa aos std::function: derive e registre via
//  setCallbacks(). Os dois mecanismos coexistem e AMBOS são chamados
//  (primeiro m_callbacks, depois os std::function).
// ============================================================================
class IGameCallbacks {
public:
    virtual ~IGameCallbacks() = default;
    virtual void onBeginFrame() {}
    virtual void onFixedUpdate(f64 dt) { (void)dt; }
    virtual void onRender(f64 alpha) { (void)alpha; }
    virtual void onEndFrame() {}
};

class GameLoop {
public:
    explicit GameLoop(const GameLoopConfig& config = {});
    ~GameLoop();

    void init();            // Init → Running (só a partir de Init)
    void tick(f64 deltaTime);  // um frame; ignorado em Init/Shutdown
    void pause();           // Running → Paused (só a partir de Running)
    void resume();          // Paused → Running (só a partir de Paused)
    void shutdown();        // Running/Paused → Shutdown

    GameState state()              const { return m_state; }
    f64       elapsedTime()        const { return m_elapsedTime; }
    f64       interpolationAlpha() const { return m_alpha; }
    u64       frameCount()         const { return m_frameCount; }
    f64       accumulator()        const { return m_accumulator; }

    void setCallbacks(IGameCallbacks* callbacks);
    const GameLoopConfig& config() const { return m_config; }

    // Hooks livres (chamados DEPOIS do IGameCallbacks, se ambos existirem)
    std::function<void(f64 dt)>    onFixedUpdate;
    std::function<void(f64 alpha)> onRender;
    std::function<void()>          onBeginFrame;
    std::function<void()>          onEndFrame;

private:
    void processFixedUpdate(f64 dt);

    GameLoopConfig  m_config;
    IGameCallbacks* m_callbacks   = nullptr;
    GameState       m_state       = GameState::Init;
    f64             m_accumulator = 0.0;
    f64             m_elapsedTime = 0.0;
    f64             m_alpha       = 0.0;
    u64             m_frameCount  = 0;
};

}  // namespace Caffeine
```

---

## Fluxo Detalhado por Frame (`tick(dt)`)

```
tick() ignorado se state ∈ {Init, Shutdown}

  onBeginFrame()
  dt = min(dt, maxFrameTime)        ← spiral guard
  elapsedTime += dt                  ← avança MESMO quando Paused
  frameCount++

  se Running:
    accumulator += dt
    while (accumulator >= fixedDeltaTime):
        onFixedUpdate(fixedDeltaTime)
        accumulator -= fixedDeltaTime
    alpha = interpolation ? accumulator / fixedDeltaTime : 0.0

  onRender(alpha)                    ← chamado também quando Paused
  onEndFrame()
```

Notas de comportamento (confirmadas pelo código e pelos testes):
- **Pausado:** `onRender`/`onBeginFrame`/`onEndFrame` continuam; `accumulator`
  fica congelado e os steps pendentes disparam ao dar `resume()`.
- **`targetFPS`/`vsync`:** apenas armazenados — o loop não dorme nem sincroniza;
  cabe ao chamador/RHI aplicar throttling e vsync.
- **`shutdown()`** a partir de `Init` é ignorado (sem transição).

---

## Spiral of Death Prevention

```cpp
deltaTime = std::min(deltaTime, m_config.maxFrameTime);  // default 250ms
```

**Efeito:** em hardware lento, o jogo entra em "slow motion" temporário em vez de travar num loop infinito de catch-up.

---

## Interpolação de Renderização

Sem interpolação, entidades "teletransportam" entre posições a cada fixed step:

```cpp
Vec2 renderPos = lerp(entity.prevPos, entity.currPos, alpha);
// alpha = 0.0 → início do step atual
// alpha = 1.0 → fim do step atual
```

**Requisito:** sistemas de física devem salvar `prevPos` antes de atualizar `currPos`. Com `interpolation = false`, `alpha` é sempre `0.0`.

---

## Exemplos de Uso

```cpp
// ── Setup básico ──────────────────────────────────────────────
Caffeine::GameLoopConfig cfg;
cfg.fixedDeltaTime = 1.0 / 60.0;
cfg.interpolation  = true;

Caffeine::GameLoop loop(cfg);

loop.onFixedUpdate = [&](f64 dt) {
    world.update(dt);
    eventBus.dispatch();
};

loop.onRender = [&](f64 alpha) {
    renderer.render(alpha);
};

loop.init();

// ── Main loop (o chamador mede o tempo e bombeia input) ───────
Caffeine::Core::Timer timer;
timer.start();
while (loop.state() != Caffeine::GameState::Shutdown) {
    SDL_PumpEvents();  // ou InputManager::beginFrame(), conforme o app
    Caffeine::Core::Duration dt = timer.tick();
    loop.tick(dt.seconds);
}
```

Com `IGameCallbacks` em vez de lambdas:

```cpp
class MyGame : public Caffeine::IGameCallbacks {
    void onFixedUpdate(f64 dt) override { world.update(dt); }
    void onRender(f64 alpha) override { renderer.render(alpha); }
};
MyGame game;
loop.setCallbacks(&game);
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
| `Init` | `tick()` ignorado; `pause()`/`resume()`/`shutdown()` ignorados |
| `Running` | Loop normal: begin → N×fixed update → render(alpha) → end |
| `Paused` | Sem fixed update; `elapsedTime` e `frameCount` avançam; render continua |
| `Shutdown` | Terminal; `tick()` ignorado |

---

## Decisões de Design

| Decisão | Justificativa |
|---------|---------------|
| Fixed timestep para lógica | Física determinística, replay, multiplayer |
| Acumulador com clamp (`min`) | Evita "spiral of death" em hardware lento |
| Loop sem clock/input próprios | Testável (`tick(dt)` determinístico) e sem acoplamento a SDL |
| `IGameCallbacks` + `std::function` | Classe de jogo ou lambdas, à escolha do chamador |
| `targetFPS`/`vsync` só armazenados | Throttling real pertence ao RHI/janela, não ao loop |

---

## Critério de Aceitação

- [x] `tests/test_gameloop.cpp`: defaults, transições de estado, contagem de frames, taxa de fixed update, clamp anti-spiral, `alpha ∈ [0,1)`
- [x] Spiral of death não ocorre: FPS pode cair mas loop não trava
- [x] `state()` transiciona Init → Running ⇄ Paused → Shutdown (transições inválidas ignoradas)
- [ ] 3600 frames com `fixedDt` acumulado = 60.0 ± 0.001 (sem drift) — teste de longa duração ainda pendente

---

## Dependências

- **Upstream:** [Timer](timer.md) (chamador mede `dt`), tipos de [`src/core/Types.hpp`](../../src/core/Types.hpp)
- **Downstream:** ECS World ([`../ecs/core.md`](../ecs/core.md)), Renderer, Input ([`../input/input-system.md`](../input/input-system.md)) — todos via callbacks, sem acoplamento

---

## Referências

- [`src/core/GameLoop.hpp`](../../src/core/GameLoop.hpp) / [`src/core/GameLoop.cpp`](../../src/core/GameLoop.cpp)
- [`tests/test_gameloop.cpp`](../../tests/test_gameloop.cpp)
- [`docs/architecture_specs.md`](../architecture_specs.md) — §1 Game Loop
- [`core/timer.md`](timer.md) — Mede o `dt` alimentado ao loop
- [Game Programming Patterns — Game Loop](https://gameprogrammingpatterns.com/game-loop.html)
