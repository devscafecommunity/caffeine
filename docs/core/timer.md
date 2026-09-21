# ⏱️ High-Resolution Timer

> **Fase:** 2 — O Pulso e a Concorrência  
> **Namespace:** `Caffeine::Core`  
> **Arquivos:** `src/core/Timer.hpp`, `src/core/Timer.cpp`  
> **Status:** ✅ Implementado  
> **Testes:** `tests/test_timer.cpp` (~30 casos)

---

## Visão Geral

O Timer fornece medições de tempo de alta precisão para o [Game Loop](game-loop.md)
e para instrumentação manual de performance. A implementação usa
`std::chrono::high_resolution_clock` — **não** SDL — com ticks em
**nanossegundos desde a epoch**.

**Propósito principal:**
- Medir `deltaTime` entre frames para o Game Loop (`Timer::tick()`)
- Medir duração acumulada com ciclos start/stop (`Timer::elapsed()`)
- Marcar escopos RAII (`ScopeTimer`)

---

## API Real

```cpp
namespace Caffeine::Core {

// ============================================================================
// @brief  Ponto no tempo. ticks em nanossegundos (ver Timer.cpp).
//         Comparações diretas; sem relógio próprio (não há TimePoint::now()).
// ============================================================================
struct TimePoint {
    u64 ticks = 0;

    TimePoint() = default;
    explicit TimePoint(u64 t) : ticks(t) {}

    bool operator<(const TimePoint&) const;
    bool operator<=(const TimePoint&) const;
    bool operator>(const TimePoint&) const;
    bool operator>=(const TimePoint&) const;
    bool operator==(const TimePoint&) const;
    bool operator!=(const TimePoint&) const;
};

// ============================================================================
// @brief  Diferença ABSOLUTA entre dois TimePoints.
//
//  ⚠️ Assume ticks em MICROSSEGUNDOS (divide por 1e6). O Timer produz ticks
//  em nanossegundos, portanto este operador NÃO é consistente com TimePoints
//  vindos do Timer — prefira Timer::elapsed()/tick() para medições reais.
// ============================================================================
inline Duration operator-(const TimePoint& a, const TimePoint& b);

// ============================================================================
// @brief  Duração em segundos (f64), com fábricas e aritmética.
// ============================================================================
struct Duration {
    f64 seconds = 0.0;

    Duration() = default;
    explicit Duration(f64 s) : seconds(s) {}

    static Duration fromSeconds(f64 s);
    static Duration fromMillis(f64 ms);
    static Duration fromMicros(f64 us);
    static Duration fromNanos(f64 ns);

    f64 millis() const;   // seconds * 1e3
    f64 micros() const;   // seconds * 1e6
    f64 nanos()  const;   // seconds * 1e9 (retorna f64, não u64)

    Duration operator+(const Duration&) const;
    Duration operator-(const Duration&) const;
    Duration operator*(f64 scalar) const;   // + forma livre scalar * Duration
    Duration operator/(f64 scalar) const;

    bool operator<(const Duration&) const;
    bool operator<=(const Duration&) const;
    bool operator>(const Duration&) const;
    bool operator>=(const Duration&) const;
    bool operator==(const Duration&) const;  // igualdade com epsilon 1e-15
    bool operator!=(const Duration&) const;
};

// ============================================================================
// @brief  Cronômetro start/stop com acumulador.
//
//  - Nasce parado (isRunning() == false).
//  - elapsed() acumula todos os ciclos start/stop desde o último reset().
//  - tick() retorna o tempo desde o último tick/start e rearma a referência;
//    retorna Duration(0) se parado.
// ============================================================================
class Timer {
public:
    Timer();
    ~Timer();

    void start();   // idempotente: segundo start() sem stop() é ignorado
    void stop();    // acumula ticks no total; parado mantém o acumulado
    void reset();   // zera acumulado e para o timer

    Duration elapsed() const;  // não rearma (peek)
    Duration tick();           // rearma a referência interna

    bool isRunning() const;

private:
    TimePoint m_start_time;
    TimePoint m_stop_time;
    u64 m_accumulated_ticks;   // nanossegundos
    bool m_is_running;
};

// ============================================================================
// @brief  RAII: start() no construtor, stop() no destrutor.
//
//  ⚠️ Limitação atual: a medição NÃO é registrada em lugar nenhum
//  (nem log, nem Profiler) e m_name não é usado. Serve hoje apenas como
//  marcador de escopo; integração com o Profiler ainda está pendente.
// ============================================================================
class ScopeTimer {
public:
    explicit ScopeTimer(const char* name);
    ~ScopeTimer();

private:
    const char* m_name;
    Timer m_timer;
};

}  // namespace Caffeine::Core
```

