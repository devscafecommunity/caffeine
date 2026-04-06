# 🧠 Memory Model — Especificação de Allocators

> ⚠️ **Status:** Versão 1.0 — Completo para as Fases 1-4.  
> ⚠️ Para contexto completo, consulte [`docs/MASTER.md`](../docs/MASTER.md) §8 e [`architecture_specs.md`](architecture_specs.md).

Este documento contém as **especificações técnicas detalhadas** dos sistemas de gerenciamento de memória da Caffeine Engine.

---

## 📋 Índice

| Allocator | Fase | Status | Uso |
|---|---|---|---|
| **IAllocator (Interface)** | 1 | 🔄 Em Progresso | Base para todos os allocators |
| **Linear Allocator** | 1 | 🔄 Em Progresso | Scratch space por frame |
| **Pool Allocator** | 1 | 🔄 Em Progresso | Blocos de tamanho fixo |
| **Stack Allocator** | 1 | 🔄 Em Progresso | Escopos aninhados |
| **Proxy Allocator** | 1 | 📅 Planejado | Wrappers com logging/debug |
| **TLSF Allocator** | TBD | 💡 Ideia | Fallback para alocações gerais |
| **Buddy Allocator** | TBD | 💡 Ideia | Para o sistema de levels |

---

## A. Visão Geral — Política de Memória

### A.1 The Golden Rule

```
╔════════════════════════════════════════════════════════════════╗
║  PROIBIDO: new e delete soltos no código da aplicação.     ║
║  TODA alocação deve passar pelos Custom Allocators.         ║
╚════════════════════════════════════════════════════════════════╝
```

### A.2 Mapa de Allocators por Uso

```
┌─────────────────────────────────────────────────────────────────────┐
│                 POLÍTICA DE MEMÓRIA DA CAFFEINE                       │
│                                                                      │
│  ════════════════════════════════════════════════════════════════    │
│  PERSISTENT (malloc/bootstrap)                                      │
│  ════════════════════════════════════════════════════════════════    │
│  • Asset registry (texturas, áudio carregados)                     │
│  • Scene data (entidades serializadas)                              │
│  • Shader bytecode                                                  │
│  • ECS archetype storage                                            │
│                                                                      │
│  ════════════════════════════════════════════════════════════════    │
│  POOL (tamanho fixo, rápido)                                       │
│  ════════════════════════════════════════════════════════════════    │
│  • Audio voices/emiters                                            │
│  • UI widgets                                                     │
│  • Input bindings / gamepad state                                   │
│  • Particle instances                                              │
│  • Active audio buffers                                            │
│                                                                      │
│  ════════════════════════════════════════════════════════════════    │
│  LINEAR / FRAME (reset a cada frame)                              │
│  ════════════════════════════════════════════════════════════════    │
│  • Contact manifolds (physics)                                      │
│  • Debug draw primitives                                           │
│  • Event queue                                                    │
│  • Temporary transforms                                            │
│  • Render command scratch                                          │
│                                                                      │
│  ════════════════════════════════════════════════════════════════    │
│  STACK (escopo aninhado)                                           │
│  ════════════════════════════════════════════════════════════════    │
│  • Level load/unload                                              │
│  • Scene transitions                                               │
│  • Task scopes (job sub-scopes)                                   │
│  • Profiler markers                                               │
└─────────────────────────────────────────────────────────────────────┘
```

### A.3 Interface Base

