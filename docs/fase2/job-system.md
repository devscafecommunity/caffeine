# 🔗 Job System

> **Fase:** 2 — O Pulso e a Concorrência  
> **Namespace:** `Caffeine::Threading`  
> **Arquivo:** `src/threading/JobSystem.hpp`  
> **Status:** 📅 Planejado  
> **RFs:** RF2.2, RF2.3, RF2.4, RF2.5, RF2.6

---

## Visão Geral

O Job System distribui trabalho pesado (física, IA, animação, carregamento de assets) entre os núcleos da CPU. Cada **Job** é uma unidade de trabalho leve executada por uma **worker thread**.

**Abordagem:** Thread pool com **work-stealing** — cada worker tem sua própria fila; quando vazia, "rouba" jobs de workers ocupados. Isso garante balanceamento automático de carga sem coordenação central.

**Melhorias da Fase 2:**
- **Priority Queue** com 3 níveis: Crítico / Normal / Background
- **Fiber-based** — Jobs podem suspender sem bloquear a thread do OS

---

## API Planejada

```cpp
namespace Caffeine::Threading {

// ============================================================================
// @brief  Prioridade de um Job.
// ============================================================================
enum class JobPriority : u8 {
    Critical   = 0,  // 🔴 Renderização, Física — nunca esperam
    Normal     = 1,  // 🟡 Gameplay, animação, eventos
    Background = 2   // ⚪ Asset loading, streaming, compressão
};

// ============================================================================
// @brief  Interface base para todos os Jobs.
//
//  Jobs devem ser cheap to copy — dados passados por valor ou shared_ptr.
//  Nunca alocar no heap dentro de um Job.
// ============================================================================
struct IJob {
    virtual ~IJob() = default;
    virtual void execute() = 0;
    virtual JobPriority priority() const { return JobPriority::Normal; }
};

// ============================================================================
// @brief  Job com dados armazenados internamente (type-safe, zero alloc).
// ============================================================================
template<typename DataT>
struct JobWithData : IJob {
    DataT                       data;
    std::function<void(DataT&)> func;
    JobPriority                 prio = JobPriority::Normal;

    void execute() override { func(data); }
    JobPriority priority() const override { return prio; }
};

// ============================================================================
// @brief  Handle para rastrear e sincronizar um Job.
//
//  Uso: index + version evita ABA problem sem lock.
// ============================================================================
class JobHandle {
public:
    JobHandle() = default;
    explicit JobHandle(u32 index, u32 version);

    void wait() const;         // Bloqueia até o Job completar
    bool isComplete() const;   // Non-blocking check
    explicit operator bool() const { return isComplete(); }

private:
    u32 m_index   = u32_max;
    u32 m_version = u32_max;
};

// ============================================================================
// @brief  Barreira de sincronização — wait() bloqueia até N jobs terminarem.
//
//  Uso:
//      JobBarrier barrier(chunkCount);
//      for (auto& chunk : chunks) {
//          jobSystem.schedule(makeJob(chunk), &barrier);
//      }
//      barrier.wait();  // todos prontos
// ============================================================================
class JobBarrier {
public:
    explicit JobBarrier(u32 targetCount = 0);

    void add();      // incrementa contador pendente
    void release();  // decrementa (chamado pelo sistema quando Job completa)
    void wait();     // bloqueia até count == 0

private:
    std::atomic<u32>        m_count;
    std::atomic<u32>        m_generation;
    std::condition_variable m_cv;
    std::mutex              m_mutex;
};

// ============================================================================
// @brief  Sistema de Jobs principal.
//
//  Arquitetura:
//  - workerCount = hardware_concurrency() - 1 (main thread não compete)
//  - Cada worker tem deque local (work-stealing da ponta oposta)
//  - Fila global MPMC para overflow e jobs sem afinidade
//  - Priority queue garante Critical > Normal > Background
// ============================================================================
class JobSystem {
public:
    explicit JobSystem(u32 workerCount = 0);  // 0 = auto (n-1 cores)
    ~JobSystem();

    // Agenda um job com prioridade opcional
    JobHandle schedule(std::unique_ptr<IJob> job,
                       JobBarrier*           barrier = nullptr,
                       JobPriority           prio    = JobPriority::Normal);

    // Açúcar: cria JobWithData<T> automaticamente
    template<typename DataT, typename FuncT>
    JobHandle scheduleData(DataT       data,
                           FuncT&&     func,
                           JobBarrier* barrier = nullptr,
                           JobPriority prio    = JobPriority::Normal) {
        auto* job       = new JobWithData<DataT>{ std::move(data),
                                                   std::forward<FuncT>(func), prio };
        return schedule(std::unique_ptr<IJob>(job), barrier, prio);
    }

    // Parallel-for sobre [0, count) dividido em chunks por worker
    template<typename FuncT>
    JobHandle scheduleParallelFor(u32         count,
                                  FuncT&&     func,
                                  JobBarrier* barrier = nullptr,
                                  JobPriority prio    = JobPriority::Normal);

    void waitAll();                           // Bloqueia até fila vazia
    u32  workerCount() const { return m_workerCount; }

    struct Stats {
        u32 activeWorkers;
        u32 pendingJobs;
        u64 completedJobsTotal;
        f64 avgJobMs;
    };
    Stats stats() const;

private:
    struct WorkerThread; // implementação interna

    u32                         m_workerCount;
    std::vector<WorkerThread>   m_workers;
    // priority queues por nível
    JobQueue                    m_queues[3];  // Critical, Normal, Background
};

}  // namespace Caffeine::Threading
```

