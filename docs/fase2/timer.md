# ⏱️ High-Resolution Timer

> **Fase:** 2 — O Pulso e a Concorrência  
> **Namespace:** `Caffeine::Core`  
> **Arquivo:** `src/time/Timer.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF2.1

---

## Visão Geral

O Timer fornece medições de tempo de alta precisão (microssegundos) para o [Game Loop](game-loop.md) e o [Profiler](debug.md). Usa `SDL_GetPerformanceCounter` internamente, que acessa o hardware clock da plataforma.

**Propósito principal:**
- Medir `deltaTime` entre frames para o Game Loop
- Medir duração de escopos para o Profiler
- Fornecer timestamps absolutos para eventos de debug

---

## API Planejada

```cpp
namespace Caffeine::Core {

// ============================================================================
// @brief  Ponto no tempo de alta resolução.
// ============================================================================
struct TimePoint {
    u64 ticks = 0;  // SDL_GetPerformanceCounter()

    TimePoint() = default;
    explicit TimePoint(u64 t) : ticks(t) {}

    static TimePoint now();

    bool operator<(TimePoint o)  const { return ticks < o.ticks; }
    bool operator<=(TimePoint o) const { return ticks <= o.ticks; }
};

// ============================================================================
// @brief  Duração entre dois TimePoints.
// ============================================================================
struct Duration {
    f64 seconds = 0.0;

    Duration() = default;
    explicit Duration(f64 s) : seconds(s) {}

    f64 millis()  const { return seconds * 1000.0; }
    f64 micros()  const { return seconds * 1'000'000.0; }
    u64 nanos()   const { return static_cast<u64>(seconds * 1'000'000'000.0); }

    static Duration between(TimePoint start, TimePoint end);
    static Duration since(TimePoint start) { return between(start, TimePoint::now()); }
};

// ============================================================================
// @brief  Timer principal para medir deltaTime entre frames.
//
//  Uso típico:
//
//      Timer timer;
//      while (running) {
//          f64 dt = timer.tick();   // seconds since last tick
//          update(dt);
//      }
// ============================================================================
class Timer {
public:
    Timer();

    // Avança o clock interno. Retorna deltaTime em segundos desde o último tick.
    f64 tick();

    // Reseta o timer (zera referência de tempo).
    void reset();

    // Retorna o tempo total decorrido desde o reset/construção.
    f64 elapsed() const;

    // Retorna o deltaTime do último tick (sem avançar).
    f64 lastDelta() const { return m_lastDelta; }

    // Retorna o número de ticks chamados.
    u64 tickCount() const { return m_tickCount; }

private:
    TimePoint m_start;
    TimePoint m_last;
    f64        m_lastDelta = 0.0;
    u64        m_tickCount = 0;
};

// ============================================================================
// @brief  ScopeTimer — mede duração de um escopo automaticamente (RAII).
//
//  Uso:
//      {
//          ScopeTimer t("physics_update");
//          runPhysics();
//      }  // automaticamente registra no Profiler
// ============================================================================
class ScopeTimer {
public:
    explicit ScopeTimer(const char* name);
    ~ScopeTimer();

private:
    const char* m_name;
    TimePoint   m_start;
};

}  // namespace Caffeine::Core
```

---

## Exemplos de Uso

```cpp
// --- Game Loop básico ---
Caffeine::Core::Timer timer;

while (running) {
    f64 dt = timer.tick();
    // dt ≈ 0.016666... para 60fps
    update(dt);
    render();
}

// --- Medir performance de um bloco ---
auto start = Caffeine::Core::TimePoint::now();
heavyComputation();
auto dur = Caffeine::Core::Duration::since(start);
LOG_INFO("Computation took %.2f ms", dur.millis());

// --- ScopeTimer com Profiler ---
{
    Caffeine::Core::ScopeTimer timer("ECS::update");
    world.update(dt);
}
// Resultado automaticamente registrado no Profiler
```

---

## Implementação Interna

```
SDL_GetPerformanceCounter()  →  u64 ticks
SDL_GetPerformanceFrequency() →  u64 freq

deltaTime (seconds) = (currentTicks - lastTicks) / freq
```

**Por que SDL?**  
`SDL_GetPerformanceCounter` é a interface portável para o hardware clock — `QueryPerformanceCounter` no Windows, `clock_gettime(CLOCK_MONOTONIC)` no Linux/macOS.

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| `u64 ticks` interno | Sem conversão prematura para f64 |
| `Duration::between(start, end)` | Clareza na leitura do código |
| `ScopeTimer` RAII | Garante `endScope()` mesmo em early return |
| `timer.lastDelta()` sem avançar | Útil para calcular interpolation alpha |

---

## Critério de Aceitação

- [ ] Precisão < 1 microssegundo em benchmark de 1 hora
- [ ] `ScopeTimer` tem overhead < 100ns
- [ ] `tick()` thread-safe para leitura de múltiplos sistemas
- [ ] Sem drift em 3600 frames (deltaTime acumulado = 60.0 ± 0.001)

---

## Dependências

- **Upstream:** `Caffeine::Core::Types` ([`src/core/Types.hpp`](../../src/core/Types.hpp))
- **Downstream:** [Game Loop](game-loop.md), [Profiler](debug.md)
- **SDL3:** `SDL_GetPerformanceCounter`, `SDL_GetPerformanceFrequency`

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §1.3 Interface Game Loop
- [`docs/fase2/game-loop.md`](game-loop.md) — Usa Timer internamente
- [`docs/fase2/debug.md`](debug.md) — Profiler usa ScopeTimer