```cpp
namespace Caffeine::Memory {

// ============================================================================
// @brief  Interface base para todos os allocators.
//
//  Todos os allocators da Caffeine implementam esta interface.
//  Isso permite trocar allocators em runtime e usar patterns como
//  ProxyAllocator para debug.
// ============================================================================
class IAllocator {
public:
    virtual ~IAllocator() = default;

    // -----------------------------------------------------------------------
    // @brief  Aloca memória alinhada.
    // @param  size      Tamanho em bytes.
    // @param  alignment Alinhamento (deve ser potência de 2).
    // @return Ponteiro para a memória alocada ou nullptr se falhou.
    // -----------------------------------------------------------------------
    virtual void* alloc(usize size, usize alignment = 8) = 0;

    // -----------------------------------------------------------------------
    // @brief  Libera memória alocada.
    // @note   Para Linear e Stack, free() pode não fazer nada (reset limpa tudo).
    // -----------------------------------------------------------------------
    virtual void free(void* ptr) = 0;

    // -----------------------------------------------------------------------
    // @brief  Para Linear/Stack: volta ao início.
    //          Para Pool: não faz nada.
    // -----------------------------------------------------------------------
    virtual void reset() = 0;

    // -----------------------------------------------------------------------
    // @brief  Retorna a memória atualmente em uso (bytes).
    // -----------------------------------------------------------------------
    virtual usize usedMemory() const = 0;

    // -----------------------------------------------------------------------
    // @brief  Retorna a memória total alocada pelo allocator (bytes).
    // -----------------------------------------------------------------------
    virtual usize totalSize() const = 0;

    // -----------------------------------------------------------------------
    // @brief  Retorna o pico de memória já usado (bytes).
    // -----------------------------------------------------------------------
    virtual usize peakMemory() const = 0;

    // -----------------------------------------------------------------------
    // @brief  Retorna o número de alocações feitas.
    // -----------------------------------------------------------------------
    virtual usize allocationCount() const = 0;

    // -----------------------------------------------------------------------
    // @brief  Retorna o nome do allocator (para debugging).
    // -----------------------------------------------------------------------
    virtual const char* name() const = 0;
};

// ============================================================================
// @brief  Trait para detectar se T tem método destroy().
// ============================================================================
template<typename T>
using AlwaysFalse = std::false_type;

template<typename T>
struct has_destroy : AlwaysFalse<T> {};

template<typename T>
constexpr bool has_destroy_v = has_destroy<T>::value;

}  // namespace Caffeine::Memory
```

---

## B. Allocators Detalhados

---

## 1. Linear Allocator

**Fase:** 1

### Propósito

| Quando usar | Quando NÃO usar |
|---|---|
| Memória volátil por frame | Persistência entre frames |
| Scratch space para cálculos | Alocações de tamanho variável que precisam de free individual |
| Buffers temporários (contact manifolds, events) | Alocações que sobrevivem ao escopo |

### Interface

```cpp
namespace Caffeine::Memory {

// ============================================================================
// @brief  Allocator linear — aloca do início ao fim, reset limpa tudo.
//
//  Algoritmo:
//
//  ┌─────────────────────────────────────────────────────────────┐
//  │  [ Usado │............. Livre ...................] [ Fim ]   │
//  │          ▲                                          ▲      │
//  │        m_cursor                                    m_end  │
//  └─────────────────────────────────────────────────────────────┘
//
//  alloc() avança m_cursor e retorna ponteiro.
//  reset() volta m_cursor para m_start.
//
//  Performance: O(1) para todas as operações.
//
//  Thread Safety: NÃO thread-safe. Usar apenas na main thread.
// ============================================================================
class LinearAllocator : public IAllocator {
public:
    // -----------------------------------------------------------------------
    // @brief  Constrói com um buffer pré-alocado.
    // @param  buffer  Ponteiro para o buffer (ownership não é transferido).
    // @param  size    Tamanho do buffer em bytes.
    // @note   O buffer deve permanecer válido durante a vida do allocator.
    // -----------------------------------------------------------------------
    LinearAllocator(void* buffer, usize size);

    // -----------------------------------------------------------------------
    // @brief  Constrói e aloca buffer internamente (via malloc).
    // -----------------------------------------------------------------------
    explicit LinearAllocator(usize size);

    ~LinearAllocator() override;

    void* alloc(usize size, usize alignment = 8) override;
    void  free(void* ptr) override;   // No-op (reset limpa tudo)
    void  reset() override;           // Volta ao início

    usize usedMemory()   const override { return m_cursor - m_start; }
    usize totalSize()    const override { return m_end - m_start; }
    usize peakMemory()   const override { return m_peak; }
    usize allocationCount() const override { return m_allocCount; }
    const char* name()    const override { return "Linear"; }

    // -----------------------------------------------------------------------
    // @brief  Retorna a memória livre disponível.
    // -----------------------------------------------------------------------
    usize availableMemory() const { return m_end - m_cursor; }

    // -----------------------------------------------------------------------
    // @brief  Retorna o cursor atual (para debugging).
    // -----------------------------------------------------------------------
    void* currentPointer() const { return m_cursor; }

private:
    void advanceCursor(usize size, usize alignment);

    u8*  m_start   = nullptr;
    u8*  m_end     = nullptr;
    u8*  m_cursor  = nullptr;
    usize m_peak    = 0;
    usize m_allocCount = 0;
    bool  m_ownsBuffer = false;
};

}  // namespace Caffeine::Memory
```

