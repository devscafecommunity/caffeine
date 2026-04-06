# 📑 Caffeine Planning & Architecture

**O Cérebro da Engine: Documentação de Design, Decisões e Roadmap.**

> ⚠️ Para uma visão **completa e consolidada** do projeto, consulte [`docs/MASTER.md`](../docs/MASTER.md).

---

## 📂 Conteúdo da Pasta

| Documento | Descrição | Relação |
|---|---|---|
| **`SPECS.md`** | Regras de desenvolvimento, estilo e padrões | Complemento direto ao MASTER §2, §5 |
| **`ROADMAP.md`** | Status das 6 fases e gates entre elas | Resumo executivo — MASTER §4 |
| **`architecture_specs.md`** | Especificações técnicas ECS, Job System, RHI | Técnico detalhado — MASTER §3 |
| **`memory_model.md`** | Especificações detalhadas dos Custom Allocators | Técnico detalhado — MASTER §8 |

---

## 🛠️ Fluxo de Planejamento

Seguimos o ciclo **R.I.C.O.** (Research, Idea, Conflict, Order):

1. **Research (Pesquisa):** Investigamos como o SDL3 ou o C++ lidam com um problema específico.
2. **Idea (Ideia):** Propomos uma solução que se encaixe na nossa filosofia de baixa dependência.
3. **Conflict (Conflito):** Discutimos no Discord se essa solução fere o desempenho ou a portabilidade 3D futura.
4. **Order (Ordem):** Documentamos a decisão final aqui nesta pasta e iniciamos a implementação.

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

1. **Mantenha Simples:** Se a explicação de um sistema for mais complexa que o código, o sistema precisa ser simplificado.
2. **Agnosticismo Dimensional:** Toda spec escrita aqui deve prever que o dado pode ser 2D ou 3D.
3. **Sincronia:** Se o código mudar drasticamente, o documento de planejamento correspondente **deve** ser atualizado no mesmo Commit.

---
> *"Planejar é trazer o futuro para o presente, para que possamos fazer algo a respeito agora."*
