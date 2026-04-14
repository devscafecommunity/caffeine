# 🧩 ECS Core

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::ECS`  
> **Arquivos:** `src/ecs/World.hpp`, `Entity.hpp`, `ComponentQuery.hpp`, `ISystem.hpp`  
> **Status:** 📅 Planejado  
> **RFs:** RF4.1, RF4.2, RF4.3, RF4.4

---

## Visão Geral

O ECS (Entity Component System) da Caffeine é **archetype-based** — entidades com o mesmo conjunto de componentes vivem em memória contígua. Isso maximiza cache locality e permite SIMD nos systems.

**Conceitos:**
- **Entity** = `u32` ID único. Sem dados, sem lógica.
- **Component** = struct POD. Sem métodos, sem virtual.
- **Archetype** = grupo de entidades com mesmo `ComponentSet`. Arrays contíguos por tipo.
- **System** = função que itera entidades que combinam um query.
- **World** = container que gerencia tudo.

---

## API Planejada

```cpp
namespace Caffeine::ECS {

// ============================================================================
// @brief  Handle para uma entidade.
//
//  Encapsula (id, World*) — o World é necessário para operações de
//  add/remove/has/get/destroy.
// ============================================================================
class Entity {
public:
    static const Entity INVALID;

    Entity() : m_id(u32_max), m_world(nullptr) {}
    Entity(u32 id, World* world) : m_id(id), m_world(world) {}

    u32  id()      const { return m_id; }
    bool isValid() const;

    template<typename T, typename... Args>
    T& add(Args&&... args);         // Adiciona componente

    template<typename T>
    void remove();                   // Remove componente

    template<typename T>
    bool has() const;                // Verifica existência

    template<typename T>
    T* get();                        // Retorna ponteiro (nullptr se não tem)

    template<typename T>
    T& getOrAdd();                   // get se existe, add se não existe

    void destroy();

    explicit operator bool() const { return isValid(); }
    bool operator==(Entity o) const { return m_id == o.m_id; }

private:
    u32    m_id;
    World* m_world;
};

// ============================================================================
// @brief  Query — especifica quais componentes uma entidade deve ter.
//
//  Uso:
//      ComponentQuery q;
//      q.with<Position2D, Velocity2D>().without<Disabled>();
//      world.query(q, [](Position2D& p, const Velocity2D& v) { ... });
// ============================================================================
class ComponentQuery {
public:
    template<typename... Ts>
    ComponentQuery& with();           // Inclui entidades com todos estes

    template<typename... Ts>
    ComponentQuery& without();        // Exclui entidades com qualquer destes

    // Iteração single-thread
    template<typename Func>
    void forEach(World& world, Func&& func) const;

    // Iteração paralela via Job System
    template<typename Func>
    void forEachParallel(World& world, Func&& func,
                         Threading::JobBarrier* barrier) const;

private:
    std::vector<ComponentID> m_includes;
    std::vector<ComponentID> m_excludes;
};

// ============================================================================
// @brief  Interface base para todos os Systems ECS.
// ============================================================================
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(World& world, f64 dt) = 0;
    virtual i32  priority() const = 0;   // menor = executa primeiro
    virtual const char* name() const = 0;
    virtual bool isEnabled() const { return true; }
};

// ============================================================================
// @brief  World — container principal do ECS.
//
//  Gerencia:
//  - Entity lifecycle (create, destroy)
//  - Component storage (archetype-based arrays)
//  - System registry (execução em ordem de prioridade)
//  - CommandBuffer diferido (safe point de mutação)
// ============================================================================
class World {
public:
    World();
    ~World();

    // ── Entity lifecycle ───────────────────────────────────────
    Entity create(const char* name = nullptr);
    void   destroy(Entity e);       // marca para destruição (cmd buffer)
    void   destroyAll();

    // ── Component access ───────────────────────────────────────
    template<typename T, typename... Args>
    T& add(Entity e, Args&&... args);

    template<typename T>
    void remove(Entity e);

    template<typename T>
    bool has(Entity e) const;

    template<typename T>
    T* get(Entity e);

    // ── Queries ────────────────────────────────────────────────
    template<typename Func>
    void query(const ComponentQuery& q, Func&& func) const;

    // ── System registry ────────────────────────────────────────
    template<typename SystemT, typename... Args>
    SystemT* registerSystem(Args&&... args);

    template<typename SystemT>
    void unregisterSystem();

    template<typename SystemT>
    SystemT* getSystem();

    // ── Frame update (chama todos os systems em ordem) ─────────
    void update(f64 dt);

    // ── Command buffer (flush automático após update) ──────────
    void flushCommands();  // aplicado pelo GameLoop após World::update()

    // ── Estatísticas ───────────────────────────────────────────
    u32 entityCount()    const;
    u32 archetypeCount() const;

private:
    Archetype* getOrCreateArchetype(ComponentSet set);
    void       executeCommands();

    std::vector<Archetype>         m_archetypes;
    HashMap<ComponentSet, u32>     m_archetypeIndex;
    u32                             m_entityCount    = 0;
    std::vector<std::unique_ptr<ISystem>> m_systems;