### Implementação

```cpp
void* LinearAllocator::alloc(usize size, usize alignment) {
    // Align up cursor
    usize padding = calculatePadding(m_cursor, alignment);
    usize totalSize = size + padding;

    // Check overflow
    if (m_cursor + totalSize > m_end) {
        return nullptr;  // Out of memory
    }

    void* ptr = m_cursor + padding;
    m_cursor += totalSize;

    // Track stats
    usize used = usedMemory();
    if (used > m_peak) m_peak = used;
    ++m_allocCount;

    return ptr;
}
```

### Uso por Sistema

| Sistema | Uso | Tamanho típico |
|---|---|---|
| **Physics** | Contact manifolds, collision pairs | 64KB - 256KB |
| **Events** | Event queue temporário | 32KB |
| **Debug Draw** | Primitive lists | 128KB |
| **Render** | Vertex buffer staging | 1MB |
| **Job System** | Task scratch memory | 64KB por worker |

---

## 2. Pool Allocator

**Fase:** 1

### Propósito

| Quando usar | Quando NÃO usar |
|---|---|
| Objetos de tamanho fixo | Tamanhos variáveis |
| Alocações/desalocações frequentes | Dados que precisam sobreviver ao reset |
| Emitters, particles, audio voices | Assets carregados |

### Interface

```cpp
namespace Caffeine::Memory {

// ============================================================================
// @brief  Pool Allocator — aloca blocos de tamanho fixo.
//
//  Algoritmo:
//
//  ┌─────────────────────────────────────────────────────────────┐
//  │  Slot[0] │ Slot[1] │ Slot[2] │ ... │ Slot[N-1]          │
//  │    ●      │    ○    │    ●    │     │    ○               │
//  │  (usado)  │ (livre) │ (usado) │     │   (livre)         │
//  └─────────────────────────────────────────────────────────────┘
//       ▲                                                    ▲
//     m_firstFree ──── linked list de slots livres          │
//                                                          m_end
//  Performance: O(1) amortizado (primeiro slot livre).
//  Thread Safety: NÃO thread-safe. Usar TLS se necessário.
// ============================================================================
class PoolAllocator : public IAllocator {
public:
    // -----------------------------------------------------------------------
    // @brief  Constrói com buffer e tamanho de slot.
    // @param  buffer    Ponteiro para o buffer.
    // @param  size      Tamanho total do buffer.
    // @param  slotSize  Tamanho de cada slot (mínimo 8 bytes).
    // @param  alignment Alinhamento dos slots (padrão 8).
    // -----------------------------------------------------------------------
    PoolAllocator(void* buffer, usize size, usize slotSize,
                  usize alignment = 8);

    // -----------------------------------------------------------------------
    // @brief  Constrói e aloca buffer internamente.
    // -----------------------------------------------------------------------
    PoolAllocator(usize poolSize, usize slotSize, usize alignment = 8);
    ~PoolAllocator() override;

    void* alloc(usize size, usize alignment = 8) override;
    void  free(void* ptr) override;
    void  reset() override;  // Marca todos os slots como livres

    usize usedMemory()    const override { return m_usedCount * m_slotSize; }
    usize totalSize()     const override { return m_poolSize; }
    usize peakMemory()   const override { return m_peakSlots * m_slotSize; }
    usize allocationCount() const override { return m_usedCount; }
    const char* name()    const override { return "Pool"; }

    // -----------------------------------------------------------------------
    // @brief  Retorna quantos slots estão livres.
    // -----------------------------------------------------------------------
    usize freeSlots() const { return m_maxSlots - m_usedCount; }

    // -----------------------------------------------------------------------
    // @brief  Retorna o tamanho de cada slot.
    // -----------------------------------------------------------------------
    usize slotSize() const { return m_slotSize; }

    // -----------------------------------------------------------------------
    // @brief  Retorna o número máximo de slots.
    // -----------------------------------------------------------------------
    usize maxSlots() const { return m_maxSlots; }

private:
    // Free list: cada slot livre aponta para o próximo
    void** m_freeList  = nullptr;
    u8*    m_poolStart = nullptr;
    usize  m_poolSize  = 0;
    usize  m_slotSize  = 0;
    usize  m_maxSlots   = 0;
    usize  m_usedCount  = 0;
    usize  m_peakSlots = 0;
    bool   m_ownsBuffer = false;
};

}  // namespace Caffeine::Memory
```

