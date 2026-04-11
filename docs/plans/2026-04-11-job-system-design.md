# Caffeine Job System (CJS) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implementar um motor de concorrência que transforma processamento pesado em Jobs distribuidos entre todos os nucleos da CPU, garantido que nenhum nucleo fique ocioso.

**Architecture:** Job System com Work-Stealing, filosofia DOD (Data-Oriented Design) para minimizar saltos de memoria e maximizar cache L1/L2, e implementacao em 3 fases progressivas (Alpha -> Beta -> Gold).

**Tech Stack:** C++20, std::thread, std::atomic, lock-free algorithms

---

## 1. Visao Geral do Sistema

### 1.1 Filosofia de Design

| Principio | Implementacao |
|----------|---------------|
| **Data-Oriented Design (DOD)** | Jobs em arrays continuos, cache-line aligned (64 bytes) |
| **Lock-Free Progress** | std::atomic em caminhos criticos, mutex apenas em fallback |
| **Work-Stealing** | Workers ociosos roubam trabalho de workers sobrecarregados |
| **Wait & Stall** | Main Thread ajuda a processar enquanto espera |

### 1.2 Requisitos Funcionais (RFs)

| ID | Requisito | Descricao Tecnica |
|----|----------|----------------|
| **RF1** | Thread Pool | N-1 threads workers (N = nucleos logicos) |
| **RF2** | Task Batching | Agrupar tarefas pequenas em lotes |
| **RF3** | Job Handles | Sincronizacao atomic para grupos de jobs |
| **RF4** | Wait & Stall | Main Thread processa enquanto espera |
| **RF5** | Priority Levels | 3 niveis: Critico, Normal, Background |

---

## 2. Estrutura de Arquivos

### 2.1 Arvore de Modulos

```
src/
└── threading/
    ├── JobSystem.hpp         # API publica, JobScheduler
    ├── Job.hpp             # struct Job (64 bytes)
    ├── JobHandle.hpp       # sincronizacao atomic
    ├── JobQueue.hpp       # fila lock-free
    ├── Worker.hpp         # thread worker
    ├── WorkerLocal.hpp   # fila local (deque LIFO)
    └── JobPriority.hpp    # prioridades
```

### 2.2 Arquivos a Criar

| Arquivo | Descricao | Fase |
|---------|----------|------|
| `src/threading/JobPriority.hpp` | Enum e comparadores | Alpha |
| `src/threading/Job.hpp` | struct Job (64 bytes) | Alpha |
| `src/threading/JobHandle.hpp` | JobHandle atomic | Alpha |
| `src/threading/JobQueue.hpp` | std::queue + mutex | Alpha |
| `src/threading/Worker.hpp` | thread loop basico | Alpha |
| `src/threading/JobSystem.hpp` | JobScheduler inicial | Alpha |
| `src/threading/WorkerLocal.hpp` | deque por thread | Beta |
| `src/threading/JobQueueLockFree.hpp` | fila lock-free | Gold |

### 2.3 Arquivos de Teste

```
tests/
└── test_threading.cpp    # testes do Job System
```

---

## 3. Desenho das Estruturas

### 3.1 Job (64 bytes - fit cache line)

```cpp
namespace Caffeine {

using JobFunc = void (*)(void*);

// @brief Estrutura de trabalho atomica (64 bytes para caber em cache line)
// @note data e um ponteiro para contexto alocado pelo caller
struct Job {
    JobFunc function;     // O que executar
    void* data;       // Dados (contexto)
    JobHandle* handle;  // Handle para sincronizacao (opcional)
    // @todo: padding para 64 bytes
};
static_assert(sizeof(Job) <= 64, "Job must fit in cache line");

} // namespace Caffeine
```

### 3.2 JobHandle (sincronizacao)

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

### 3.3 Priority Levels

```cpp
namespace Caffeine {

// @brief Niveis de prioridade de job
enum class JobPriority : u8 {
    Critical = 0,   // Fisica/Input - nunca esperam
    Normal = 1,      // Gameplay, animation
    Background = 2     // Asset loading
};

} // namespace Caffeine
```

---

## 4. Fases de Implementacao

### Fase Alpha: Thread Pool com Mutex

**Objetivo:** Implementar thread pool basico funcional para debug e validacao.

**Criterio:** 1.000 tarefas simples (incremento de inteiros) completes com sucesso.

