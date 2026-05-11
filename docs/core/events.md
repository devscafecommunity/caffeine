# 📢 Event Bus

> **Fase:** 4 — O Cérebro
> **Namespace:** `Caffeine::Events`
> **Arquivos:** `src/events/EventBus.hpp`, `src/events/EventBus.cpp`, `src/events/Events.hpp`
> **Status:** ✅ Implementado
> **RF:** RF4.6

---

## Visão Geral

O Event Bus permite comunicação **desacoplada** entre sistemas. Um sistema publica um evento; outros se inscrevem sem conhecer quem publica. Isso elimina dependências diretas (ex: Physics não precisa conhecer Audio).

**Dois modos de entrega:**
- `publish<T>()` — imediato, chama listeners agora (main thread)
- `publishDeferred<T>()` — enfileira para o próximo `dispatch()` (thread-safe)

---

## API

### `EventBus`

```cpp
namespace Caffeine::Events {

using ListenerHandle = u32;

class EventBus {
public:
    // Inscreve um listener tipado. Retorna handle para cancelamento.
    template<typename T>
    ListenerHandle subscribe(std::function<void(const T&)> callback);

    // Remove o listener. No-op se handle inválido. Zero dangling pointers.
    void unsubscribe(ListenerHandle handle);

    // Entrega imediata: chama todos os listeners agora (snapshot seguro).
    template<typename T>
    void publish(const T& event);

    // Enfileira evento para o próximo dispatch(). Thread-safe.
    template<typename T>
    void publishDeferred(T event);

    // Processa toda a fila diferida. Chamar uma vez por frame, no GameLoop.
    void dispatch();

    // Número de eventos pendentes na fila.
    usize pendingCount() const;

    // Remove todos os listeners e esvazia a fila.
    void clear();
};

} // namespace Caffeine::Events
```

### Tipo ID de Evento

Cada tipo `T` recebe um ID único em runtime via endereço de variável static local — zero colisão, zero registro manual:

```cpp
template<typename T>
u32 eventTypeId() {
    static char s_sentinel = 0;
    return static_cast<u32>(reinterpret_cast<uintptr_t>(&s_sentinel));
}
```

---

## Eventos Pré-definidos

Todos em `src/events/Events.hpp`, namespace `Caffeine::Events`.

### ECS

```cpp
struct OnEntityCreated   { u32 entityId; };
struct OnEntityDestroyed { u32 entityId; };
```

### Física / Colisão

```cpp
struct OnCollision2D {
    u32  entityA;
    u32  entityB;
    Vec2 contactPoint;
    Vec2 normal;
    f32  impulse;
};

struct OnTrigger2D {
    u32  triggerEntity;
    u32  otherEntity;
    bool entered; // true = enter, false = exit
};
```

### Combate / Stats

```cpp
struct OnHealthChanged {
    u32 entityId;
    f32 previousHealth;
    f32 currentHealth;
    f32 maxHealth;
};

struct OnDeath {
    u32 entityId;
    u32 killerEntityId; // 0 = environmental / no killer
};
```

### Progressão

```cpp
struct OnScoreChanged {
    u32 playerId;
    i32 previousScore;
    i32 currentScore;
    i32 delta;
};
```

### Cena

```cpp
struct OnLevelLoaded   { u32 levelId; c8 levelName[64]; };
struct OnLevelUnloaded { u32 levelId; };
```

### Input

```cpp
struct OnActionPressed  { u32 actionId; u32 playerId; };
struct OnActionReleased { u32 actionId; u32 playerId; };
```

---

## Exemplos de Uso

### Setup básico

```cpp
Caffeine::Events::EventBus bus;

// Inscrever
ListenerHandle h = bus.subscribe<OnDeath>([](const OnDeath& e) {
    if (e.killerEntityId == 0) {
        // morte ambiental
    }
});

// Publicar imediato (main thread)
bus.publish(OnDeath{ entityId, killerId });

// Cancelar
bus.unsubscribe(h);
```

### Publicação diferida (de worker thread)

```cpp
// PhysicsSystem rodando em job worker:
bus.publishDeferred(OnCollision2D{
    entityA, entityB, contactPoint, normal, impulse
});

// GameLoop, início do fixed update:
bus.dispatch(); // entrega todos os eventos enfileirados
```

### Comunicação entre sistemas sem acoplamento

