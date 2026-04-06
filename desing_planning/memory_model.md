# 🧠 Memory Model — Especificação de Allocators

> ⚠️ **Status:** A ser preenchido conforme desenvolvimento da Fase 1.  
> ⚠️ Para contexto completo, consulte [`docs/MASTER.md`](../docs/MASTER.md) §8.

Este documento contém as **especificações técnicas detalhadas** dos sistemas de gerenciamento de memória da Caffeine Engine.

---

## 📋 Índice

| Allocator | Fase | Status | Uso |
|---|---|---|---|
| **IAllocator (Interface)** | 1 | 📅 Planejado | Base para todos os allocators |
| **Linear Allocator** | 1 | 📅 Planejado | Scratch space por frame |
| **Pool Allocator** | 1 | 📅 Planejado | Blocos de tamanho fixo |
| **Stack Allocator** | 1 | 📅 Planejado | Escopos aninhados |
| **Proxy Allocator** | 1 | 📅 Planejado | Wrappers com logging/debug |
| **TLSF Allocator** | TBD | 💡 Ideia | Fallback para alocações gerais |
| **Buddy Allocator** | TBD | 💡 Ideia | Para o sistema de levels |

---

## 📝 Template de Especificação

Cada allocator segue o template:

```markdown
## [Nome do Allocator]

### Propósito
- Quando usar
- Quando NÃO usar
- Trade-offs

### Interface
```cpp
class NomeAllocator : public IAllocator {
public:
    explicit NomeAllocator(usize totalSize);
    
    void* alloc(usize size, usize alignment = 8) override;
    void free(void* ptr) override;
    void reset() override;  // se aplicável
    
    usize usedMemory() const override;
    usize peakMemory() const override;
};
```

### Implementação
- Algoritmo (pseudo-código)
- Complexidade (O)
- Fragmentação esperada
- Thread-safety

### Exemplo de Uso
```cpp
// Código real de uso
```

### Benchmarks de Referência
- Allocs/segundo
- Fragmentação após 1M allocs
- Overhead de memória
```

---

## [A ser preenchido conforme desenvolvimento da Fase 1]