| Marco | Tarefas | Tempo |
|-------|---------|-------|
| Alpha-1 | Cria pastas e headers | 5 min |
| Alpha-2 | Implementa Job e JobHandle | 10 min |
| Alpha-3 | Implementa Worker com thread | 15 min |
| Alpha-4 | Implementa JobScheduler basico | 15 min |
| Alpha-5 | Escreve testes | 10 min |
| Alpha-6 | Stress test 1k tarefas | 5 min |
| **Total** | | **60 min** |

#### Tarefa Alpha-1: Criar estrutura de pastas e arquivos

**Files:**
- Create: `src/threading/JobPriority.hpp`
- Create: `src/threading/Job.hpp`
- Create: `src/threading/JobHandle.hpp`
- Create: `src/threading/JobQueue.hpp`
- Create: `src/threading/Worker.hpp`
- Create: `src/threading/JobSystem.hpp`
- Create: `tests/test_threading.cpp`

**Step 1: Criar diretorios**

```bash
mkdir -p src/threading
touch src/threading/.gitkeep
```

**Step 2: Criar JobPriority.hpp**

```cpp
#pragma once

#include "core/Types.hpp"

namespace Caffeine {

// ============================================================================
// @file    JobPriority.hpp
// @brief   Priority levels for Jobs
// ============================================================================

// @brief Niveis de prioridade de job
// @note CRITICAL = fisica/input (alta prioridade), BACKGROUND = asset loading (baixa)
enum class JobPriority : u8 {
    Critical = 0,   // Fisica, Input - nunca esperam
    Normal = 1,       // Gameplay, animation
    Background = 2    // Asset loading, streaming
};

constexpr const char* toString(JobPriority priority) {
    switch (priority) {
        case JobPriority::Critical:  return "Critical";
        case JobPriority::Normal:   return "Normal";
        case JobPriority::Background: return "Background";
        default: return "Unknown";
    }
}

} // namespace Caffeine
```

**Step 3: Commit**

```bash
git add src/threading/
git commit -m "feat(threading): add JobPriority enum and basic headers"
```

#### Tarefa Alpha-2: Implementar Job e JobHandle

**Files:**
- Modify: `src/threading/Job.hpp`
- Modify: `src/threading/JobHandle.hpp`

**Step 1: Write the failing test**

```cpp
// tests/test_threading.cpp
#include <catch2/catch.hpp>
#include "threading/Job.hpp"
#include "threading/JobHandle.hpp"

TEST_CASE("JobHandle initializes with correct count", "[threading]") {
    Caffeine::JobHandle handle(5);
    CHECK(handle.unfinishedJobs.load() == 5);
}

TEST_CASE("JobHandle decrements on complete", "[threading]") {
    Caffeine::JobHandle handle(3);
    handle.complete();
    CHECK(handle.unfinishedJobs.load() == 2);
}

TEST_CASE("JobHandle returns true when finished", "[threading]") {
    Caffeine::JobHandle handle(1);
    bool done = handle.complete();
    CHECK(done == true);
}

TEST_CASE("Job is 64 bytes or less", "[threading]") {
    static_assert(sizeof(Caffeine::Job) <= 64, "Job must fit cache line");
}
```

**Step 2: Run test to verify it fails**

Run: `make test_threading`
Expected: FAIL - undefined symbols

**Step 3: Implement Job and JobHandle**

```cpp
// src/threading/Job.hpp
#pragma once

#include "core/Types.hpp"

namespace Caffeine {

// ============================================================================
// @file    Job.hpp
// @brief   Job unit - atomic work item (64 bytes for cache line)
// ============================================================================

using JobFunc = void (*)(void*);

// @brief Estrutura de trabalho atomica
// @note 64 bytes para caber em uma cache line (tipico L1/L2)
struct Job {
    JobFunc function;     // O que executar
    void* data;       // Dados (contexto)
    JobHandle* handle; // Handle para sincronizacao (opcional)
    u8 padding[40];  // Padding para 64 bytes
    
    Job() = default;
    
    Job(JobFunc func, void* userData, JobHandle* h = nullptr)
        : function(func), data(userData), handle(h) {}
};

// static_assert ja verificado em compile time

} // namespace Caffeine
```

