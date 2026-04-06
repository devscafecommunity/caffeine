# 📜 Manifesto de Crescimento: Caffeine Engine

Este documento estabelece as diretrizes fundamentais para o desenvolvimento da engine, garantindo que o crescimento seja sustentável, seguro e de alta performance.

---

## 🛡️ 1. Regras de Segurança do Projeto

Segurança em uma engine C++ não trata apenas de ataques externos, mas de **estabilidade de memória** e **proteção do código-fonte**.

* **Gestão de Memória (The Golden Rule):** Proibido o uso de `new` e `delete` soltos no código. Toda alocação deve passar pelos *Custom Allocators* da Caffeine para evitar memory leaks e fragmentação.
* **Ownership Clara:** Utilizar `std::unique_ptr` ou `std::shared_ptr` apenas quando estritamente necessário. Preferir referências e ponteiros brutos (`raw pointers`) para acesso rápido, desde que o ciclo de vida do objeto seja garantido pelo *Core*.
* **Versionamento Blindado:** * Nenhum código entra na `main` sem um *Code Review* de pelo menos um outro Architect.
    * Uso obrigatório de `.gitignore` para não subir binários, arquivos de cache de IDE ou pastas de build.
* **Boundary Checks:** Em modo *Debug*, todos os containers da `Caffeine::Stdlib` devem validar limites de array. Em *Release*, essas checagens são removidas para performance máxima.

---

## 🏗️ 2. Regras de Planejamento e Desenvolvimento

A Caffeine é um projeto de "tempo livre", portanto, a eficiência é vital.

* **Simplicidade sobre Abstração:** Não crie abstrações complexas para problemas que ainda não temos. Siga o princípio **YAGNI** (*You Ain't Gonna Need It*).
* **Data-Oriented Design (DOD):** Sempre que possível, estruture os dados em arrays contíguos para favorecer o Cache da CPU. Evite hierarquias de classes muito profundas (Deep Inheritance).
* **O Ciclo de Feedback:**
    1.  **Draft:** O Scribe descreve a funcionalidade.
    2.  **Prototype:** O Architect implementa uma versão funcional "suja".
    3.  **Refactor:** O código é limpo e integrado aos padrões da engine.
    4.  **Audit:** O Oracle testa a performance e estabilidade.
* **Padrão de Nomenclatura (Style Guide):**
    * Classes e Structs: `PascalCase` (ex: `ThreadManager`)
    * Funções e Métodos: `camelCase` (ex: `initSdl()`)
    * Variáveis Privadas: `m_prefixo` (ex: `m_isRunning`)
    * Macros e Constantes: `UPPER_CASE` (ex: `MAX_THREADS`)

---

## 📈 3. Padronização de Crescimento

Para garantir que a Caffeine suporte jogos reais no futuro:

* **Independência de Plataforma:** O código do *Core* não deve saber o que é Windows ou Linux. Todo código específico de OS deve estar isolado em pastas `/platform` e abstraído pela API da Caffeine.
* **Documentação Obrigatória:** Nenhuma função nova é aceita sem um comentário explicativo no header (`.h`). Os **Scribes** são os guardiões dessa clareza.
* **Performance First:** Se uma nova funcionalidade reduzir o FPS do *Boilerplate* em mais de 1%, ela deve ser reavaliada ou movida para um sistema opcional.
* **Modularidade:** A engine deve ser composta por módulos independentes. Deve ser possível compilar a engine sem o módulo de áudio, por exemplo, sem quebrar o sistema de renderização.

---

## ⚖️ 4. O Compromisso da Guilda

> "Nós, os desenvolvedores da Caffeine, priorizamos a compreensão sobre a facilidade. Escrevemos código que nossos 'eus' do futuro não odiarão ler. Construímos para durar, um frame de cada vez."
Para colocar isso em prática, o primeiro passo técnico seria criar o arquivo `Caffeine.h` que define os tipos básicos da sua **Stdlib** (como `u32`, `f64`, `int32`, etc), garantindo que todos os membros usem os mesmos tipos de dados.

**Quer que eu ajude a redigir esse arquivo inicial de tipos básicos para padronizar o código?**