### Implementação

```cpp
void* PoolAllocator::alloc(usize size, usize alignment) {
    // Pool allocator ignora size — usa slotSize fixo
    CF_ASSERT(size <= m_slotSize, "Requested size exceeds slot size");

    if (!m_freeList) {
        return nullptr;  // Pool exausto
    }

    void* ptr = m_freeList;
    m_freeList = *static_cast<void**>(ptr);  // Pop do free list
    ++m_usedCount;
    if (m_usedCount > m_peakSlots) m_peakSlots = m_usedCount;

    return ptr;
}

void PoolAllocator::free(void* ptr) {
    if (!ptr) return;

    // Push para o free list
    *static_cast<void**>(ptr) = m_freeList;
    m_freeList = ptr;
    --m_usedCount;
}
```

### Uso por Sistema

| Sistema | Uso | Tamanho de slot |
|---|---|---|
| **Audio** | AudioSource/voice instances | 128 bytes |
| **Particles** | Particle instances | 64 bytes |
| **UI** | Widget instances | 256 bytes |
| **ECS** | Component arrays (internal) | varies |
| **Input** | Gamepad state records | 64 bytes |

---

## 3. Stack Allocator

**Fase:** 1

### Propósito

| Quando usar | Quando NÃO usar |
|---|---|
| Escopos aninhados (levels) | Persistência entre resets |
| Frames com alocações que precisam de free ordenado | Free fora de ordem |
| Temporary task scopes | Alocações de threads diferentes |

### Interface

```cpp
namespace Caffeine::Memory {

// ============================================================================
// @brief  Stack Allocator — aloca em pilha com marcadores.
//
//  Algoritmo:
//
//  1. Allocate: avança cursor como LinearAllocator
//  2. SetMarker: salva posição atual
//  3. FreeToMarker: libera tudo após o marker
//
//  ┌─────────────────────────────────────────────────────────────┐
//  │  [ Usado A ] │ [ Marker ] │ [ Usado B ] │.....│ Livre   │
//  │                          ▲           ▲                    │
//  │                       marker1      cursor                │
//  │                                                           │
//  │  freeToMarker(marker1) ───▶ libera [Usado B]           │
//  └───────────────────────────────────────────────────────────┘
//
//  Performance: O(1) para todas as operações.
//  Thread Safety: NÃO thread-safe.
// ============================================================================
class StackAllocator : public IAllocator {
public:
    StackAllocator(void* buffer, usize size);
    explicit StackAllocator(usize size);
    ~StackAllocator() override;

    void* alloc(usize size, usize alignment = 8) override;
    void  free(void* ptr) override;  // Free to marker implícito (último alloc)
    void  reset() override;

    // -----------------------------------------------------------------------
    // @brief  Salva um marcador na posição atual.
    // -----------------------------------------------------------------------
    Marker setMarker();

    // -----------------------------------------------------------------------
    // @brief  Libera toda a memória após o marcador.
    // -----------------------------------------------------------------------
    void freeToMarker(Marker marker);

    usize usedMemory()      const override;
    usize totalSize()       const override { return m_end - m_start; }
    usize peakMemory()      const override { return m_peak; }
    usize allocationCount() const override { return m_allocCount; }
    const char* name()     const override { return "Stack"; }

private:
    u8*  m_start  = nullptr;
    u8*  m_end    = nullptr;
    u8*  m_cursor = nullptr;
    usize m_peak   = 0;
    usize m_allocCount = 0;
    bool  m_ownsBuffer = false;
};

}  // namespace Caffeine::Memory
```

