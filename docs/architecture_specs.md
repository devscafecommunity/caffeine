# 🏗️ Architecture Specifications

> ⚠️ **Status:** Versão 1.0 — Completo para as Fases 1-6.  
> ⚠️ Para contexto completo, consulte [`docs/MASTER.md`](../docs/MASTER.md) §3.

Este documento contém as **especificações técnicas detalhadas** de cada subsistema da Caffeine Engine. Baseado em pesquisa de padrões de engines como [flecs](https://github.com/SanderMertens/flecs), [EnTT](https://github.com/skypjack/entt), e integrações com SDL3.

---

## 📋 Índice das Especificações

| Especificação | Fase | Status | Descrição |
|---|---|---|---|
| **1. Game Loop** | 2 | 🔄 Em Progresso | Loop principal, fixed timestep, estados |
| **2. Job System** | 2 | 📅 Planejado | Thread pool, work-stealing, barreiras |
| **3. Input System** | 2 | 📅 Planejado | Action mapping, polling/event-driven |
| **4. ECS Core** | 4 | 📅 Planejado | Archetype-based, queries, systems |
| **5. Scene Graph & Serialização** | 4 | 📅 Planejado | Hierarquia, prefabs, .caf format |
| **6. Event Bus** | 4 | 📅 Planejado | Pub/sub typed, fila com prioridade |
| **7. Asset Manager** | 3 | 📅 Planejado | Async loading, cache, hot-reload |
| **8. RHI** | 3 | 📅 Planejado | Abstração SDL_GPU, command queue |
| **9. Batch Renderer** | 3 | 📅 Planejado | Sprite batching, texture atlas |
| **10. Camera System** | 3 | 📅 Planejado | Matriz 4×4, orto/perspectiva |
| **11. Audio System** | 4 | 📅 Planejado | SDL3 audio, pooling, spatial |
| **12. Animation System** | 4 | 📅 Planejado | Sprite frames, state machine |
| **13. Physics (2D)** | 4 | 📅 Planejado | AABB, collision, integration ECS |
| **14. UI System** | 5 | 📅 Planejado | Retained mode, ECS integration |
| **15. Math Library** | 1+ | 📅 Planejado | Vec, Mat, Quat, SIMD hints |
| **16. Debug/Development Tools** | 2+ | 📅 Planejado | Logging, profiler, debug draw |

---

## A. Padrões Arquiteturais Transversais

Antes de detalharmos cada subsistema, estabelecemos os **padrões compartilhados** que todas as especificações seguem:

### A.1 Ciclo de Vida de Frame

Todo subsistema participa de um ciclo previsível dentro do game loop:

```
┌─────────────────────────────────────────────────────────────┐
│                   FRAME LIFECYCLE                           │
├─────────────┬──────────────┬───────────────┬────────────────┤
│ beginFrame  │  update/     │  snapshot     │ endFrame      │
│             │  fixedUpdate │  published    │               │
│             │              │               │               │
│ • Limpar    │ • Input      │ • Read-only   │ • Debug data  │
│   transients│   evaluate   │   snapshots   │ • Cleanup      │
│ • Processar │ • Physics    │   disponíveis │ • Stats update │
│   SDL events│ • ECS systems│   para render │               │
│ • Iniciar   │ • Jobs       │               │               │
│   jobs      │ • Asset load │               │               │
└─────────────┴──────────────┴───────────────┴────────────────┘
```

### A.2 Relação com ECS

**Bom para ECS** (dados de simulação):
- Transform, Velocity, RigidBody, Collider
- Sprite, Animator, AudioEmitter
- Comandos de gameplay (MoveIntent, FireIntent)

**Mau para ECS** (estado global/não-entidade):
- Asset registry, File watchers
- Input device management, Scene transitions
- Log storage, Console state

### A.3 Relação com Job System

**Bons candidatos para jobs paralelos:**
- Carregamento de assets (I/O + decode)
- Avaliação de animação para muitas entidades
- Broad phase collision detection
- Processamento de debug/profiler

**Maus candidatos:**
- SDL event polling
- UI focus mutation
- Scene swap finalization
- Real-time audio callback

### A.4 Política de Allocators

| Uso | Allocator | Exemplo |
|---|---|---|
| Scratch por frame | Linear | Event lists, contact pairs |
| Objetos de tamanho fixo | Pool | Voices, emitters, bindings |
| Escopos aninhados | Stack | Nível de jogo, task scopes |
| Longa duração | Persistent | Registries, assets, configs |

### A.5 Regra de Threading

> "Worker threads processam dados puros e produzem resultados publicáveis; transições de ownership/estado acontecem na main thread a menos que o subsistema defina explicitamente um path de publicação lock-free."

---

## 1. Game Loop

**Fase:** 2 — O Pulso e a Concorrência  
**Responsável:** Architects

### 1.1 Visão Geral

O Game Loop é o coração da engine — o loop que roda do início ao fim do jogo, em cada frame. A Caffeine implementa o padrão **Fixed Timestep com Acumulador**, que desvincula a lógica de física (que precisa de tempo consistente) da renderização (que pode variar).

Inspirado em [Game Programming Patterns — Decoupling Systems](https://gameprogrammingpatterns.com/game-loop.html).

### 1.2 Estados do Loop

```
┌──────────────────────────────────────────────────────────┐
│                   GAME LOOP STATES                       │
├─────────────┬──────────────┬──────────────┬─────────────┤
│    INIT     │   RUNNING    │    PAUSED    │  SHUTDOWN   │
│             │              │              │             │
│ - Carregar   │ - Processar  │ - Pausar     │ - Cleanup   │
│   recursos   │   eventos    │   lógica     │   recursos  │
│ - Criar      │ - Update     │ - Manter     │ - Flush     │
│   mundo      │   ECS        │   render     │   logs      │
│ - Inicializ. │ - Renderizar │ - aceitar    │ - Exit      │
│   subsistemas│ - Interpolar │   resume     │   code      │
└─────────────┴──────────────┴──────────────┴─────────────┘
```

### 1.3 Interface Pública

```cpp
namespace Caffeine::Core {

// ============================================================================
// @brief  Estados possíveis do game loop.
// ============================================================================
enum class GameState : u8 {
    Init,
    Running,
    Paused,
    Shutdown
};

// ============================================================================
// @brief  Configuração do game loop.
// ============================================================================
struct GameLoopConfig {
    f64 fixedDeltaTime = 1.0 / 60.0;  // 60 updates/segundo
    f64 maxFrameTime   = 0.25;          // Clamp para evitar spiral of death
    u32 targetFPS      = 0;             // 0 = unlimited
    bool vsync         = true;
    bool interpolation = true;          // Interpolar renderização
};

// ============================================================================
// @brief  Game loop principal com fixed timestep.
//
//  Ciclo por frame:
//
//  1. processInput()      — SDL_PollEvent, alimenta Input System
//  2. accumulator += dt    — Acumula tempo desde o último update
//  3. while (accum >= dt)  — Update em fixed steps enquanto accumulator
//     - processEvents()    — Event Bus processa fila
//     - updateSystems()    — ECS systems em ordem de precedência
//     - accumulator -= dt
//  4. interpolate(alpha)   — Calcula valores interpolados para render
//  5. render(alpha)       — Render com valores interpolados
// ============================================================================
class GameLoop {
public:
    explicit GameLoop(const GameLoopConfig& config);

    void init();
    void tick(f64 deltaTime);
    void pause();
    void resume();
    void shutdown();

    GameState state() const { return m_state; }
    f64 elapsedTime() const;
    f64 interpolationAlpha() const { return m_alpha; }
    u64 frameCount() const { return m_frameCount; }

private:
    void processInput();
    void processFixedUpdate(f64 dt);
    void processInterpolation(f64 alpha);

    GameLoopConfig  m_config;
    GameState       m_state = GameState::Init;
    f64              m_accumulator    = 0.0;
    f64              m_elapsedTime    = 0.0;
    f64              m_alpha          = 0.0;
    u64              m_frameCount     = 0;
};

}  // namespace Caffeine::Core
```

### 1.4 Spiral of Death Prevention

Se o loop de update cair para trás (ex: sistema lento), o acumulador cresce indefinidamente:

```cpp
m_accumulator = min(m_accumulator, m_config.maxFrameTime);
// Clamp para no máximo 250ms — skip frames se necessário
```

### 1.5 Decisões de Design

| Decisão | Justificativa |
|---|---|
| Fixed timestep para lógica | Física determinística, replay de frames, rede |
| Acumulador com clamp | Evita "spiral of death" em sistemas lentos |
| `vsync` configurável | Permite benchmark em loop sem vsync |
| `interpolation` configurável | Debug mais fácil sem interp |

---

## 2. Job System

**Fase:** 2 — O Pulso e a Concorrência  
**Responsável:** Architects

### 2.1 Visão Geral

O Job System distribui trabalho pesado (física, IA, animação, carregamento de assets) entre os núcleos da CPU. Cada Job é uma unidade de trabalho leve que uma worker thread executa.

A Caffeine implementa um **thread pool com work-stealing** — cada worker tem sua própria fila; quando vazia, "rouba" jobs de workers ocupados.

Baseado em padrões de [Jolt Physics Job System](https://github.com/jrouwe/JoltPhysics) e [Multithreaded Job Systems in Game Engines](https://pulsegeek.com/articles/multithreaded-job-systems-in-game-engines/).

### 2.2 Interfaces

```cpp
namespace Caffeine::Threading {

// ============================================================================
// @brief  Interface base para todos os Jobs.
//
//  Jobs devem ser cheap to copy — dados devem ser passados por valor ou
//  via shared_ptr. Nunca alocar no heap dentro de um Job.
// ============================================================================
struct IJob {
    virtual ~IJob() = default;
    virtual void execute() = 0;
};

// ============================================================================
// @brief  Job com dados armazenados internamente.
// ============================================================================
template<typename DataT>
struct JobWithData : IJob {
    DataT         data;
    std::function<void(DataT&)> func;

    void execute() override { func(data); }
};

// ============================================================================
// @brief  Handle para rastrear e sincronizar Jobs.
// ============================================================================
class JobHandle {
public:
    JobHandle() = default;
    explicit JobHandle(u32 index, u32 version);

    void wait() const;
    bool isComplete() const;
    explicit operator bool() const;

private:
    u32 m_index   = u32_max;
    u32 m_version = u32_max;
};

// ============================================================================
// @brief  Barreira de sincronização — wait() bloqueia até N jobs terminarem.
// ============================================================================
class JobBarrier {
public:
    explicit JobBarrier(u32 targetCount);
    void add();
    void release();
    void wait();

private:
    std::atomic<u32>        m_count;
    std::atomic<u32>        m_generation;
    std::condition_variable m_cv;
    std::mutex              m_mutex;
};

// ============================================================================
// @brief  Sistema de Jobs principal.
//
//  - Num workers: hardware_concurrency() - 1
//  - Fila por worker: lock-free MPMC queue
//  - Work-stealing: Atomic compare-and-swap nas pontas da deque
// ============================================================================
class JobSystem {
public:
    explicit JobSystem(u32 workerCount = 0);
    ~JobSystem();

    JobHandle schedule(std::unique_ptr<IJob> job, JobBarrier* barrier = nullptr);

    template<typename DataT, typename FuncT>
    JobHandle scheduleData(DataT data, FuncT&& func, JobBarrier* barrier = nullptr) {
        auto* job = new JobWithData<DataT>{std::move(data),
                                            std::forward<FuncT>(func)};
        return schedule(std::unique_ptr<IJob>(job), barrier);
    }

    // Chunk-based parallel for
    template<typename FuncT>
    JobHandle scheduleParallelFor(u32 count, FuncT&& func, JobBarrier* barrier = nullptr);

    void waitAll();
    u32 workerCount() const { return m_workerCount; }

    struct Stats {
        u32 activeWorkers;
        u32 pendingJobs;
        u32 completedFrames;
    };
    Stats stats() const;

private:
    u32                      m_workerCount;
    std::vector<WorkerThread> m_workers;
    JobQueue                  m_globalQueue;
};

}  // namespace Caffeine::Threading
```

### 2.3 Exemplo de Uso

```cpp
// Física distribuída entre workers
JobBarrier barrier(particleCount / CHUNK_SIZE);
for (u32 i = 0; i < particleCount; i += CHUNK_SIZE) {
    jobSystem.scheduleData(
        ChunkJob{i, CHUNK_SIZE, dt},
        [&](const ChunkJob& j) {
            for (u32 k = j.start; k < j.start + j.count; ++k) {
                particles[k].integrate(dt);
            }
        },
        &barrier
    );
}
barrier.wait();  // Todos os chunks prontos
```

### 2.4 Decisões de Design

| Decisão | Justificativa |
|---|---|
| `std::unique_ptr<IJob>` | Ownership claro — sistema é dono do Job |
| Work-stealing deque | Melhora throughput vs fila centralizada |
| `index + version` no Handle | Evita ABA problem sem lock |
| Workers = n-1 | Main thread não compete por recursos com workers |

---

## 3. Input System

**Fase:** 2 — O Pulso e a Concorrência  
**Responsável:** Architects

### 3.1 Visão Geral

O Input System abstrai dispositivos (teclado, mouse, gamepad) e expõe **ações nomeadas** em vez de scancodes crus. Isso permite remapear controles sem alterar o código do jogo.

Baseado em pesquisa de padrões action-based input e integração com ECS.

### 3.2 Arquitetura

```
SDL Events
      │
      ▼
InputManager.poll()  ← Gameloop.processInput()
      │
      ├── KeyboardState    ──▶ ActionMap  ──▶ ActionState
      ├── MouseState       ──▶ AxisMap   ──▶ AxisState
      └── GamepadState[]   ──▶           ──▶ (fornecido ao ECS)
```

### 3.3 Interfaces

```cpp
namespace Caffeine::Input {

// ============================================================================
// @brief  Códigos de ação pré-definidos.
// ============================================================================
enum class Action : u16 {
    None = 0,
    Jump,
    Attack,
    Interact,
    Pause,
    MenuUp, MenuDown, MenuLeft, MenuRight,
    MenuConfirm, MenuBack,
    CUSTOM_START = 128
};

// ============================================================================
// @brief  Eixo analógico (mouse, joystick).
// ============================================================================
enum class Axis : u8 {
    None = 0,
    MoveX,    // -1.0 (esquerda) a +1.0 (direita)
    MoveY,    // -1.0 (cima) a +1.0 (baixo)
    LookX,    // Mouse X / Right stick X
    LookY,    // Mouse Y / Right stick Y
    CUSTOM_START = 64
};

// ============================================================================
// @brief  Estado de uma ação.
// ============================================================================
struct ActionState {
    bool pressed     = false;
    bool justPressed  = false;
    bool justReleased = false;
};

// ============================================================================
// @brief  Estado de um eixo analógico.
// ============================================================================
struct AxisState {
    f32 value = 0.0f;  // -1.0 a +1.0
    f32 delta = 0.0f;  // mudança desde o último frame
};

// ============================================================================
// @brief  Gerenciador de input.
//
//  Ciclo:
//  1. beginFrame() — clear justPressed/justReleased, processa SDL events
//  2. isActionPressed(Action::Jump) — consulta estado
//  3. endFrame() — atualiza história para próximo frame
// ============================================================================
class InputManager {
public:
    InputManager();
    ~InputManager();

    void beginFrame();
    void endFrame();

    const ActionState& actionState(Action a) const;
    const AxisState& axisState(Axis a) const;

    Vec2 mousePosition() const;
    Vec2 mouseDelta() const;
    void setMouseRelative(bool enabled);

    void bindAction(Action action, std::initializer_list<Binding> bindings);
    void bindAxis(Axis axis, std::initializer_list<Binding> bindings);

private:
    void processSdlEvent(const SDL_Event& e);

    HashMap<SDL_Scancode, Action> m_keyBindings;
    HashMap<u32, Action>          m_mouseBindings;
    HashMap<i32, Action>          m_gamepadBindings;
    HashMap<i32, Axis>            m_gamepadAxis;

    Array<ActionState, 64> m_actionStates;
    Array<AxisState, 32>  m_axisStates;
    Array<bool, 64>        m_prevActionStates;

    Vec2 m_mousePos;
    Vec2 m_mouseDelta;
    bool m_mouseRelative = false;
};

// Estrutura para binding de input
struct Binding {
    enum class Type { Key, MouseButton, GamepadButton, GamepadAxis } type;
    u32 code;     // scancode, button, ou axis
    f32 scale = 1.0f;  // para gamepad axis
};

}  // namespace Caffeine::Input
```

### 3.4 Componente ECS para Input

```cpp
// ============================================================================
// @brief  Componente que permite a uma entidade reagir a input.
// ============================================================================
struct PlayerInput {
    Action moveAction = Action::None;
    Action jumpAction = Action::None;
    Axis   moveAxisX  = Axis::MoveX;
    Axis   moveAxisY  = Axis::MoveY;
    f32    moveSpeed   = 5.0f;
    f32    jumpForce   = 10.0f;
};
```

### 3.5 Decisões de Design

| Decisão | Justificativa |
|---|---|
| Action-based, não scancode | Games portáteis; remapping em runtime |
| `justPressed`/`justReleased` | Elimina polling no update |
| Binding map mutável | Remapping em runtime (menu de opções) |
| Mouse relative mode | Mouse look para câmera sem drift |

---

## 4. ECS Core

**Fase:** 4 — O Cérebro  
**Responsável:** Architects

### 4.1 Visão Geral

O ECS (Entity Component System) da Caffeine é **archetype-based** — entidades com o mesmo conjunto de componentes vivem em memória contígua (archetypes). Isso maximiza cache locality e permite SIMD nos systems.

Inspirado em [flecs](https://github.com/SanderMertens/flecs) e [EnTT](https://github.com/skypjack/entt). Ver [EnTT in Action](https://github.com/skypjack/entt/wiki/EnTT-in-Action).

### 4.2 Conceitos Fundamentais

```
┌──────────────────────────────────────────────────────────────┐
│                    ECS KEY CONCEPTS                          │
│                                                              │
│  ENTITY:  u32 ID único. Não tem dados nem lógica.           │
│           bits: [archetype_index] [entity_index]              │
│                                                              │
│  COMPONENT: Struct POD. Sem métodos, sem virtual.             │
│             Ex: Position { f32 x, y; }                       │
│                                                              │
│  ARCHETYPE: Grupo de entidades com mesmo ComponentSet.       │
│             Arrays contíguos de cada componente.               │
│                                                              │
│  SYSTEM: Função que itera sobre entidades que combinam        │
│          um ComponentQuery. Roda no Job System.                │
│                                                              │
│  WORLD: Container que gerencia todas as entidades,            │
│         componentes e archetypes.                             │
└──────────────────────────────────────────────────────────────┘
```

### 4.3 Interfaces

```cpp
namespace Caffeine::ECS {

// ----------------------------------------------------------------------------
// @brief  Handle para uma entidade.
// ----------------------------------------------------------------------------
class Entity {
public:
    Entity() : m_id(u32_max), m_world(nullptr) {}
    Entity(u32 id, World* world) : m_id(id), m_world(world) {}

    u32 id() const { return m_id; }
    bool isValid() const;

    template<typename T, typename... Args>
    T& add(Args&&... args);

    template<typename T>
    void remove();

    template<typename T>
    bool has() const;

    template<typename T>
    T* get();

    void destroy();

    explicit operator bool() const { return isValid(); }
    bool operator==(Entity other) const { return m_id == other.m_id; }

private:
    u32    m_id;
    World* m_world;
};

// ----------------------------------------------------------------------------
// @brief  Query — define quais componentes uma entidade deve ter.
// ----------------------------------------------------------------------------
class ComponentQuery {
public:
    ComponentQuery& with(auto&&... components);
    ComponentQuery& without(auto&&... components);

    template<typename Func>
    void forEach(World& world, Func&& func) const;

    template<typename Func>
    void forEachParallel(World& world, Func&& func, JobBarrier* barrier) const;

private:
    struct Clause {
        std::vector<ComponentID> includes;
        std::vector<ComponentID> excludes;
    };
    Clause m_clause;
};

// ----------------------------------------------------------------------------
// @brief  System — functor que opera sobre um query.
// ----------------------------------------------------------------------------
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(World& world, f64 dt) = 0;
    virtual i32 priority() const = 0;
    virtual const char* name() const = 0;
};

// ----------------------------------------------------------------------------
// @brief  World — gerenciador principal do ECS.
// ----------------------------------------------------------------------------
class World {
public:
    World();
    ~World();

    // Entity lifecycle
    Entity create(const char* name = nullptr);
    void   destroy(Entity e);
    void   destroyAll();

    // Component access
    template<typename T, typename... Args>
    T& add(Entity e, Args&&... args);

    template<typename T>
    void remove(Entity e);

    template<typename T>
    bool has(Entity e) const;

    template<typename T>
    T* get(Entity e);

    // Queries
    template<typename Func>
    void query(ComponentQuery& q, Func&& func) const;

    // Systems
    template<typename SystemT, typename... Args>
    void registerSystem(const char* name, i32 priority, Args&&... args);

    void unregisterSystem(const char* name);

    // Frame update
    void update(f64 dt);

    u32 entityCount() const;
    u32 archetypeCount() const;

private:
    Archetype* getOrCreateArchetype(ComponentSet set);

    Array<Archetype, 256>       m_archetypes;
    HashMap<ComponentSet, u32>  m_archetypeIndex;
    u32                           m_entityCount    = 0;
    u32                           m_archetypeCount = 0;
    std::vector<Own<ISystem>>    m_systems;
};

}  // namespace Caffeine::ECS
```

### 4.4 Componentes Pré-definidos

```cpp
namespace Caffeine::Components {

// Transform 2D
struct Position2D { f32 x = 0, y = 0; };
struct Velocity2D  { f32 x = 0, y = 0; };
struct Rotation2D   { f32 angle = 0; };

// Sprite
struct Sprite {
    u32   textureId     = u32_max;
    Vec2  offset       = {0, 0};
    Vec2  scale        = {1, 1};
    f32   rotation     = 0;
    Color tint         = Color::WHITE;
    i32   layer       = 0;
    bool  flipX        = false;
    bool  flipY        = false;
};

// Rigid Body 2D
struct RigidBody2D {
    f32 mass        = 1.0f;
    f32 restitution = 0.5f;
    f32 friction    = 0.3f;
    Vec2 acceleration = {0, 0};
    bool isKinematic = false;
};

// AABB Collider
struct Collider2D {
    Vec2  size     = {1, 1};
    Vec2  offset   = {0, 0};
    bool  isStatic = false;
    u32   layer    = 0;
    bool  isTrigger = false;
};

// Tag components
struct Player     {};
struct Enemy      {};
struct Projectile {};
struct Particle   {};
struct Disabled   {};  // entidades com isso são puladas
struct Destroy   {};  // marcada para destruição no fim do frame

// Name
struct Name {
    FixedString<32> value;
};

}  // namespace Caffeine::Components
```

### 4.5 Exemplo de Uso

```cpp
World world;

Entity player = world.create("Player");
world.add<Position2D>(player, {100, 200});
world.add<Velocity2D>(player, {0, 0});
world.add<Sprite>(player, { .textureId = playerTex });
world.add<PlayerInput>(player);
world.add<RigidBody2D>(player, { .mass = 5.0f });
world.add<Collider2D>(player, { .size = {32, 64} });

// Queries
ComponentQuery movementQuery;
movementQuery.with<Position2D, Velocity2D>();
movementQuery.without<Disabled, Destroy>();

world.query(movementQuery, [](Position2D& pos, const Velocity2D& vel, f64 dt) {
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
});

// Systems — registrados em ordem de prioridade
world.registerSystem<PhysicsSystem>("Physics", 100);
world.registerSystem<AnimationSystem>("Animation", 200);
world.registerSystem<RenderSystem>("Render", 300);

while (running) {
    world.update(fixedDt);
}
```

---

## 5. Scene Graph & Serialização

**Fase:** 4 — O Cérebro  
**Responsável:** Architects

### 5.1 Visão Geral

O Scene Graph organiza entidades em hierarquia (parent/child) para transformações locais. A serialização permite salvar/carregar cenas em formato binário `.caf` ou JSON.

### 5.2 Interfaces

```cpp
namespace Caffeine::Scene {

// ============================================================================
// @brief  Componente de parent/child para hierarquia.
// ============================================================================
struct Parent {
    Entity parent = Entity::INVALID;
    bool   dirty = true;  // world transform precisa recalcular
};

// ============================================================================
// @brief  Transform global (world space) — calculado a partir de Local.
// ============================================================================
struct WorldTransform {
    Mat4 matrix = Mat4::identity();
};

// ============================================================================
// @brief  Serializador para Caffeine Object Notation (.caf).
//
//  Formato:
//
//  [Header: "CAFF" + version + crc32]
//  [Entity count]
//  [Archetypes]
//  [Entity data (binary)]
//  [Strings table]
//  [Asset references]
// ============================================================================
class SceneSerializer {
public:
    explicit SceneSerializer(World& world);

    bool serialize(const char* path) const;
    bool deserialize(const char* path);
    bool serializeJson(const char* path) const;
    bool deserializeJson(const char* path);

private:
    World& m_world;
};

// ============================================================================
// @brief  Gerenciador de cenas.
// ============================================================================
class SceneManager {
public:
    enum class TransitionType { None, Fade, Slide, Custom };

    LoadHandle loadScene(const char* path, bool async = false);
    void switchScene(const char* path,
                     TransitionType trans = TransitionType::None,
                     f32 duration = 0.0f);
    void pushScene(const char* path);
    void popScene();

    World* activeWorld() const { return m_activeWorld; }

private:
    World*                        m_activeWorld = nullptr;
    std::vector<Own<World>>       m_sceneStack;
};

}  // namespace Caffeine::Scene
```

### 5.3 CAF Format (Binary)

```
Offset  Size  Description
------  ----  -----------
0       4     Magic: "CAFF" (0x43414646)
4       2     Version major (u16)
6       2     Version minor (u16)
8       4     CRC32 of entire file (except this field)
12      4     Entity count (u32)
16      4     Archetype count (u32)
20      N     Archetype table
...     ...   Entity data (packed by archetype)
...     ...   Strings table
...     ...   Asset reference table
EOF     4     CRC32 of entire file
```

---

## 6. Event Bus

**Fase:** 4 — O Cérebro  
**Responsável:** Architects

### 6.1 Visão Geral

O Event Bus permite comunicação **desacoplada** entre sistemas. Um sistema publica eventos; outros se inscrevem sem conhecer quem publica.

### 6.2 Interfaces

```cpp
namespace Caffeine::Events {

// ============================================================================
// @brief  Evento base — todos os eventos derivam deste.
//
//  Eventos são structs POD com dados relevantes. Ex:
//
//  struct CollisionEvent {
//      Entity a;
//      Entity b;
//      Vec2 contactPoint;
//      Vec2 normal;
//  };
// ============================================================================
struct IEvent {
    virtual ~IEvent() = default;
    virtual u32 typeId() const = 0;
};

template<typename T>
u32 eventTypeId() {
    static char unique = 0;
    return reinterpret_cast<u32>(&unique);
}

using EventListener = std::function<void(const IEvent&)>;

// ============================================================================
// @brief  Barramento de eventos. Padrão pub/sub.
// ============================================================================
class EventBus {
public:
    EventBus();
    ~EventBus();

    template<typename EventT>
    ListenerHandle subscribe(EventListener listener);

    void unsubscribe(ListenerHandle handle);

    template<typename EventT>
    void publish(const EventT& event);

    void clear();
    u32 pendingCount() const;

private:
    struct ListenerRecord {
        ListenerHandle handle;
        EventListener  callback;
        bool           valid = true;
    };

    HashMap<u32, std::vector<ListenerRecord>> m_listeners;
    std::vector<std::unique_ptr<IEvent>>       m_eventQueue;
    ListenerHandle                             m_nextHandle = 1;
};

}  // namespace Caffeine::Events
```

### 6.3 Eventos Pré-definidos

```cpp
namespace Caffeine::Events {

struct OnEntityCreated    { Entity entity; };
struct OnEntityDestroyed  { Entity entity; };

struct OnCollision2D {
    Entity a, b;
    Vec2   contactPoint;
    Vec2   normal;
    f32    penetration;
};

struct OnHealthChanged {
    Entity entity;
    f32    oldHealth;
    f32    newHealth;
};

struct OnDeath {
    Entity entity;
    Entity killer;
};

struct OnLevelLoaded {
    FixedString<64> levelName;
};

}  // namespace Caffeine::Events
```

---

## 7. Asset Manager

**Fase:** 3 — O Olho da Engine  
**Responsável:** Artisans

### 7.1 Visão Geral

O Asset Manager carrega, cache e fornece assets (texturas, audio, shaders, modelos) de forma transparente. Suporta **async loading** via Job System e **hot-reload** em desenvolvimento.

### 7.2 Interfaces

```cpp
namespace Caffeine::Assets {

enum class AssetType : u8 {
    Texture, Audio, Shader, Font, Scene, Mesh, Animation
};

enum class LoadStatus : u8 {
    Unloaded, Loading, Loaded, Failed
};

// ============================================================================
// @brief  Handle para um asset carregado.
// ============================================================================
template<typename T>
class AssetHandle {
public:
    AssetHandle() = default;
    explicit AssetHandle(u32 id, AssetManager* manager);

    T* get() const;
    bool isReady() const;
    bool hasFailed() const;
    f32  progress() const;
    T* wait() const;

    explicit operator bool() const { return isReady(); }
    T* operator->() const { return get(); }

private:
    u32 m_id;
    AssetManager* m_manager;
};

// ============================================================================
// @brief  Gerenciador de assets.
//
//  - Cache em memória (LRU eviction quando cheio)
//  - Async loading via Job System
//  - Hot-reload em dev mode
//  - Reference counting
// ============================================================================
class AssetManager {
public:
    AssetManager(const char* basePath, u64 cacheSizeMB = 256);
    ~AssetManager();

    template<typename T>
    AssetHandle<T> loadAsync(const char* path);

    template<typename T>
    T* loadSync(const char* path);

    void unload(AssetHandleBase handle);
    void collectGarbage();
    void pollFileChanges();

    struct CacheStats {
        u64 totalCachedBytes;
        u64 maxCacheBytes;
        u32 textureCount;
        u32 audioCount;
    };
    CacheStats stats() const;

private:
    Asset* loadSyncInternal(const char* path, AssetType type);

    const char*        m_basePath;
    u64                 m_maxCacheBytes;

    HashMap<u64, Asset*> m_textureCache;
    HashMap<u64, Asset*> m_audioCache;
    HashMap<u64, Asset*> m_shaderCache;
    HashMap<u64, u32>    m_refCounts;

    bool m_watchEnabled = false;
};

}  // namespace Caffeine::Assets
```

### 7.3 Componente ECS para Assets

```cpp
// ============================================================================
// @brief  Componente que mantém uma referência a um asset.
// ============================================================================
template<typename T>
struct AssetRef {
    FixedString<128> path;
    AssetHandle<T>   handle;
    bool             autoLoad = true;
};
```

---

## 8. RHI (Rendering Hardware Interface)

**Fase:** 3 — O Olho da Engine  
**Responsável:** Artisans

### 8.1 Visão Geral

O RHI é a camada de abstração sobre o SDL_GPU. A engine nunca chama funções SDL diretamente — todas as chamadas passam por `RHI`, que gerencia command buffers, swapchains e render targets.

### 8.2 Interfaces

```cpp
namespace Caffeine::RHI {

// ============================================================================
// @brief  Inicializa o RHI com contexto SDL_GPU.
// ============================================================================
class RenderDevice {
public:
    bool init(SDL_Window* window, const RenderConfig& config);

    CommandBuffer* beginFrame();
    void           endFrame(CommandBuffer* cmd);
    void           present(CommandBuffer* cmd);

    Texture* createTexture(const TextureDesc& desc);
    Shader*  createShader(const ShaderDesc& desc);
    Buffer*  createBuffer(const BufferDesc& desc, BufferUsage usage);
    void     destroyTexture(Texture* tex);
    void     destroyShader(Shader* shader);
    void     destroyBuffer(Buffer* buf);

    GPUTimestamps* createTimestamps(u32 maxEntries);

private:
    SDL_GPUDevice*    m_device    = nullptr;
    SDL_GPUSwapchain* m_swapchain = nullptr;
    CommandBuffer*     m_cmdBuffers[3];  // triple buffering
    u32                m_frameIndex = 0;
};

// ============================================================================
// @brief  Command buffer — commands enfileirados para a GPU.
// ============================================================================
class CommandBuffer {
public:
    void beginRenderPass(const RenderPassDesc& desc);
    void endRenderPass();

    void bindPipeline(Pipeline* pipeline);
    void bindVertexBuffer(Buffer* buf, u32 slot = 0);
    void bindIndexBuffer(Buffer* buf);
    void bindTexture(Texture* tex, u32 slot);

    void draw(u32 vertexCount, u32 firstVertex = 0);
    void drawIndexed(u32 indexCount, u32 firstIndex = 0, i32 vertexOffset = 0);
    void drawInstanced(u32 vertexCount, u32 instanceCount,
                       u32 firstVertex = 0, u32 firstInstance = 0);

    void barrier(const BarrierDesc& desc);
    void writeTimestamp(GPUTimestamps* pool, u32 index);
};

// ============================================================================
// @brief  Draw command.
// ============================================================================
struct DrawCommand {
    Pipeline* pipeline = nullptr;
    Buffer*   vertices = nullptr;
    Buffer*   indices = nullptr;
    u32       indexCount = 0;
    u32       firstIndex = 0;
    Mat4      transform = Mat4::identity();
    Vec4      tint = Vec4(1, 1, 1, 1);
    Texture*  texture = nullptr;
    i32       sortKey = 0;
};

}  // namespace Caffeine::RHI
```

---

## 9. Batch Renderer

**Fase:** 3 — O Olho da Engine  
**Responsável:** Artisans

### 9.1 Visão Geral

O Batch Renderer agrupamenta milhares de sprites em um único draw call. Em vez de enviar 10.000 draw calls para a GPU, enviamos 1.

### 9.2 Interfaces

```cpp
namespace Caffeine::Render {

// ============================================================================
// @brief  Sprite — componente visual 2D.
// ============================================================================
struct Sprite {
    FixedString<128> texturePath;
    Rect2D           srcRect     = {{0,0}, {0,0}};
    Vec2             origin      = {0.5f, 0.5f};
    Vec2             scale       = {1, 1};
    f32              rotation    = 0;
    Color            tint        = Color::WHITE;
    i32              layer       = 0;
};

// ============================================================================
// @brief  Sistema de Batch Rendering para sprites 2D.
//
//  Vertex format (24 bytes por vertex):
//  offset 0:  Vec3  position  (12 bytes)
//  offset 12: Vec2  texcoord   (8 bytes)
//  offset 20: Color tint      (4 bytes, packed RGBA8)
//                      TOTAL: 24 bytes (align 4)
//
//  Capacidade:
//  - Max sprites por batch: 32K
//  - Max batches por frame: ilimitado
// ============================================================================
class BatchRenderer {
public:
    BatchRenderer(RenderDevice* device);
    ~BatchRenderer();

    void beginFrame();
    void endFrame(CommandBuffer* cmd);

    void submitSprite(const Mat4& worldTransform, const Sprite& sprite);
    void flushCurrentBatch(CommandBuffer* cmd);

    void setCamera(const Camera& camera);

    struct FrameStats {
        u32 totalSprites;
        u32 totalBatches;
        u32 drawCalls;
        u32 verticesUploaded;
    };
    FrameStats lastFrameStats() const { return m_stats; }

private:
    void flushBatch(CommandBuffer* cmd, Batch& batch);

    RenderDevice*  m_device;
    Camera         m_camera;
    HashMap<u64, Batch> m_batches;
    FrameStats     m_stats;
};

// ============================================================================
// @brief  Texture Atlas — agrupa múltiplas texturas em uma só imagem.
// ============================================================================
class TextureAtlas {
public:
    TextureAtlas(u32 width, u32 height);
    ~TextureAtlas();

    bool add(const char* name, Rect2D region);
    void pack();
    Buffer exportImage() const;
    Vec4 getUV(const char* name) const;
    u32  usedArea() const;

private:
    struct Region {
        FixedString<64> name;
        Rect2D          rect;
    };
    std::vector<Region> m_regions;
    u32 m_width, m_height;
};

}  // namespace Caffeine::Render
```

---

## 10. Camera System

**Fase:** 3 — O Olho da Engine  
**Responsável:** Artisans

### 10.1 Interfaces

```cpp
namespace Caffeine::Render {

// ============================================================================
// @brief  Câmera 2D (projeção ortográfica).
//
//  Para 3D futuro: criar Camera3D com projeção perspectiva.
//  A interface será similar, permitindo trocar cameras sem quebrar código.
// ============================================================================
class Camera2D {
public:
    Camera2D();

    Mat4 viewMatrix() const;
    Mat4 projectionMatrix() const;
    Mat4 viewProjectionMatrix() const;

    Vec2 worldToScreen(Vec2 worldPos) const;
    Vec2 screenToWorld(Vec2 screenPos) const;

    void setPosition(Vec2 pos);
    void setZoom(f32 zoom);
    void setRotation(f32 degrees);
    void setViewport(Rect2D viewport);
    void setAspectRatio(f32 aspect);

    void follow(Entity target, f32 smoothing = 0.1f);
    void shake(Vec2 intensity, f32 duration);
    void setBounds(Rect2D worldBounds);

    Vec2   position() const { return m_position; }
    f32    zoom() const { return m_zoom; }
    f32    rotation() const { return m_rotation; }
    Rect2D viewport() const { return m_viewport; }

private:
    Mat4  calculateViewMatrix() const;
    Mat4  calculateProjectionMatrix() const;

    Vec2   m_position   = {0, 0};
    f32    m_zoom       = 1.0f;
    f32    m_rotation   = 0.0f;
    Rect2D m_viewport   = {0, 0, 1280, 720};
    f32    m_aspect     = 1280.0f / 720.0f;

    Entity m_followTarget    = Entity::INVALID;
    f32    m_followSmoothing = 0.1f;

    Vec2   m_shakeIntensity = {0, 0};
    f32    m_shakeTime      = 0;
    f32    m_shakeDuration  = 0;

    bool   m_hasBounds    = false;
    Rect2D m_worldBounds   = {{0, 0}, {0, 0}};

    mutable Mat4 m_cachedViewProj;
    mutable bool m_dirty = true;
};

}  // namespace Caffeine::Render
```

---

## 11. Audio System

**Fase:** 4 — O Cérebro  
**Responsável:** Artisans

### 11.1 Visão Geral

O Audio System usa SDL_AudioStream para streaming de música e pré-carregamento de SFX. Suporta posicionamento 2D (pan, volume baseado em distância).

Baseado em pesquisa da API SDL3 audio.

### 11.2 Interfaces

```cpp
namespace Caffeine::Audio {

// ============================================================================
// @brief  Source de áudio — uma instância de som tocando.
// ============================================================================
class AudioSource {
public:
    void play();
    void pause();
    void stop();
    void setVolume(f32 volume);    // 0.0 a 1.0
    void setPan(f32 pan);          // -1.0 (left) a +1.0 (right)
    void setPitch(f32 pitch);     // 0.5 a 2.0
    void setLoop(bool loop);

    bool isPlaying() const;
    f32  currentTime() const;
    f32  duration() const;

private:
    SDL_AudioStream* m_stream = nullptr;
    AudioClip*        m_clip   = nullptr;
    f32               m_volume = 1.0f;
    f32               m_pan    = 0.0f;
    bool              m_loop   = false;
};

// ============================================================================
// @brief  Sistema de áudio.
//
//  Usa SDL_AudioStream para streaming de música e pré-carregamento de SFX.
//  Suporta posicionamento 2D (pan, volume baseado em distância).
//
//  Memory:
//  - SFX: carregados inteiros no PoolAllocator (audio buffer)
//  - Música: streaming (buffers parciais, 64KB chunks)
// ============================================================================
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool init(u32 sfxChannelCount, u32 musicChannelCount);

    AudioClip* loadClip(const char* path);
    AudioSource* playSFX(AudioClip* clip, f32 volume = 1.0f, f32 pan = 0.0f);

    void playMusic(const char* path, f32 volume = 1.0f, bool loop = true);
    void stopMusic();
    void fadeMusic(f32 fadeOutSecs, const char* nextPath, f32 fadeInSecs);

    void setSourcePosition(AudioSource* source, const Vec2& worldPos,
                           const Vec2& listenerPos);
    void setListenerPosition(const Vec2& pos, const Vec2& forward);

    void setMasterPause(bool paused);
    void setMasterVolume(f32 volume);

    void update(f64 dt);

private:
    SDL_AudioDeviceID m_device;
    std::vector<Own<AudioSource>> m_sfxPool;
    AudioSource*                    m_musicSource = nullptr;
    HashMap<u64, Own<AudioClip>>    m_clipCache;
    Vec2                            m_listenerPos = {0, 0};
    bool                            m_masterPaused = false;
    f32                             m_masterVolume = 1.0f;
};

}  // namespace Caffeine::Audio
```

---

## 12. Animation System

**Fase:** 4 — O Cérebro  
**Responsável:** Artisans

### 12.1 Visão Geral

Sistema de animação 2D com clipes de sprite e state machine. Futuro: skeletal animation com bone transforms.

Baseado em pesquisa de padrões de animação em ECS.

### 12.2 Interfaces

```cpp
namespace Caffeine::Animation {

// ============================================================================
// @brief  Clip de animação — sequência de frames.
// ============================================================================
struct AnimationClip {
    FixedString<32>     name;
    u32                 fps          = 12;
    std::vector<Rect2D> frames;
    bool                loop         = true;
    f32 duration() const { return (f32)frames.size() / (f32)fps; }
};

// ============================================================================
// @brief  State machine de animação.
// ============================================================================
struct AnimationState {
    FixedString<32>        name;
    const AnimationClip*   clip    = nullptr;
    std::vector<Transition> transitions;

    struct Transition {
        FixedString<32>  toState;
        std::function<bool()> condition;
        f32              blendTime = 0.1f;  // crossfade
    };
};

// ============================================================================
// @brief  Componente de animação attaché a uma entidade.
// ============================================================================
struct Animator {
    HashMap<FixedString<32>, AnimationState> states;
    FixedString<32>                          currentState;
    FixedString<32>                          previousState;
    f32                                      timeInState   = 0;
    f32                                      blendWeight   = 1.0f;
    f32                                      playbackScale = 1.0f;
    bool                                     paused        = false;

    std::vector<std::pair<u32, FixedString<32>>> events;
    std::function<void(const FixedString<32>&)> onEvent;
};

// ============================================================================
// @brief  Sistema de animação ECS.
// ============================================================================
class AnimationSystem : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32 priority() const override { return 200; }
    const char* name() const override { return "Animation"; }

    void play(ECS::Entity e, const char* stateName, f32 blendTime = 0.1f);
    void pause(ECS::Entity e);
    void resume(ECS::Entity e);
    void setSpeed(ECS::Entity e, f32 speed);
};

}  // namespace Caffeine::Animation
```

---

## 13. Physics (2D)

**Fase:** 4 — O Cérebro  
**Responsável:** Artisans

### 13.1 Visão Geral

Sistema de física 2D simples com suporte a:
- Rigid body dynamics (massa, velocidade, força)
- AABB e circle collision detection
- Static e dynamic bodies
- Collision layers e masks
- Simple collision response (restitution + friction)

Baseado em pesquisa de [Box2D/LiquidFun](https://github.com/google/liquidfun).

### 13.2 Interfaces

```cpp
namespace Caffeine::Physics2D {

// ============================================================================
// @brief  Sistema de física 2D simples.
//
//  NÃO suporta (para futuro com biblioteca dedicada):
//  - Rotação de rigid bodies
//  - Joints/constraints
//  - Complex shapes (convex hulls)
//
//  Integração com ECS:
//  - Lê RigidBody2D, Collider2D, Position2D, Velocity2D
//  - Escreve Position2D, Velocity2D
//  - Publica OnCollision2D events
// ============================================================================
class PhysicsSystem2D : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32 priority() const override { return 100; }
    const char* name() const override { return "Physics2D"; }

    void setGravity(Vec2 gravity) { m_gravity = gravity; }
    Vec2 gravity() const { return m_gravity; }

    const RaycastHit* raycast(Vec2 origin, Vec2 dir, f32 maxDist) const;
    Array<ECS::Entity, 32> overlapCircle(Vec2 center, f32 radius) const;
    Array<ECS::Entity, 32> overlapAABB(Rect2D rect) const;

    void applyForce(ECS::Entity e, Vec2 force);
    void applyImpulse(ECS::Entity e, Vec2 impulse);
    void setKinematic(ECS::Entity e, bool kinematic);

private:
    void integrate(ECS::Entity e, f32 dt);
    bool detectCollision(ECS::Entity a, ECS::Entity b, CollisionManifold* out);
    void resolveCollision(ECS::Entity a, ECS::Entity b, const CollisionManifold& m);

    Vec2 m_gravity = {0, -9.81f * 60.0f};  // pixels/s²
    HashMap<ECS::Entity, Vec2> m_pendingForces;
};

}  // namespace Caffeine::Physics2D
```

---

## 14. UI System

**Fase:** 5 — O Olimpo  
**Responsável:** Full Guild

### 14.1 Visão Geral

Sistema de UI retido (retained mode) — widgets são entidades ECS com componentes de UI. Isso permite que UI seja afetada por ECS systems (ex: HealthBar reflete o valor de `Health` component).

### 14.2 Interfaces

```cpp
namespace Caffeine::UI {

// Layout rect em pixels ou fractions (0-1 do parent)
struct RectTransform {
    Vec2 anchorMin = {0, 0};
    Vec2 anchorMax = {1, 1};
    Vec2 pivot     = {0.5f, 0.5f};
    Rect2D offset  = {};
};

struct UIStyle {
    Color  backgroundColor = {0.1f, 0.1f, 0.1f, 0.9f};
    Color  textColor      = Color::WHITE;
    Color  borderColor    = {0.3f, 0.3f, 0.3f, 1.0f};
    f32    borderWidth    = 1.0f;
    f32    borderRadius   = 4.0f;
    Font*  font          = nullptr;
    f32    fontSize       = 16.0f;
    Vec2   textAlignment  = {0.5f, 0.5f};
};

struct UIWidget {
    enum class Type : u8 { Canvas, Panel, Button, Label,
                           ProgressBar, Checkbox, Slider };
    Type        type        = Type::Panel;
    bool        visible     = true;
    bool        interactable = true;
    i32         siblingOrder = 0;
    UIStyle     style;
    RectTransform transform;

    std::function<void(ECS::Entity)> onClick;
    std::function<void(ECS::Entity)> onHover;
    std::function<void(ECS::Entity)> onValueChanged;
};

struct Button : UIWidget {
    FixedString<64> labelText;
    Color idleColor    = {0.2f, 0.2f, 0.2f, 1.0f};
    Color hoverColor   = {0.3f, 0.3f, 0.3f, 1.0f};
    Color pressedColor = {0.1f, 0.1f, 0.1f, 1.0f};
};

struct Label : UIWidget {
    FixedString<256> text;
    bool              wordWrap = false;
};

struct ProgressBar : UIWidget {
    f32 minValue = 0.0f;
    f32 maxValue = 100.0f;
    f32 currentValue = 50.0f;
    bool showText = true;
};

// ============================================================================
// @brief  Sistema de UI.
//
//  Memory: Widgets alocados no PoolAllocator (tamanho fixo por tipo).
// ============================================================================
class UISystem : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32 priority() const override { return 500; }
    const char* name() const override { return "UI"; }

    ECS::Entity createCanvas(ECS::World& world);
    ECS::Entity createButton(ECS::World& world, ECS::Entity parent,
                             const char* text, Vec2 pos);
    ECS::Entity createLabel(ECS::World& world, ECS::Entity parent,
                             const char* text, Vec2 pos);
    ECS::Entity createProgressBar(ECS::World& world, ECS::Entity parent,
                                   Vec2 pos, Vec2 size);

    void bindComponent(ECS::Entity widget, ECS::Entity target,
                        ECS::ComponentID component, const char* field);
};

}  // namespace Caffeine::UI
```

---

## 15. Math Library

**Fase:** 1+ (incremental)  
**Responsável:** Architects

### 15.1 Interfaces

```cpp
namespace Caffeine::Math {

// ============================================================================
// @brief  Vetor 2D com operadores completos.
// ============================================================================
struct Vec2 {
    f32 x, y;

    Vec2() : x(0), y(0) {}
    Vec2(f32 x, f32 y) : x(x), y(y) {}

    static Vec2 zero()   { return {0, 0}; }
    static Vec2 one()   { return {1, 1}; }
    static Vec2 up()    { return {0, 1}; }
    static Vec2 right() { return {1, 0}; }

    f32    length()     const;
    f32    lengthSq()  const { return x*x + y*y; }
    Vec2   normalized() const;
    f32    dot(Vec2 o)  const { return x*o.x + y*o.y; }
    f32    cross(Vec2 o) const { return x*o.y - y*o.x; }

    Vec2  operator+(Vec2 o) const { return {x+o.x, y+o.y}; }
    Vec2  operator-(Vec2 o) const { return {x-o.x, y-o.y}; }
    Vec2  operator*(f32 s)  const { return {x*s, y*s}; }
    Vec2  operator/(f32 s)  const { return {x/s, y/s}; }

    Vec2  xx() const { return {x, x}; }
    Vec2  yy() const { return {y, y}; }
    Vec2  xy() const { return {x, y}; }
    Vec2  yx() const { return {y, x}; }
};

// ============================================================================
// @brief  Vetor 3D.
// ============================================================================
struct Vec3 {
    f32 x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}

    f32    length()     const;
    Vec3   normalized() const;
    f32    dot(Vec3 o)   const { return x*o.x + y*o.y + z*o.z; }
    Vec3   cross(Vec3 o)  const;
    static Vec3 lerp(Vec3 a, Vec3 b, f32 t);

    Vec3  operator+(Vec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3  operator-(Vec3 o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3  operator*(f32 s)  const { return {x*s, y*s, z*s}; }

    Vec2  xy() const { return {x, y}; }
};

// ============================================================================
// @brief  Vetor 4D (SIMD-ready).
// ============================================================================
struct alignas(16) Vec4 {
    f32 x, y, z, w;

    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
    Vec4(Vec3 v, f32 w) : x(v.x), y(v.y), z(v.z), w(w) {}

    Vec3 xyz() const { return {x, y, z}; }
};

// ============================================================================
// @brief  Matriz 4×4 column-major (OpenGL-style).
// ============================================================================
struct alignas(16) Mat4 {
    f32 m[16];  // column-major

    Mat4();
    static Mat4 identity();
    static Mat4 translate(f32 x, f32 y, f32 z = 0);
    static Mat4 translate(Vec3 t);
    static Mat4 rotateZ(f32 angle);
    static Mat4 rotateY(f32 angle);  // Fase 5
    static Mat4 rotateX(f32 angle);  // Fase 5
    static Mat4 scale(f32 x, f32 y, f32 z = 1.0f);
    static Mat4 scale(Vec3 s);
    static Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top,
                       f32 near, f32 far);
    static Mat4 perspective(f32 fovY, f32 aspect,
                             f32 near, f32 far);  // Fase 5
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);  // Fase 5

    Mat4  operator*(Mat4 o) const;
    Vec4  operator*(Vec4 v) const;
    Vec3  transformPoint(Vec3 p) const;
    Vec3  transformVector(Vec3 v) const;
    Mat4  transposed() const;
    Mat4  inverse() const;
    f32   determinant() const;

    f32*        data() { return m; }
    const f32* data() const { return m; }
    f32& at(u32 row, u32 col) { return m[col * 4 + row]; }
};

// ============================================================================
// @brief  Quaternion para rotações 3D (Fase 5).
// ============================================================================
struct Quat {
    f32 x, y, z, w;  // w = scalar part

    static Quat identity() { return {0, 0, 0, 1}; }
    static Quat fromAxisAngle(Vec3 axis, f32 angle);
    static Quat fromEuler(f32 pitch, f32 yaw, f32 roll);
    static Quat lookAt(Vec3 forward, Vec3 up);
    static Quat slerp(Quat a, Quat b, f32 t);

    Vec3 toEuler() const;
    Mat4 toMatrix() const;
    Vec3 rotate(Vec3 v) const;

    f32  length()     const;
    Quat normalized()  const;
    Quat conjugate()   const { return {-x, -y, -z, w}; }
    Quat inverse()    const;

    Quat  operator*(Quat o) const;
    Vec3  operator*(Vec3 v) const { return rotate(v); }
};

// ============================================================================
// @brief  Utility functions.
// ============================================================================
f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
f32 clamp(f32 v, f32 min, f32 max);
f32 smoothstep(f32 edge0, f32 edge1, f32 x);
f32 distance(Vec2 a, Vec2 b) { return (a - b).length(); }
f32 distance(Vec3 a, Vec3 b) { return (a - b).length(); }
f32 dot(Vec2 a, Vec2 b) { return a.x*b.x + a.y*b.y; }
f32 dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

}  // namespace Caffeine::Math
```

---

## 16. Debug/Development Tools

**Fase:** 2+  
**Responsável:** All

### 16.1 Visão Geral

Ferramentas de desenvolvimento sempre disponíveis para profiling e debugging.

### 16.2 Interfaces

```cpp
namespace Caffeine::Debug {

enum class LogLevel { Trace, Info, Warn, Error, Fatal };

// ============================================================================
// @brief  Sistema de logging categorizado.
// ============================================================================
class LogSystem {
public:
    void log(LogLevel level, const char* category, const char* message, ...);

    void setLevel(LogLevel minLevel);
    void setCategoryEnabled(const char* category, bool enabled);
};

// ============================================================================
// @brief  Profiler com markers.
// ============================================================================
class Profiler {
public:
    void beginScope(const char* name);
    void endScope();

    struct ScopeStats {
        const char* name;
        u64 callCount;
        f64 totalMs;
        f64 avgMs;
        f64 minMs;
        f64 maxMs;
    };

    void report(Caffeine::Array<ScopeStats, 256>& out);
    void reset();
};

// ============================================================================
// @brief  Debug draw — visualize dados no mundo.
// ============================================================================
class DebugDraw {
public:
    void line(Vec2 a, Vec2 b, Color color);
    void rect(Rect2D rect, Color color);
    void circle(Vec2 center, f32 radius, Color color);
    void text(Vec2 pos, const char* text, Color color);
    void arrow(Vec2 from, Vec2 to, Color color);

    void beginFrame();
    void endFrame(CommandBuffer* cmd, const Camera& camera);

private:
    std::vector<DebugPrimitive> m_primitives;
};

}  // namespace Caffeine::Debug
```

---

## B. Dependências entre Sistemas

```
GameLoop
  │
  ├── InputManager.beginFrame()
  ├── EventBus.dispatch()
  │
  ├── [Jobs parallelizados]
  │     ├── PhysicsSystem2D     (Job — lê RigidBody, Collider, Position, Velocity)
  │     ├── AnimationSystem     (Job — lê Animator, Sprite, escreve Sprite)
  │     └── AssetManager.async  (Job — carrega assets pendentes)
  │
  ├── [Jobs sincronizados]
  │     └── ECSWorld.update()   (executa todos os systems em ordem)
  │           ├── PhysicsSystem2D.priority=100
  │           ├── MovementSystem.priority=150
  │           ├── AnimationSystem.priority=200
  │           ├── UISystem.priority=500
  │           └── RenderSystem.priority=1000
  │
  ├── InputManager.endFrame()
  ├── AudioSystem.update()
  └── BatchRenderer.render()
        ├── TextureAtlas.getUV()
        ├── Camera2D.viewProjectionMatrix()
        └── RHI.CommandBuffer.drawInstanced()
```

---

## C. Critérios de Sucesso por Sistema

| Sistema | Critério | Benchmark |
|---|---|---|
| **Game Loop** | Determinístico em 60Hz, sem drift em 1 hora | 3600 frames, deltaTime acumulado = 60.0 ± 0.001 |
| **Job System** | 10K Jobs em <10ms, tsan clean | `parallelFor(1M, func)` em <50ms em 8 cores |
| **Input** | <1ms de latency em ação | `justPressed` registrado no mesmo frame |
| **ECS** | 100K entidades, 60fps, <5ms por frame | ECS benchmark: insert/query/destroy |
| **Scene Serialization** | Scene com 1K entidades em <100ms | Serialize + deserialize round-trip <200ms |
| **Event Bus** | 1K eventos/frame sem overhead perceptível | `publish()` <0.1ms |
| **Asset Manager** | 100 textures (512×512) em <2s (async) | 200MB carregados em background sem freeze |
| **Batch Renderer** | 50K sprites = 1 draw call | GPU Profiler: <2ms de GPU time |
| **Camera** | Smooth follow sem jitter | Camera segue Entity a 60fps sem artifacts |
| **Audio** | 32 SFX simultâneos sem crackle | Audio pool sem alloc em runtime |
| **Animation** | 60fps em 100 entidades animadas | `advanceFrame()` <0.1ms |
| **Physics** | 1K rigid bodies, 60fps | Physics step <3ms |
| **UI** | 50 widgets, 60fps | UI render <2ms |
| **Debug Tools** | Log sem impact mensurável | `log()` <1μs |

---

## D. Escalation Triggers

| Se... | Então... |
|---|---|
| Necessário multi-parâmetro locomotion/aim blending | Adicionar blend tree layer para animação |
| Gameplay precisa de joints, CCD, ou many dynamic stacks | Integrar biblioteca de física dedicada |
| Conteúdo escala e single-scene flow é limitante | Adicionar additive scenes / world streaming |
| Interface de editor é necessária | Implementar Caffeine Studio IDE (Fase 6) |
| Hot reload de assets é essencial | Expandir Asset Manager com file watcher robusto |
| Profiling mostra bottlenecks claros | Paralelizar com Job System onde apropriado |