```cpp
// AudioSystem (não conhece PhysicsSystem)
bus.subscribe<OnCollision2D>([&audio](const OnCollision2D& e) {
    audio.playSFX("impact.caf");
});

// ScoreSystem (não conhece nada de gameplay)
bus.subscribe<OnDeath>([&score](const OnDeath& e) {
    score.add(100);
});

// PhysicsSystem publica, ambos recebem
bus.publishDeferred(OnCollision2D{ ... });
```

---

## Integração no GameLoop

```
Frame N:
  ├── InputManager.poll()
  ├── EventBus.dispatch()          ← entrega diferidos do frame N-1
  ├── World.update(fixedDt)
  │     ├── PhysicsSystem → publishDeferred(OnCollision2D)
  │     ├── CombatSystem  → publish(OnDeath)   ← imediato
  │     └── ...
  └── render(alpha)

Frame N+1:
  ├── EventBus.dispatch()          ← entrega os OnCollision2D do frame N
  └── ...
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|---------------|
| Type-erased `void*` internamente | Permite `std::unordered_map<u32, vector<Record>>` sem virtual por tipo |
| Snapshot antes de iterar em `publish()` | Listener pode chamar `unsubscribe()` sem invalidar iteração |
| Mutex apenas na fila deferred | `publish()` e `subscribe()` são main-thread only; só `publishDeferred()` precisa de lock |
| `ListenerHandle = u32` | Sem ponteiros pendentes; unsubscribe por ID, não por referência |
| `eventTypeId<T>()` via `static char` | Zero custo de registro; ID único garantido por endereço de static |

---

## Thread Safety

| Operação | Thread-safe? | Motivo |
|----------|-------------|--------|
| `subscribe()` | Main thread only | Modifica `m_listeners` sem lock |
| `unsubscribe()` | Main thread only | Modifica `m_listeners` sem lock |
| `publish()` | Main thread only | Lê `m_listeners` sem lock |
| `publishDeferred()` | ✅ Qualquer thread | Protegido por `m_queueMutex` |
| `dispatch()` | Main thread only | Consome fila após swap; não precisa de lock |

---

## Critérios de Aceitação

| Critério | Resultado |
|----------|-----------|
| 1K eventos/frame sem overhead perceptível | ✅ 1000× `publish()` = **0.085ms** total |
| `publish()` < 0.1ms (hot path) | ✅ **0.086µs**/call (10K reps, Release) |
| `publishDeferred` thread-safe | ✅ 8 threads × 125 eventos = 1000/1000 sem race |
| Eventos diferidos entregues no próximo `dispatch()` | ✅ Zero entregues antes, todos depois |
| Zero listeners mortos após `unsubscribe` | ✅ 0 callbacks após unsubscribe (imediato e deferred) |

---

## Testes

`tests/test_eventbus.cpp` — 18 test cases, 39 assertions:

| Teste | Cobertura |
|-------|-----------|
| subscribe + publish imediato | Fluxo base |
| Múltiplos listeners no mesmo tipo | Fan-out |
| unsubscribe remove listener | Cancelamento correto |
| unsubscribe handle inválido | No-op seguro |
| Tipos de evento isolados | Sem cross-talk |
| publish sem listeners | No-op seguro |
| publishDeferred não entrega imediatamente | Fila correta |
| dispatch entrega todos | Entrega garantida |
| dispatch esvazia fila | Estado pós-dispatch |
| Ordem de entrega deferred | FIFO preservado |
| clear remove listeners e fila | Reset completo |
| Handles únicos por subscription | IDs não colidem |
| Unsubscribe seletivo entre vários | Precisão cirúrgica |
| Deferred com tipos mistos | Multi-type queue |
| `OnEntityCreated` predefinido | Evento ECS |
| `OnDeath` predefinido | Evento combate |
| `OnScoreChanged` predefinido + dispatch | Evento progressão |
| 8 threads × 125 `publishDeferred` | Thread safety |

---

## Dependências

- **Upstream:** `Caffeine::Core::Types`, `src/math/Vec2.hpp`
- **Downstream:** [Physics](physics.md) (publica `OnCollision2D`), [Audio](audio.md) (reage a eventos), [UI](ui.md) (reage a eventos de gameplay), [GameLoop](../core/game-loop.md) (chama `dispatch()`)

---

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Concorrência & Runtime]() | Event Bus no contexto do Game Loop |
| [Sistemas de Jogo]() | Eventos conectam sistemas de gameplay |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §6 Event Bus
- [`physics/physics-2d.md`](physics.md) — publica `OnCollision2D`
- [`core/game-loop.md`](../core/game-loop.md) — chama `eventBus.dispatch()`
- [Índice de Tópicos Transversais]()