```cpp
// src/threading/JobHandle.hpp
#pragma once

#include <atomic>

namespace Caffeine {

// ============================================================================
// @file    JobHandle.hpp
// @brief   Synchronization handle for job groups
// ============================================================================

// @brief Handle para sincronizacao atomica de grupo de jobs
// @note usa contador atomico - zero mutex em hot path
struct JobHandle {
    std::atomic<int32_t> unfinishedJobs;
    
    // @brief Construtor - inicializa com N jobs pendentes
    explicit JobHandle(int32_t initialCount = 1)
        : unfinishedJobs(initialCount) {}
    
    // @brief Decremente contador atomico
    // @return true se este era o ultimo job
    bool complete() {
        int32_t prev = unfinishedJobs.fetch_sub(
            1, 
            std::memory_order_release
        );
        return (prev - 1) == 0;
    }
    
    // @brief Espera ate todos os jobs completarem
    // @note usa busy-wait (yield) - nao bloqueia threads
    void wait() {
        while (unfinishedJobs.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }
};

} // namespace Caffeine
```

**Step 4: Run test to verify it passes**

Run: `make test_threading`
Expected: PASS

**Step 5: Commit**

```bash
git add src/threading/Job.hpp src/threading/JobHandle.hpp tests/test_threading.cpp
git commit -m "feat(threading): add Job struct and JobHandle atomic counter"
```

#### Tarefa Alpha-3: Implementar Worker basico

**Files:**
- Modify: `src/threading/Worker.hpp`

**Step 1: Write test for Worker**

```cpp
// Adicionar ao test_threading.cpp
TEST_CASE("Worker thread can be created", "[threading]") {
    std::atomic<int> counter{0};
    
    auto workerFn = [](void* data) {
        auto* c = static_cast<std::atomic<int>*>(data);
        c->fetch_add(1);
    };
    
    Caffeine::Worker worker;
    worker.start(workerFn, &counter);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    worker.stop();
    CHECK(counter.load() > 0);
}
```

**Step 2: Run test to verify it fails**

Run: `make test_threading`
Expected: FAIL - Worker not defined

**Step 3: Implement Worker**

```cpp
// src/threading/Worker.hpp
#pragma once

#include <thread>
#include <functional>
#include <atomic>

namespace Caffeine {

// ============================================================================
// @file    Worker.hpp
// @brief   Thread worker basico
// ============================================================================

// @brief Worker thread com loop simples
class Worker {
public:
    using WorkFunc = void(*)(void*);
    
    Worker() = default;
    ~Worker() { stop(); }
    
    // @brief Inicia worker com funcao
    void start(WorkFunc func, void* data) {
        running_.store(true, std::memory_order_release);
        func_ = func;
        data_ = data;
        thread_ = std::thread([this]() {
            while (running_.load(std::memory_order_acquire)) {
                if (func_) {
                    func_(data_);
                }
                func_ = nullptr;  // executa uma vez
            }
        });
    }
    
    // @brief Para o worker
    void stop() {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    WorkFunc func_{nullptr};
    void* data_{nullptr};
};

} // namespace Caffeine
```

**Step 4: Run test to verify it passes**

Run: `make test_threading`
Expected: PASS

**Step 5: Commit**

```bash
git add src/threading/Worker.hpp
git commit -m "feat(threading): add basic Worker class"
```

#### Tarefa Alpha-4: Implementar JobScheduler basico

**Files:**
- Create: `src/threading/JobScheduler.hpp`

**Step 1: Write test**

```cpp
TEST_CASE("JobScheduler creates workers", "[threading]") {
    Caffeine::JobScheduler scheduler;
    CHECK(scheduler.workerCount() > 0);
}

TEST_CASE("JobScheduler can schedule job", "[threading]") {
    Caffeine::JobScheduler scheduler;
    std::atomic<int> result{0};
    
    scheduler.schedule([](void* data) {
        auto* r = static_cast<std::atomic<int>*>(data);
        r->fetch_add(1);
    }, &result);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    CHECK(result.load() == 1);
}
```

**Step 2: Run test to verify it fails**

Run: `make test_threading`
Expected: FAIL - JobScheduler not defined

**Step 3: Implement JobScheduler (Alpha version)**