### Uso por Sistema

| Sistema | Uso | Tamanho típico |
|---|---|---|
| **Scene** | Level load/unload | 1MB - 16MB |
| **Job System** | Task sub-scopes | 64KB por worker |
| **Profiler** | Markers de escopo | 4KB |
| **Asset Manager** | Asset loading arena | 8MB |

---

## 4. Proxy Allocator

**Fase:** 1

### Propósito

Wrapper que adiciona logging, debugging ou accounting a qualquer allocator existente.

### Interface

```cpp
namespace Caffeine::Memory {

// ============================================================================
// @brief  Proxy Allocator — wrapper com logging e accounting.
//
//  Útil para debugging de memory leaks e profiling de alocações.
// ============================================================================
class ProxyAllocator : public IAllocator {
public:
    ProxyAllocator(IAllocator* backend, const char* name);
    ~ProxyAllocator() override;

    void* alloc(usize size, usize alignment = 8) override;
    void  free(void* ptr) override;
    void  reset() override;

    usize usedMemory()      const override;
    usize totalSize()       const override;
    usize peakMemory()      const override;
    usize allocationCount()  const override;
    const char* name()     const override;

    // -----------------------------------------------------------------------
    // @brief  Retorna relatório de alocações.
    // -----------------------------------------------------------------------
    struct AllocationReport {
        const char* allocatorName;
        usize currentUsed;
        usize peakUsed;
        usize totalAllocations;
        usize totalFrees;
        usize failedAllocations;
    };
    AllocationReport report() const;

private:
    IAllocator* m_backend;
    const char*  m_name;

    usize m_currentUsed      = 0;
    usize m_peakUsed        = 0;
    usize m_totalAllocs     = 0;
    usize m_totalFrees      = 0;
    usize m_failedAllocs    = 0;

    // Allocation header para tracking
    struct Header {
        usize size;
        usize alignment;
    };
    static constexpr usize HEADER_SIZE = sizeof(Header);
};

}  // namespace Caffeine::Memory
```

---

## C. Allocator Registry

**Fase:** 1

### Visão Geral

Para debugging e profiling, todo allocator é registrado em um registry global.

### Interface

```cpp
namespace Caffeine::Memory {

// ============================================================================
// @brief  Registry global de allocators.
//
//  Permite:
//  - Verificar memory leaks no shutdown
//  - Profiling de uso de memória
//  - Detectar fragmentation
// ============================================================================
class AllocatorRegistry {
public:
    // -----------------------------------------------------------------------
    // @brief  Registra um allocator.
    // -----------------------------------------------------------------------
    static void registerAllocator(IAllocator* alloc, const char* name);

    // -----------------------------------------------------------------------
    // @brief  Remove um allocator do registry.
    // -----------------------------------------------------------------------
    static void unregisterAllocator(IAllocator* alloc);

    // -----------------------------------------------------------------------
    // @brief  Retorna estatísticas agregadas de todos os allocators.
    // -----------------------------------------------------------------------
    struct GlobalStats {
        usize totalAllocators;
        usize totalUsedMemory;
        usize totalPeakMemory;
        usize totalAllocations;
    };
    static GlobalStats globalStats();

    // -----------------------------------------------------------------------
    // @brief  Imprime relatório de todos os allocators (debug).
    // -----------------------------------------------------------------------
    static void printReport();

    // -----------------------------------------------------------------------
    // @brief  Verifica se há memory leaks (chamar no shutdown).
    // -----------------------------------------------------------------------
    static bool hasLeaks();

private:
    static std::vector<ProxyAllocator*> s_allocators;
    static std::mutex s_mutex;
};

}  // namespace Caffeine::Memory
```

### Exemplo de Saída