    // Command buffer diferido
    struct Command {
        enum class Type { Create, Destroy, AddComponent, RemoveComponent } type;
        u32  entityId;
        u32  componentId;
        std::vector<u8> data;
    };
    std::vector<Command> m_pendingCommands;
};

}  // namespace Caffeine::ECS
```

---

## Componentes Pré-definidos

```cpp
namespace Caffeine::Components {

// ── Transform 2D ──────────────────────────────────────────────
struct Position2D  { f32 x = 0, y = 0; };
struct Velocity2D  { f32 x = 0, y = 0; };
struct Rotation2D  { f32 angle = 0; };

// ── Transform 3D (Fase 5) ─────────────────────────────────────
// struct Position3D  { f32 x, y, z; };
// struct Rotation3D  { Quat rotation; };

// ── Sprite Visual ─────────────────────────────────────────────
struct Sprite {
    u32   textureId  = u32_max;
    Vec2  offset    = {0, 0};
    Vec2  scale     = {1, 1};
    f32   rotation  = 0;
    Color tint      = Color::WHITE;
    i32   layer    = 0;
    bool  flipX    = false;
    bool  flipY    = false;
};

// ── Physics ───────────────────────────────────────────────────
struct RigidBody2D {
    f32  mass        = 1.0f;
    f32  restitution = 0.5f;
    f32  friction    = 0.3f;
    Vec2 acceleration = {0, 0};
    bool isKinematic = false;
};

struct Collider2D {
    Vec2 size     = {1, 1};
    Vec2 offset   = {0, 0};
    bool isStatic = false;
    u32  layer    = 0;
    bool isTrigger = false;
};

// ── Audio ─────────────────────────────────────────────────────
struct AudioEmitter {
    FixedString<128> clipPath;
    f32 volume  = 1.0f;
    bool loop   = false;
    bool spatial = true;
};

// ── Tags ──────────────────────────────────────────────────────
struct Player     {};
struct Enemy      {};
struct Projectile {};
struct Particle   {};
struct Disabled   {};  // sistemas pulam entidades com este tag
struct Destroy    {};  // marcada para destruição no fim do frame

// ── Meta ──────────────────────────────────────────────────────
struct Name {
    FixedString<32> value;
};

struct Parent {
    Entity parent = Entity::INVALID;
    bool   dirty  = true;
};

struct WorldTransform {
    Mat4 matrix = Mat4::identity();
};

}  // namespace Caffeine::Components
```

---

## Command Buffer Diferido (RF4.4)

```cpp
// ── POR QUE DIFERIR? ─────────────────────────────────────────
// Sistemas NÃO podem criar/destruir entidades durante iteração:
// isso invalida ponteiros nos archetypes e causa UB.

// ERRADO (durante forEach):
world.query(enemyQuery, [&](Enemy& e, Position2D& pos) {
    if (pos.x > 1000) world.destroy(entity);  // ❌ invalida iteração!
});

// CORRETO (comand buffer):
world.query(enemyQuery, [&](Enemy& e, Position2D& pos, Entity self) {
    if (pos.x > 1000) world.destroy(self);    // ✅ anotado para depois
});
// world.flushCommands() chamado pelo GameLoop APÓS todos os updates
```

---

## Exemplos de Uso

```cpp
// ── Criar entidade ────────────────────────────────────────────
Caffeine::ECS::World world;

Entity player = world.create("Player");
world.add<Position2D>(player, {100, 200});
world.add<Velocity2D>(player, {0, 0});
world.add<Sprite>(player, { .textureId = heroTexId });
world.add<PlayerInput>(player);
world.add<RigidBody2D>(player, { .mass = 5.0f });
world.add<Collider2D>(player, { .size = {32, 64} });

// ── Query simples ─────────────────────────────────────────────
ComponentQuery movQuery;
movQuery.with<Position2D, Velocity2D>().without<Disabled>();

world.query(movQuery, [](Position2D& pos, const Velocity2D& vel, f64 dt) {
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
});

// ── Registrar sistemas ────────────────────────────────────────
world.registerSystem<PhysicsSystem2D>();   // priority = 100
world.registerSystem<AnimationSystem>();  // priority = 200
world.registerSystem<RenderSystem>();     // priority = 1000

// ── Game loop ─────────────────────────────────────────────────
while (running) {
    world.update(fixedDt);      // executa todos os systems em ordem
    world.flushCommands();       // aplica create/destroy pendentes
}
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Archetype-based | Cache locality máximo — arrays contíguos por tipo |
| Entity = u32 | Sem alocação, sem virtual, handle leve |
| ComponentSet como bitmask | Comparação O(1) para encontrar archetype |
| Command buffer diferido | Sem invalidação de iteração durante updates |
| Systems por prioridade | Física antes de Animação, UI por último |

---

## Critério de Aceitação

- [ ] 100K entidades a 60fps com 5 systems ativos
- [ ] `query()` < 5ms para 100K entidades
- [ ] Command buffer: create/destroy aplicados após todos os updates
- [ ] `tsan` clean — sem data races em `forEachParallel`
- [ ] ECS benchmark: insert/query/destroy sem fragmentação de memória

---

## Dependências

- **Upstream:** [Fase 1 — Memory, Containers](../architecture/memory.md), [Fase 2 — Job System](../fase2/job-system.md)
- **Downstream:** [Scene](scene.md), [Physics](physics.md), [Animation](animation.md), [Audio](audio.md), [UI](ui.md), [Fase 3 — Batch Renderer](../fase3/batch-renderer.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §4 ECS Core
- [flecs — Archetype ECS](https://github.com/SanderMertens/flecs)
- [EnTT in Action](https://github.com/skypjack/entt/wiki/EnTT-in-Action)