```cpp
// src/threading/JobScheduler.hpp
#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <thread>

#include "threading/Job.hpp"
#include "threading/JobHandle.hpp"
#include "threading/Worker.hpp"
#include "core/Platform.hpp"

namespace Caffeine {

// ============================================================================
// @file    JobScheduler.hpp
// @brief   Job scheduler basico com thread pool
// @note   Alpha: mutex-protected queue (easy debug)
// ============================================================================

// @brief Scheduler de jobs com thread pool
// @note Alpha usa mutex para simplificar debug
class JobScheduler {
public:
    JobScheduler();
    ~JobScheduler();
    
    // @brief Retorna numero de workers
    usize workerCount() const { return workers_.size(); }
    
    // @brief Agenda um job para execucao
    // @param func Funcao do job
    // @param data Dados para a funcao
    // @return Handle para sincronizacao
    JobHandle* schedule(JobFunc func, void* data);
    
    // @brief Agenda multiplos jobs
    // @param count Numero de jobs
    // @param func Funcao do job
    // @param data Arrays de dados (count elementos)
    JobHandle* schedule(usize count, JobFunc func, void* data);
    
    // @brief Espera todos os jobs completarem
    // @param handle Handle do job group
    void wait(JobHandle* handle);
    
private:
    // @brief Worker function do loop
    static void workerLoop(void* scheduler);
    
    // @brief Fila global de jobs (protegida por mutex)
    std::queue<Job> jobQueue_;
    mutable std::mutex queueMutex_;
    
    // @brief Threads workers
    std::vector<std::thread> workers_;
    
    // @brief Controla se workers estao ativos
    std::atomic<bool> running_{false};
    
    // @brief Handles alocados
    std::vector<JobHandle*> handles_;
};

// Implementacao inline (header-only para simplicidade)
inline JobScheduler::JobScheduler() {
    // Cria N-1 workers (N = nucleos logicos)
    usize threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 2;
    threadCount = (threadCount > 1) ? (threadCount - 1) : 1;
    
    running_.store(true, std::memory_order_release);
    workers_.reserve(threadCount);
    
    for (usize i = 0; i < threadCount; ++i) {
        workers_.emplace_back(workerLoop, this);
    }
}

inline JobScheduler::~JobScheduler() {
    running_.store(false, std::memory_order_release);
    
    // Desliga todos os workers
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

inline JobHandle* JobScheduler::schedule(JobFunc func, void* data) {
    auto handle = new JobHandle(1);
    Job job{func, data, handle};
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        jobQueue_.push(job);
    }
    
    return handle;
}

inline JobHandle* JobScheduler::schedule(usize count, JobFunc func, void* data) {
    auto handle = new JobHandle(static_cast<int32_t>(count));
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (usize i = 0; i < count; ++i) {
            jobQueue_.push(Job{func, data, handle});
        }
    }
    
    return handle;
}

inline void JobScheduler::wait(JobHandle* handle) {
    if (handle) {
        handle->wait();
        delete handle;
    }
}

inline void JobScheduler::workerLoop(void* scheduler) {
    auto* sch = static_cast<JobScheduler*>(scheduler);
    
    while (sch->running_.load(std::memory_order_acquire)) {
        Job job;
        
        {
            std::lock_guard<std::mutex> lock(sch->queueMutex_);
            if (!sch->jobQueue_.empty()) {
                job = sch->jobQueue_.front();
                sch->jobQueue_.pop();
            }
        }
        
        if (job.function) {
            job.function(job.data);
            
            if (job.handle) {
                job.handle->complete();
            }
        } else {
            // Escalonar CPU
            std::this_thread::yield();
        }
    }
}

} // namespace Caffeine
```

**Step 4: Run test to verify it passes**

Run: `make test_threading`
Expected: PASS

**Step 5: Commit**

```bash
git add src/threading/JobScheduler.hpp
git commit -m "feat(threading): add JobScheduler with mutex-protected queue"
```

#### Tarefa Alpha-5: Testes Alpha

**Files:**
- Modify: `tests/test_threading.cpp`
- Add: Stress test 1k tarefas

**Step 1: Write stress test**

```cpp
TEST_CASE("Alpha stress: 1000 jobs complete", "[threading][stress]") {
    Caffeine::JobScheduler scheduler;
    std::atomic<int32_t> counter{0};
    
    auto handle = scheduler.schedule(1000, [](void* data) {
        auto* c = static_cast<std::atomic<int32_t>*>(data);
        c->fetch_add(1);
    }, &counter);
    
    scheduler.wait(handle);
    
    CHECK(counter.load() == 1000);
}
```

**Step 2: Run stress test**

Run: `./tests/test_threading "[stress]"`
Expected: PASS - todos os 1000 jobs completam

**Step 3: Commit**

