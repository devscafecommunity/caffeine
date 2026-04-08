# ☕ Caffeine Engine
**A high-performance, custom-built game engine for the Codex Studio.**

Caffeine é uma engine de jogos desenvolvida em **C++** sobre a camada do **SDL3**. O foco do projeto é o controle total sobre o hardware, priorizando concorrência multithread, gerenciamento de memória customizado e otimização gráfica de baixo nível.

---

## 📚 Documentação

| Documento | Descrição |
|---|---|
| **[`docs/MASTER.md`](docs/MASTER.md)** | **Documentação completa** — filosofia, arquitetura, fases, convenções, fluxo de trabalho |
| **[`desing_planning/`](desing_planning/)** | Especificações de design e specs técnicas |
| **[`desing_planning/roadmap.md`](https://github.com/devscafecommunity/caffeine/blob/main/desing_planning/ROADMAP.md)** | Roadmap detalhado das 6 fases |
| **[`desing_planning/RULES.md`](https://github.com/devscafecommunity/caffeine/blob/main/desing_planning/RULES.md)** | Leis do projeto, regras de segurança e estilo |

> ### **[Kanban do Projeto](https://github.com/orgs/devscafecommunity/projects/3/views/1)**
> Kanban para toda a implementação vinda do projeto.

> ⚠️ **O código-fonte ainda não foi criado.** O projeto está em **Fase 0** (Setup & Documentação). Comece pela [`docs/MASTER.md`](docs/MASTER.md) para entender a visão completa.

---

## 🏛️ Filosofia de Desenvolvimento
Ao contrário de engines "bloated", a Caffeine é construída sob o princípio da **transparência**.
* **Zero Dependency:** Reduzir ao máximo a dependência da `std` padrão, criando uma `stdlib` própria focada em jogos.
* **Data-Oriented:** Foco em performance de cache e processamento paralelo.
* **Caffeine-Powered:** Feita por desenvolvedores que preferem entender cada byte que passa pela CPU.

---



## 🛠️ Roadmap Técnico

### 1. Sistema de Concorrência (Threading)
* Implementação de um **Job System** baseado em workers.
* Distribuição de tarefas pesadas (física, animação) entre núcleos da CPU.
* Primitivas de sincronização *lock-free* para evitar gargalos.

### 2. Otimização Gráfica
* Integração profunda com a nova **GPU API do SDL3**.
* Sistema de **Batch Rendering** para redução de Draw Calls.
* Gerenciamento eficiente de VRAM e Texture Streaming.

### 3. Core & Game Loop
* Loop de tempo fixo para lógica/física e variável para renderização.
* Interpolação de frames para garantir fluidez visual (60fps+).
* Sistema de eventos de alta precisão.

### 4. Caffeine Stdlib (Custom Lib)
* **Custom Allocators:** Linear, Pool e Stack allocators para evitar fragmentação de memória.
* **Contêineres Otimizados:** Implementação própria de vetores e strings focados em performance.

---

## 🚀 Como Compilar (WIP)
*Atualmente em fase de desenvolvimento Alpha.*

**Requisitos:**
* Compilador C++20 ou superior.
* SDL3 (Latest Build).
* CMake 3.20+.

```bash
git clone https://github.com/codex-studio/caffeine
cd caffeine
mkdir build && cd build
cmake ..
make
```

---
> *“Caffeine: Because great games are built on strong code and a lot of coffee.”*