```
=== Memory Report ===
[Linear]  Physics Scratch:   64.0 KB /  256.0 KB (used/peak)
[Pool]    Audio Voices:       16.0 KB /   32.0 KB (32 slots, 16 used)
[Pool]    UI Widgets:          8.0 KB /   16.0 KB (64 slots, 8 used)
[Stack]   Level Arena:       512.0 KB / 2048.0 KB (marker at 25%)
[Linear]  Render Staging:    64.0 KB / 1024.0 KB (used/peak)

Total: 664.0 KB used, 3.3 MB peak, 1.2M allocs
```

---

## D. Uso por Sistema — Mapa Completo

```
┌──────────────────────────────────────────────────────────────────────┐
│                     SYSTEM → ALLOCATOR MAP                             │
├────────────────────┬────────────────────┬─────────────────────────────┤
│ Sistema             │ Allocator          │ Razão                     │
├────────────────────┼────────────────────┼─────────────────────────────┤
│ Game Loop          │ Linear (frame)    │ Reset a cada frame        │
│ Job System         │ Linear (scratch)   │ Task scopes                │
│ Job System         │ Stack (task scope) │ Marca/free ordenado        │
│ Input Manager      │ Pool              │ Gamepad state, bindings     │
│ Event Bus          │ Linear (frame)    │ Fila de eventos            │
│ Event Bus          │ Linear (listener)  │ Listeners por frame        │
│ ECS World          │ Pool              │ Component storage           │
│ ECS World          │ Persistent        │ Archetype storage          │
│ Physics 2D         │ Linear (frame)    │ Contact manifolds          │
│ Physics 2D         │ Pool             │ Rigid body proxies         │
│ Asset Manager       │ Persistent        │ Asset registry, cache       │
│ Asset Manager       │ Linear (load)    │ File buffer, decode        │
│ Audio System        │ Pool             │ AudioSource instances      │
│ Audio System        │ Persistent        │ Clip registry              │
│ Animation System    │ Pool             │ Animator instances         │
│ Render / RHI       │ Linear (frame)    │ Command buffer scratch     │
│ Batch Renderer      │ Linear (frame)   │ Batch metadata             │
│ Camera System       │ Linear (frame)   │ Temp matrices              │
│ UI System          │ Pool             │ Widget instances            │
│ UI System          │ Persistent        │ Style/theme data           │
│ Debug Draw         │ Linear (frame)    │ Primitive list             │
│ Debug Profiler     │ Stack            │ Scoped markers             │
│ Scene Manager       │ Stack (level)    │ Load/unload arena          │
│ Scene Manager       │ Persistent        │ Scene metadata             │
│ Serializer          │ Linear (frame)   │ Parse buffer              │
└────────────────────┴────────────────────┴─────────────────────────────┘
```

---

## E. Benchmarks de Referência

### E.1 Linear Allocator

| Métrica | Valor |
|---|---|
| `alloc()` throughput | ~50M allocs/segundo |
| `reset()` throughput | ~500M resets/segundo |
| Fragmentação | 0% (reset limpa tudo) |
| Overhead por alloc | ~0 bytes |

### E.2 Pool Allocator

| Métrica | Valor |
|---|---|
| `alloc()` throughput | ~30M allocs/segundo |
| `free()` throughput | ~40M frees/segundo |
| Fragmentação | 0% (slots fixos) |
| Overhead por slot | 0 bytes (exceto bookkeeping) |
| Max slots | 65,535 por pool |

### E.3 Stack Allocator

| Métrica | Valor |
|---|---|
| `alloc()` throughput | ~50M allocs/segundo |
| `freeToMarker()` throughput | ~100M frees/segundo |
| Fragmentação | 0% (free to marker) |
| Max markers por stack | 255 |

---

## F. Checklist de Integração

Antes de usar um allocator em um novo sistema:

- [ ] Sistema usa `IAllocator*` como parâmetro (não hardcoded)
- [ ] Sistema reseta allocators lineares no início de cada frame
- [ ] Sistema usa Pool para objetos de tamanho fixo
- [ ] Sistema usa Stack para escopos aninhados
- [ ] Sistema loga estatísticas via AllocatorRegistry
- [ ] Sistema verifica `alloc()` returnou nullptr
- [ ] Sistema NÃO usa `new` ou `delete`
- [ ] Sistema NÃO guarda ponteiros após `reset()` ou `freeToMarker()`