```bash
git add tests/test_threading.cpp
git commit -m "test(threading): add Alpha stress test (1k jobs)"
```

---

### Fase Beta: Work-Stealing

**Objetivo:** Substituir fila unica por filas por thread e implementar steal().

**Criterio:** 10.000 tarefas com work-stealing ativo, CPU usage > 80%.

| Marco | Tarefas | Tempo |
|-------|---------|-------|
| Beta-1 | Implementar WorkerLocal deque | 15 min |
| Beta-2 | Implementar steal() algorithm | 15 min |
| Beta-3 | Integrar com JobScheduler | 15 min |
| Beta-4 | Stress test 10k tarefas | 10 min |
| **Total** | | **55 min** |

#### Tarefa Beta-1: Implementar WorkerLocal (deque LIFO)

**Files:**
- Create: `src/threading/WorkerLocal.hpp`

```cpp
// src/threading/WorkerLocal.hpp
#pragma once

#include <deque>
#include <atomic>

namespace Caffeine {

// ============================================================================
// @file    WorkerLocal.hpp
// @brief   Local deque (LIFO) para cada worker
// ============================================================================

// @brief Fila local do worker - LIFO para cache locality
// @note Thread-local by design
class WorkerLocal {
public:
    // @brief Adiciona job na frente (LIFO)
    void push(Job&& job) {
        queue_.push_front(std::move(job));
    }
    
    // @brief Remove job da frente (LIFO)
    // @return true se tinha job
    bool pop(Job* out) {
        if (queue_.empty()) return false;
        *out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }
    
    // @brief Tenta roubar da cauda (FIFO - para steal)
    // @return true se conseguiu
    bool steal(Job* out) {
        if (queue_.empty()) return false;
        *out = std::move(queue_.back());
        queue_.pop_back();
        return true;
    }
    
    bool empty() const { return queue_.empty(); }
    usize size() const { return queue_.size(); }

private:
    std::deque<Job> queue_;
};

} // namespace Caffeine
```

#### Tarefa Beta-2: Implementar JobScheduler com Work-Stealing

Versao completa do JobScheduler com queues por worker e steal algorithm.

**Step 1: Update JobScheduler.hpp**

- Remover fila global unica
- Adicionar array de WorkerLocal (uma por thread)
- Implementar steal() no worker loop

#### Tarefa Beta-3: Stress test 10k tarefas

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

---

### Fase Gold: Lock-Free

**Objetivo:** Trocar mutexes por operacoes atomicas de baixo nivel.

**Criterio:** 50.000 tarefas em lock-free, zero mutex no hot path.

| Marco | Tarefas | Tempo |
|-------|---------|-------|
| Gold-1 | Implementar atomic queue | 20 min |
| Gold-2 | Substituir mutex por atomic | 15 min |
| Gold-3 | Stress test 50k tarefas | 10 min |
| Gold-4 | TSAN/ASAN clean | 10 min |
| **Total** | | **55 min** |

#### Tarefa Gold-1: Atomic Queue

Implementar fila lock-free usando compare-and-swap.

#### Tarefa Gold-2: Remover Mutex

Substituir mutexes por std::atomic nas filas locais.

#### Tarefa Gold-3: Stress test 50k tarefas

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

---

## 5. Testes Finais

### 5.1 Stress Test Global

```cpp
// Stress test final - 50k tarefas, lock-free
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

### 5.2 Criterios de Progresso

| Fase | Teste | Benchmark |
|------|-------|-----------|
| Alpha | 1k tarefas | < 100ms |
| Beta | 10k tarefas, work-stealing | < 500ms |
| Gold | 50k tarefas, lock-free | < 2s, TSAN clean |

---

## 6. Inclusao no Build

### 6.1 Update CMakeLists.txt

```cmake
# src/CMakeLists.txt
add_subdirectory(threading)
```

### 6.2 Update Caffeine.hpp

```cpp
// src/Caffeine.hpp
#pragma once

#include "core/Types.hpp"
// ...
#include "threading/JobSystem.hpp"
```

---

## 7. Resumo do Plano

| Fase | Descricao | Arquivos | Testes |
|------|----------|---------|--------|
| **Alpha** | Thread pool + mutex | 7 novos | 1k jobs |
| **Beta** | Work-stealing | 1 novo | 10k jobs |
| **Gold** | Lock-free | 1 novo | 50k jobs |

**Total estimado:** ~3 horas
