# 📑 Caffeine Planning & Architecture
**O Cérebro da Engine: Documentação de Design, Decisões e Roadmap.**

Esta subpasta contém os documentos fundamentais que regem o desenvolvimento da **Caffeine Engine**. Antes de escrever qualquer linha de código no `/src`, os Architects e Scribes consultam e atualizam os documentos contidos aqui.

---

## 📂 Conteúdo da Pasta

* **`manifesto.md`**: As leis fundamentais do projeto. Define as regras de segurança (Memory Safety), padrões de código (Style Guide) e a filosofia de desenvolvimento.
* **`master_plan.md`**: O roadmap detalhado dividido em fases (da Fundação Atômica até a IDE). Serve para rastrear o progresso macro do projeto.
* **`architecture_specs.md`**: Detalhes técnicos sobre a implementação do ECS, Job System e a camada RHI (Rendering Hardware Interface).
* **`memory_model.md`**: Especificação dos *Custom Allocators* e como a Caffeine gerencia a memória para evitar fragmentação.

---

## 🛠️ Fluxo de Planejamento

Seguimos o ciclo **R.I.C.O.** (Research, Idea, Conflict, Order):

1.  **Research (Pesquisa):** Investigamos como o SDL3 ou o C++ lidam com um problema específico.
2.  **Idea (Ideia):** Propomos uma solução que se encaixe na nossa filosofia de baixa dependência.
3.  **Conflict (Conflito):** Discutimos no Discord se essa solução fere o desempenho ou a portabilidade 3D futura.
4.  **Order (Ordem):** Documentamos a decisão final aqui nesta pasta e iniciamos a implementação.

---

## 🚦 Status das Fases

| Fase | Descrição | Status | Responsável |
| :--- | :--- | :--- | :--- |
| **0** | **Setup Inicial & Docs** | 🕒 Em Progresso | Guilda Codex |
| **1** | **Fundação Atômica (Memória/Tipos)** | 📅 Planejado | Architects |
| **2** | **Concorrência & Loop** | 📅 Planejado | Architects |
| **3** | **RHI & 2D Foundation** | 📅 Planejado | Artisans/Architects |

---

## ⚖️ Regras de Ouro para Documentação

1.  **Mantenha Simples:** Se a explicação de um sistema for mais complexa que o código, o sistema precisa ser simplificado.
2.  **Agnosticismo Dimensional:** Toda spec escrita aqui deve prever que o dado pode ser 2D ou 3D.
3.  **Sincronia:** Se o código mudar drasticamente, o documento de planejamento correspondente **deve** ser atualizado no mesmo Commit.

---
> *“Planejar é trazer o futuro para o presente, para que possamos fazer algo a respeito agora.”*
