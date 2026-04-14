# 📢 Event Bus

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::Events`  
> **Arquivo:** `src/events/EventBus.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF4.6

---

## Visão Geral

O Event Bus permite comunicação **desacoplada** entre sistemas. Um sistema publica um evento; outros se inscrevem sem conhecer quem publica. Isso elimina dependências diretas entre sistemas (ex: Physics não precisa conhecer Audio).

---

## API Planejada

```cpp
namespace Caffeine::Events {

using ListenerHandle = u32;

// ============================================================================
// @brief  Interface base para eventos.
//
//  Eventos são structs POD com dados relevantes.
//  Ex: struct OnCollision2D { Entity a, b; Vec2 normal; };
// ============================================================================
struct IEvent {
    virtual ~IEvent() = default;
    virtual u32 typeId() const = 0;
};

// Gera typeId único por tipo de evento (via static local)
template<typename T>
u32 eventTypeId() {
    static char unique = 0;
    return reinterpret_cast<uptr>(&unique);
}

using EventListener = std::function<void(const IEvent&)>;

// ============================================================================
// @brief  Barramento de eventos pub/sub tipado.
//
//  Thread safety: publish() pode ser chamado de qualquer thread.
//  Os listeners são chamados na main thread (no próximo dispatch()).
// ============================================================================
class EventBus {
public:
    EventBus();
    ~EventBus();

    // ── Subscribe ──────────────────────────────────────────────
    template<typename EventT>
    ListenerHandle subscribe(std::function<void(const EventT&)> listener);

    void unsubscribe(ListenerHandle handle);

    // ── Publish ────────────────────────────────────────────────
    // Imediato: chama listeners agora
    template<typename EventT>
    void publish(const EventT& event);

    // Diferido: enfileira para o próximo dispatch()
    template<typename EventT>
    void publishDeferred(const EventT& event);

    // Processa fila de eventos diferidos (chamado pelo GameLoop)
    void dispatch();

    void clear();
    u32  pendingCount() const;

private:
    struct ListenerRecord {
        ListenerHandle handle;
        EventListener  callback;
        bool           valid = true;
    };

    HashMap<u32, std::vector<ListenerRecord>> m_listeners;
    std::vector<std::unique_ptr<IEvent>>       m_queue;
    ListenerHandle                             m_nextHandle = 1;
    std::mutex                                 m_queueMutex;
};

}  // namespace Caffeine::Events
```

---

## Eventos Pré-definidos

```cpp
namespace Caffeine::Events {

// ── ECS Events ────────────────────────────────────────────────
struct OnEntityCreated   { ECS::Entity entity; };
struct OnEntityDestroyed { ECS::Entity entity; };

// ── Physics Events ────────────────────────────────────────────
struct OnCollision2D {
    ECS::Entity a, b;
    Vec2        contactPoint;
    Vec2        normal;
    f32         penetration;
};

struct OnTrigger2D {
    ECS::Entity trigger;
    ECS::Entity other;
    bool        entering;  // true = entrou, false = saiu
};

// ── Gameplay Events ───────────────────────────────────────────
struct OnHealthChanged {
    ECS::Entity entity;
    f32         oldHealth;
    f32         newHealth;
    f32         delta;
};

struct OnDeath {
    ECS::Entity entity;
    ECS::Entity killer;
};

struct OnScoreChanged {
    i32 oldScore;
    i32 newScore;
};

// ── Scene Events ──────────────────────────────────────────────
struct OnLevelLoaded   { FixedString<64> levelName; };
struct OnLevelUnloaded { FixedString<64> levelName; };

// ── Input Events (emitidos pelo InputManager) ─────────────────
struct OnActionPressed  { Input::Action action; };
struct OnActionReleased { Input::Action action; };

}  // namespace Caffeine::Events
```

---

## Exemplos de Uso

```cpp
// ── Setup ─────────────────────────────────────────────────────
Caffeine::Events::EventBus eventBus;

// ── Subscribe ─────────────────────────────────────────────────
auto handle = eventBus.subscribe<OnCollision2D>([](const OnCollision2D& e) {
    CF_INFO("Physics", "Collision: %u vs %u", e.a.id(), e.b.id());
    // Toca som de impacto:
    audioSystem.playSFX("sfx/impact.caf", 1.0f);
});

eventBus.subscribe<OnDeath>([&](const OnDeath& e) {
    spawnDeathParticles(e.entity);
    scoreSystem.addScore(100);
    eventBus.publish(OnScoreChanged{ score, score + 100 });
});

// ── Publish (do PhysicsSystem) ────────────────────────────────
if (detectCollision(a, b)) {
    eventBus.publishDeferred(OnCollision2D{
        .a            = entityA,
        .b            = entityB,
        .contactPoint = contact,
        .normal       = normal,
        .penetration  = depth
    });
}

// ── GameLoop — despacha eventos diferidos ─────────────────────
// (no início de cada fixed update step)
eventBus.dispatch();  // chama todos os listeners pendentes

// ── Remover listener ──────────────────────────────────────────
eventBus.unsubscribe(handle);
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Pub/sub tipado com template | Erro de tipo pego em compile time |
| Deferred queue | Physics publica durante iteração; listeners rodam depois |
| `ListenerHandle` para unsubscribe | Sem ponteiros pendentes (dangling) |
| `dispatch()` na main thread | Thread safety — listeners não precisam de locks |
| Mutex apenas na fila | `publish()` de workers é safe; `dispatch()` é single-thread |

---

## Critério de Aceitação

- [ ] 1K eventos/frame sem overhead perceptível
- [ ] `publish()` < 0.1ms (hot path)
- [ ] `subscribe`/`unsubscribe` thread-safe
- [ ] Eventos diferidos entregues no próximo `dispatch()`
- [ ] Zero listeners mortos (dangling) após `unsubscribe`

---

## Dependências

- **Upstream:** [ECS Core](ecs.md), `Caffeine::Core::Types`
- **Downstream:** [Physics](physics.md) (publica colisões), [Audio](audio.md) (reage a eventos), [UI](ui.md) (reage a eventos de gameplay)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §6 Event Bus
- [`docs/fase4/physics.md`](physics.md) — publica `OnCollision2D`
- [`docs/fase2/game-loop.md`](../fase2/game-loop.md) — chama `eventBus.dispatch()`
