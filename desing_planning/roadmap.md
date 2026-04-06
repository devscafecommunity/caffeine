# 🗺️ Caffeine Engine (Roadmap)

## Fase 1: A Fundação Atômica (Kernel & Memory)
*O objetivo aqui é criar independência total da `std` e garantir que a engine tenha controle absoluto do hardware.*

1.  **Caffeine Core Types & Macros:** * Definição de tipos de largura fixa (`u32`, `f64`, etc.).
    * Implementação de macros de plataforma (Detectar Windows/Linux/Mac).
    * Sistema de Assertions customizado para debug agressivo.
2.  **Memory Management System (Allocators):**
    * **Linear Allocator:** Para memória volátil que limpa a cada frame.
    * **Pool Allocator:** Para entidades repetitivas (partículas, projéteis).
    * **Stack Allocator:** Para níveis de jogo e escopos aninhados.
3.  **Custom Strings & Containers:**
    * Implementação de uma `StringView` e `FixedString` (evitando alocações dinâmicas).
    * `Caffeine::Vector` e `Caffeine::HashMap` otimizados para cache-locality.

---

## Fase 2: O Pulso e a Concorrência (Multithreading & Timing)
*Preparar a engine para utilizar todos os núcleos da CPU e manter um clock estável.*

1.  **High-Resolution Timer:**
    * Sistema de cronometragem com precisão de microssegundos usando o clock de alta frequência do hardware via SDL3.
2.  **Job System (Worker Threads):**
    * Criação de um sistema de "Fogueira de Tarefas". As threads ficam em repouso e acordam para processar "Jobs" (Física, IA, Carregamento).
    * Implementação de barreiras de sincronização e atômicos para evitar *Race Conditions*.
3.  **The Master Game Loop:**
    * Implementação do loop de tempo fixo (*Fixed Timestep*) para lógica e variável para renderização, garantindo que o jogo não acelere em PCs potentes.

---

## Fase 3: O Olho da Engine (RHI & 2D Foundation)
*Construir a camada de renderização agnóstica que hoje desenha pixels, mas amanhã desenha polígonos.*

1.  **Rendering Hardware Interface (RHI):**
    * Abstração sobre o SDL_GPU. A engine não chama "SDL_Draw", ela envia um `DrawCommand` para uma fila interna.
2.  **2D Batch Renderer:**
    * Sistema que agrupa milhares de sprites em um único envio para a GPU.
    * Suporte a Z-Buffer para ordenar quem está na frente (paralaxe e camadas).
3.  **Camera System (Agnóstico):**
    * Sistema de câmeras baseado em matrizes ($4 \times 4$). Para 2D, usamos Projeção Ortográfica. No futuro, apenas habilitamos a Projeção de Perspectiva.

---

## Fase 4: O Cérebro (ECS & Serialização)
*Onde as ideias dos Scribes e Architects se transformam em objetos vivos.*

1.  **Entity Component System (ECS):**
    * Entidades são apenas IDs. Os dados (Posição, Sprite, Vida) vivem em arrays contíguos para performance máxima de CPU.
2.  **Scene Graph & Serialization:**
    * Capacidade de salvar o estado de uma cena em um formato binário ou JSON (Caffeine Object Notation).
    * Base fundamental para o sistema de Save/Load e para a futura IDE.
3.  **Event Bus:**
    * Sistema de comunicação entre sistemas sem acoplamento (ex: o sistema de Som reage a um evento de "Morte" sem precisar saber quem morreu).

---

## Fase 5: Transição Dimensional (The 3D Leap)
*Expandindo os horizontes sem quebrar o que já foi feito.*

1.  **3D Math Extension:**
    * Ativação completa de cálculos de Quaternions e Matrizes de Transformação 3D.
2.  **Mesh Loading & Shaders:**
    * Implementação de loaders para modelos (.obj/.fbx) e suporte a shaders customizados (HLSL/GLSL) via SDL3.
3.  **Spatial Partitioning:**
    * Implementação de Quadtrees (2D) que podem evoluir para Octrees (3D) para otimizar colisões e visibilidade em mundos grandes.

---

## Fase 6: O Olimpo (Caffeine Studio IDE)
*Transformar o código em uma ferramenta visual para a comunidade.*

1.  **Embedded UI (Dear ImGui):**
    * Integração de uma interface de depuração dentro da própria engine para editar variáveis em tempo real.
2.  **The Scene Editor:**
    * Ferramenta visual para arrastar entidades, editar componentes e visualizar a hierarquia do mundo.
3.  **Asset Pipeline:**
    * Um sistema que processa imagens, sons e modelos brutos e os transforma em arquivos otimizados ".caf" para carregamento ultra-rápido.

---

### 🛡️ Nota:
Cada uma dessas fases deve passar por um **"Stress Test"**. Não avançaremos para a Fase 3 (Renderização) se a Fase 1 (Memória) apresentar vazamentos ou instabilidade.
