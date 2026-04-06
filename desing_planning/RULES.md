# 📜 Dev Manifesto: Regras e Padrões de Desenvolvimento

Este documento estabelece as **regras fundamentais** para o desenvolvimento da Caffeine Engine. Para contexto, filosofia e visão macro, consulte [`docs/MASTER.md`](../docs/MASTER.md) §2 e §5.

---

## 🛡️ 1. Regras de Segurança

### 1.1 Memória — The Golden Rule

```
╔════════════════════════════════════════════════════════════╗
║  PROIBIDO: new e delete soltos no código da aplicação.     ║
║  TODA alocação deve passar pelos Custom Allocators.          ║
╚════════════════════════════════════════════════════════════╝
```

| Padrão | Detalhes |
|---|---|
| **System Allocator** | Única exceção — usa `malloc` uma vez para bootstrap |
| **Linear Allocator** | Memória volátil por frame, `reset()` limpa tudo |
| **Pool Allocator** | Blocos de tamanho fixo (partículas, projéteis) |
| **Stack Allocator** | Escopos aninhados (níveis de jogo) |

### 1.2 Ownership e Ponteiros

| Situação | Abordagem |
|---|---|
| Ciclo de vida gerenciado pelo Core | `raw pointer` (acesso rápido) |
| Escopo estrito, único owner | `std::unique_ptr<T>` |
| Shared ownership necessária | `std::shared_ptr<T>` (raro — evitar) |
| Leitura sem ownership | `T&` ou `const T&` |
| Ponteiro opaco (outro sistema aloca) | `T*` com documentação clara |

### 1.3 Versionamento e Code Review

| Regra | Detalhe |
|---|---|
| **Code Review obrigatório** | Nenhum código entra na `main` sem approval de 1 Architect |
| **Branch naming** | `feature/fase-N-nome`, `fix/fase-N-descrição`, `docs/nome` |
| **Merge strategy** | Squash merge para manter histórico limpo |
| **Breaking changes** | Documentados com `BREAKING CHANGE:` no commit |
| **`.gitignore`** | Verificar a cada novo diretório criado |

### 1.4 Boundary Checks

| Modo | Comportamento |
|---|---|
| **Debug** | Todos os containers validam limites (`CF_ASSERT`) |
| **Release** | Checagens removidas — performance máxima |
| **CI/CD** | Sanitizers obrigatórios: ASan, MSan, TSan, UBSan |

---

## 🏗️ 2. Padrões de Código (Style Guide)

Para convenções completas de nomenclatura, consulte MASTER.md §5.

### 2.1 Resumo de Nomenclatura

```
Namespace:          Caffeine::ModuleName
Classe/Struct:      PascalCase          → ThreadManager
Função/Método:      camelCase           → initSdl()
Variável:           camelCase           → entityCount
Variável privada:   m_prefixo           → m_isRunning
Membro estático:    s_prefixo           → s_instanceCount
Constante/Macro:    UPPER_CASE          → MAX_THREADS
Header:             PascalCase.hpp      → LinearAllocator.hpp
Source:             PascalCase.cpp      → LinearAllocator.cpp
```

### 2.2 Estrutura de Arquivos

Cada módulo segue a estrutura:

```
module/
├── ModuleName.hpp      # Interface pública (API)
├── ModuleName.inl     # Implementação inline (se necessário)
└── ModuleName.cpp     # Implementação não-inline
```

### 2.3 Headers — Padrão de Inclusão

```cpp
#pragma once  // Não usar include guards tradicionais

// 1. Header do módulo (self)
#include "ModuleName.hpp"

// 2. Headers internos do mesmo módulo
#include "RelatedStruct.hpp"

// 3. Headers de outros módulos da Caffeine
#include <caffeine/memory/Allocator.hpp>
#include <caffeine/containers/Vector.hpp>

// 4. Headers externos (SDL3, etc.)
#include <SDL3/SDL.h>

// 5. Headers do sistema (evitar)
#include <cstring>
```

### 2.4 Documentação de APIs

Toda função pública **deve** ter documentação no header:

```cpp
// ============================================================================
// @brief  Aloca um bloco de memória do pool.
// @param  size     Tamanho do bloco a alocar (em bytes).
// @param  alignment Alinhamento do bloco (deve ser potência de 2).
// @return Ponteiro para o bloco alocado, ou nullptr se pool cheio.
// @note   Não causa fragmentação — alinhamento pode desperdiçar espaço.
// @see    LinearAllocator::reset()
// ============================================================================
void* allocate(usize size, usize alignment = 8);
```

---

## 📈 3. Padrões de Crescimento

### 3.1 Critérios para Avançar de Fase

Uma fase só termina quando **todos** os critérios são atingidos:

| Critério | Descrição |
|---|---|
| **Stress Test Passed** | Sistema não vaza memória nem crasha sob carga |
| **API Estável** | Interface pública não mudou nos últimos 2 ciclos |
| **Documentação Completa** | Specs e Doxygen 100% |
| **Code Review** | Pelo menos 1 Architect aprovou cada arquivo |
| **Testes** | Cobertura mínima 80% nos módulos core |

### 3.2 Modificações na API

| Tipo | Regra |
|---|---|
| **Adicionar** | Sempre permitido — adicionar, não modificar o existente |
| **Deprecar** | Manter por 2 fases com warning |
| **Remover** | Só na próxima versão `MAJOR` |

### 3.3 Portabilidade

| Regra | Detalhe |
|---|---|
| **Core agnóstico** | `/src/core/` não sabe o que é Windows ou Linux |
| **Platform layer** | Código específico de OS em `/src/platform/` |
| **Teste obrigatório** | Código novo compila em Windows E Linux E macOS |
| **CI/CD** | Build matrix: Windows (MSVC), Linux (GCC/Clang), macOS (Clang) |

---

## ⚖️ 4. O Compromisso da Guilda

> *"Nós, os desenvolvedores da Caffeine, priorizamos a compreensão sobre a facilidade. Escrevemos código que nossos 'eus' do futuro não odiarão ler. Construímos para durar, um frame de cada vez."*

### 4.1 O Que Isso Significa na Prática

| Comportamento | Evitar |
|---|---|
| Escrever código que seja óbvio | "Clever code" que precisa de comentário para ser entendido |
| Nomear coisas pelo que fazem | Nomes vagos (`data`, `temp`, `tmp`) |
| Preferir simples a elegante | Abstrações prematuras (YAGNI) |
| Documentar decisões, não obviedades | Comentários que repetem o código |
| Entender cada dependência que incluímos | `// TODO: remove this later` |

### 4.2 Quando Questionar

Se uma decisão de design atender **todos** estes critérios, está certa:

- [ ] Resolve um problema **real** (não hipotético)
- [ ] Não aumenta complexidade sem benefício mensurável
- [ ] Se encaixa na filosofia de baixa dependência
- [ ] Não prejudica performance em mais de 1%
- [ ] Pode ser explicada a um novo membro da guilda em 5 minutos

---

## 📋 Checklist de Código

Use antes de abrir PR:

```
□ Nenhum new/delete solto no código da aplicação
□ Allocator usado (Linear, Pool ou Stack)
□ Nomenclatura segue Style Guide
□ Variáveis privadas com prefixo m_
□ Macros em UPPER_CASE
□ Header com #pragma once
□ Doxygen em toda função pública
□ Ordem de includes correta
□ static_assert para tamanhos de tipos
□ Code Review pendente
□ Documentação atualizada (mesmo commit)
□ Compila em todas as plataformas
□ Stress test executado
□ Sem TODO sem issue associada
□ Sanitizers limpos (ASan, TSan, UBSan)
```