---

## Exemplos de Uso

```cpp
// ── Física distribuída em chunks ──────────────────────────────
Caffeine::Threading::JobSystem jobs;
Caffeine::Threading::JobBarrier barrier(particleCount / CHUNK_SIZE);

for (u32 i = 0; i < particleCount; i += CHUNK_SIZE) {
    jobs.scheduleData(
        ChunkJob{i, CHUNK_SIZE, dt},
        [&particles](const ChunkJob& j) {
            for (u32 k = j.start; k < j.start + j.count; ++k)
                particles[k].integrate(j.dt);
        },
        &barrier,
        Caffeine::Threading::JobPriority::Critical
    );
}
barrier.wait();  // todos os chunks prontos antes de render

// ── Parallel-for simplificado ─────────────────────────────────
jobs.scheduleParallelFor(entityCount,
    [&](u32 i) { animators[i].advance(dt); },
    nullptr,
    Caffeine::Threading::JobPriority::Normal
);

// ── Asset loading em background ───────────────────────────────
auto handle = jobs.scheduleData(
    AssetLoadJob{"textures/hero.caf"},
    [](const AssetLoadJob& j) { loadAsset(j.path); },
    nullptr,
    Caffeine::Threading::JobPriority::Background
);

// Main thread continua sem bloquear; verifica quando precisar:
if (handle.isComplete()) { /* use asset */ }
```

---

## Arquitetura Interna

```
JobSystem
  │
  ├── Worker[0]  ──┐
  ├── Worker[1]  ──┤  work-stealing deque por worker
  ├── Worker[2]  ──┤
  └── Worker[n]  ──┘
        │
        ▼
  Priority Queues
  [Critical]   ← Physics, Render
  [Normal]     ← Gameplay, Animation
  [Background] ← Asset load, streaming
        │
        ▼
  JobHandle (index + version) → sem ABA problem
```

**Work-Stealing:**  
Cada worker pega jobs da ponta "top" da sua própria deque. Quando vazia, rouba da ponta "bottom" de outro worker — minimizando contention.

---

## Integração com ECS (Fase 4)

```cpp
// ComponentQuery suporta forEachParallel nativo:
ComponentQuery physicsQuery;
physicsQuery.with<RigidBody2D, Collider2D, Position2D>();

physicsQuery.forEachParallel(world, [](RigidBody2D& rb, Collider2D& col,
                                       Position2D& pos) {
    // roda em parallel-for via Job System
    integrateRigidBody(rb, pos);
}, &barrier);
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| `workerCount = n-1` | Main thread não compete com workers por CPU |
| Work-stealing deque | Melhor throughput vs fila centralizada em workloads heterogêneos |
| `index + version` no Handle | Evita ABA problem sem mutex |
| 3 níveis de prioridade | Renderização/Física nunca esperam por asset load |
| Fiber-based (RF2.6) | Jobs pesados podem suspender sem bloquear OS thread |

---

## Critério de Aceitação

- [ ] 10K Jobs em <10ms em hardware de 8 cores
- [ ] `parallelFor(1M, func)` < 50ms em 8 cores
- [ ] `tsan` (ThreadSanitizer) clean — zero data races
- [ ] Workers a ≥ 80% de carga em stress test de 10K partículas
- [ ] `background` jobs nunca atrasam `critical` jobs

---

## Dependências

- **Upstream:** `Caffeine::Core::Types`, `Caffeine::Containers::HashMap`
- **Downstream:** [Asset Manager](../fase3/asset-manager.md), [ECS — forEachParallel](../fase4/ecs.md), [Physics 2D](../fase4/physics.md), [Animation](../fase4/animation.md)
- **Plano detalhado:** [`docs/plans/2026-04-11-job-system-design.md`](../plans/2026-04-11-job-system-design.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §2 Job System
- [`docs/plans/2026-04-11-job-system-design.md`](../plans/2026-04-11-job-system-design.md)
- [Jolt Physics Job System](https://github.com/jrouwe/JoltPhysics) — Referência de design
- [Multithreaded Job Systems in Game Engines](https://pulsegeek.com/articles/multithreaded-job-systems-in-game-engines/)