---

## Exemplos de Uso

```cpp
// --- deltaTime por frame ---
Caffeine::Core::Timer timer;
timer.start();
while (running) {
    Caffeine::Core::Duration dt = timer.tick();
    update(dt.seconds);
}

// --- tempo acumulado com pausas ---
Caffeine::Core::Timer t;
t.start();
/* ... */ t.stop();   // pausa: elapsed() congela
/* ... */ t.start();  // retoma acumulando
Caffeine::Core::Duration total = t.elapsed();

// --- fábricas e conversões ---
auto d = Caffeine::Core::Duration::fromMillis(16.666);
LOG_INFO("dt = %.2f ms", d.millis());

// --- ScopeTimer (hoje: só marca escopo, sem saída) ---
{
    Caffeine::Core::ScopeTimer timer("ECS::update");
    world.update(dt);
}
```

---

## Implementação Interna

```
std::chrono::high_resolution_clock::now()
  → time_since_epoch() em nanossegundos → TimePoint.ticks

elapsed() = fromNanos(now - m_start_time + m_accumulated_ticks)
tick()    = fromNanos(now - m_start_time)  + rearma m_start_time = now
```

Sem dependência de SDL. `Timer` não é thread-safe (sem mutex/atomics);
para leitura de múltiplos sistemas, cada sistema deve ter sua instância
ou o acesso deve ser serializado pelo Game Loop.

---

## Decisões de Design

| Decisão | Justificativa |
|---------|---------------|
| `std::chrono` em vez de SDL | Timer é módulo core sem dependência de SDL |
| `u64 ticks` interno (ns) | Sem conversão prematura para f64 |
| `elapsed()` não rearma | Permite `peek` repetido (ver testes) |
| Acumulador em start/stop | Suporta pausa/retomada sem perder tempo |
| `Duration::operator==` com epsilon | Comparação exata de f64 é instável |

---

## Critério de Aceitação

- [x] `tests/test_timer.cpp`: TimePoint, Duration, Timer, ScopeTimer e integração (~30 casos)
- [x] Precisão de microssegundos verificada (`sleep 1000us` → `millis() > 0.5`)
- [ ] `ScopeTimer` integrado ao Profiler (hoje não registra nada)
- [ ] Resolver inconsistência de unidade do `operator-(TimePoint, TimePoint)` (assume µs; Timer produz ns)
- [ ] `tick()` thread-safe para leitura de múltiplos sistemas

---

## Dependências

- **Upstream:** `Caffeine::Core::Types` ([`src/core/Types.hpp`](../../src/core/Types.hpp))
- **Downstream:** [Game Loop](game-loop.md); Profiler planejado ([`../debug/debug-tools.md`](../debug/debug-tools.md))
- **Externas:** apenas `<chrono>` da std

---

## Referências

- [`src/core/Timer.hpp`](../../src/core/Timer.hpp) / [`src/core/Timer.cpp`](../../src/core/Timer.cpp)
- [`tests/test_timer.cpp`](../../tests/test_timer.cpp)
- [`docs/architecture_specs.md`](../architecture_specs.md) — §1 Game Loop
- [`core/game-loop.md`](game-loop.md) — Usa Timer internamente
