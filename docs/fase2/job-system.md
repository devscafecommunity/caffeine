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

---

---

## Implementation Plan (3 Phases)

> **Tech Stack:** C++20, std::thread, std::atomic, lock-free algorithms  
> **Goal:** Implementar um motor de concorrência que transforma processamento pesado em Jobs distribuidos entre todos os nucleos da CPU.

### Visão Geral do Sistema

#### Filosofia de Design

| Principio | Implementacao |
|----------|---------------|
| **Data-Oriented Design (DOD)** | Jobs em arrays continuos, cache-line aligned (64 bytes) |
| **Lock-Free Progress** | std::atomic em caminhos criticos, mutex apenas em fallback |
| **Work-Stealing** | Workers ociosos roubam trabalho de workers sobrecarregados |
| **Wait & Stall** | Main Thread ajuda a processar enquanto espera |

#### Requisitos Funcionais (RFs)

| ID | Requisito | Descricao Tecnica |
|----|----------|----------------|
| **RF1** | Thread Pool | N-1 threads workers (N = nucleos logicos) |
| **RF2** | Task Batching | Agrupar tarefas pequenas em lotes |
| **RF3** | Job Handles | Sincronizacao atomic para grupos de jobs |
| **RF4** | Wait & Stall | Main Thread processa enquanto espera |
| **RF5** | Priority Levels | 3 niveis: Critico, Normal, Background |

### Estrutura de Dados

#### Job (64 bytes - fit cache line)

```cpp
namespace Caffeine {

using JobFunc = void (*)(void*);

// @brief Estrutura de trabalho atomica (64 bytes para caber em cache line)
// @note data e um ponteiro para contexto alocado pelo caller
struct Job {
    JobFunc function;     // O que executar
    void* data;           // Dados (contexto)
    JobHandle* handle;    // Handle para sincronizacao (opcional)
    // @todo: padding para 64 bytes
};
static_assert(sizeof(Job) <= 64, "Job must fit in cache line");

} // namespace Caffeine
```

#### JobHandle (sincronizacao)

```cpp
namespace Caffeine {

// @brief Handle para sincronizacao de grupo de jobs
// @note usa contador atomico para tracking sem mutex
struct JobHandle {
    std::atomic<int32_t> unfinishedJobs;  // atomic counter
    
    // @brief Construtor - inicializa com N jobs pendentes
    explicit JobHandle(int32_t initialCount) 
        : unfinishedJobs(initialCount) {}
    
    // @brief Decrementa contador
    // @return true se chegou a zero
    bool complete() {
        return (unfinishedJobs.fetch_sub(1, std::memory_order_release) - 1) == 0;
    }
    
    // @brief Espera ate todos os jobs completarem
    void wait() {
        while (unfinishedJobs.load(std::memory_order_acquire) > 0) {
            // @todo: yield CPU
        }
    }
};

} // namespace Caffeine
```

#### Priority Levels

```cpp
namespace Caffeine {

// @brief Niveis de prioridade de job
enum class JobPriority : u8 {
    Critical = 0,   // Fisica/Input - nunca esperam
    Normal = 1,      // Gameplay, animation
    Background = 2   // Asset loading
};

} // namespace Caffeine
```

### Fases de Implementacao

#### Fase Alpha: Thread Pool com Mutex

**Objetivo:** Implementar thread pool basico funcional para debug e validacao.

**Criterio:** 1.000 tarefas simples completes com sucesso.

| Marco | Tarefas | Tempo |
|-------|---------|-------|
| Alpha-1 | Cria pastas e headers | 5 min |
| Alpha-2 | Implementa Job e JobHandle | 10 min |
| Alpha-3 | Implementa Worker com thread | 15 min |
| Alpha-4 | Implementa JobScheduler basico | 15 min |
| Alpha-5 | Escreve testes | 10 min |
| Alpha-6 | Stress test 1k tarefas | 5 min |
| **Total** | | **60 min** |

**Arquivos:**
- `src/threading/JobPriority.hpp` — Enum e comparadores
- `src/threading/Job.hpp` — struct Job (64 bytes)
- `src/threading/JobHandle.hpp` — JobHandle atomic
- `src/threading/JobQueue.hpp` — std::queue + mutex
- `src/threading/Worker.hpp` — thread loop basico
- `src/threading/JobSystem.hpp` — JobScheduler inicial
- `tests/test_threading.cpp` — testes do Job System

**TDD Workflow:**

1. **Alpha-1:** Estrutura de pastas, headers vazios
2. **Alpha-2:** Job struct (64 bytes), JobHandle com atomic counter
   - Test: `JobHandle initializes with correct count`
   - Test: `JobHandle decrements on complete`
   - Test: `Job is 64 bytes or less`
3. **Alpha-3:** Worker basico
   - Test: `Worker thread can be created`
4. **Alpha-4:** JobScheduler com fila protegida por mutex
   - Test: `JobScheduler creates workers`
   - Test: `JobScheduler can schedule job`
5. **Alpha-5 & 6:** Stress tests
   - Test: `Alpha stress: 1000 jobs complete`
   - Expected: < 100ms para 1k jobs

#### Fase Beta: Work-Stealing

**Objetivo:** Substituir fila unica por filas por thread e implementar steal().

**Criterio:** 10.000 tarefas com work-stealing ativo, CPU usage > 80%.

| Marco | Tarefas | Tempo |
|-------|---------|-------|
| Beta-1 | Implementar WorkerLocal deque | 15 min |
| Beta-2 | Implementar steal() algorithm | 15 min |
| Beta-3 | Integrar com JobScheduler | 15 min |
| Beta-4 | Stress test 10k tarefas | 10 min |
| **Total** | | **55 min** |

**Arquivos novos:**
- `src/threading/WorkerLocal.hpp` — deque LIFO por worker

**WorkerLocal (LIFO deque):**

```cpp
class WorkerLocal {
public:
    // @brief Adiciona job na frente (LIFO)
    void push(Job&& job) {
        queue_.push_front(std::move(job));
    }
    
    // @brief Remove job da frente (LIFO)
    bool pop(Job* out) {
        if (queue_.empty()) return false;
        *out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }
    
    // @brief Tenta roubar da cauda (FIFO - para steal)
    bool steal(Job* out) {
        if (queue_.empty()) return false;
        *out = std::move(queue_.back());
        queue_.pop_back();
        return true;
    }
    
    bool empty() const { return queue_.empty(); }
private:
    std::deque<Job> queue_;
};
```

**Stress test:**
```cpp
TEST_CASE("Beta stress: 10000 jobs with work stealing", "[threading][stress]") {
    Caffeine::JobScheduler scheduler;
    std::atomic<int32_t> counter{0};
    
    auto handle = scheduler.schedule(10000, [](void* data) {
        auto* c = static_cast<std::atomic<int32_t>*>(data);
        c->fetch_add(1);
    }, &counter);
    
    scheduler.wait(handle);
    CHECK(counter.load() == 10000);
}
```

#### Fase Gold: Lock-Free

**Objetivo:** Trocar mutexes por operacoes atomicas de baixo nivel.

**Criterio:** 50.000 tarefas em lock-free, zero mutex no hot path.

| Marco | Tarefas | Tempo |
|-------|---------|-------|
| Gold-1 | Implementar atomic queue | 20 min |
| Gold-2 | Substituir mutex por atomic | 15 min |
| Gold-3 | Stress test 50k tarefas | 10 min |
| Gold-4 | TSAN/ASAN clean | 10 min |
| **Total** | | **55 min** |

**Stress test final:**
```cpp
TEST_CASE("Gold stress: 50000 jobs complete", "[threading][stress]") {
    Caffeine::JobScheduler scheduler;
    std::atomic<int32_t> counter{0};
    
    auto handle = scheduler.schedule(50000, [](void* data) {
        auto* c = static_cast<std::atomic<int32_t>*>(data);
        c->fetch_add(1);
    }, &counter);
    
    scheduler.wait(handle);
    CHECK(counter.load() == 50000);
}
```

### Criterios de Progresso

| Fase | Teste | Benchmark | Target |
|------|-------|-----------|--------|
| Alpha | 1k tarefas | < 100ms | Correctness |
| Beta | 10k tarefas, work-stealing | < 500ms | Load balancing |
| Gold | 50k tarefas, lock-free | < 2s, TSAN clean | Performance |

### Inclusao no Build

```cmake
# src/CMakeLists.txt
add_subdirectory(threading)
```

```cpp
// src/Caffeine.hpp
#include "threading/JobSystem.hpp"
```

### Resumo da Implementacao

| Fase | Descricao | Arquivos | Stress Test |
|------|----------|---------|-------------|
| **Alpha** | Thread pool + mutex | 7 novos | 1k jobs |
| **Beta** | Work-stealing | 1 novo | 10k jobs |
| **Gold** | Lock-free | 1 novo | 50k jobs |

**Tempo total estimado:** ~3 horas

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §2 Job System
- [Jolt Physics Job System](https://github.com/jrouwe/JoltPhysics) — Referência de design
- [Multithreaded Job Systems in Game Engines](https://pulsegeek.com/articles/multithreaded-job-systems-in-game-engines/)
