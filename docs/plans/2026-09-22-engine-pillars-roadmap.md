# 🏛️ Engine Pillars — Roadmap de Sistemas Fundamentais

> **Objetivo:** definir de forma **sólida e verificável** os subsistemas que a Caffeine Engine deve ter como base de produção — independentemente de features de editor ou plugins.  
> **Critério de “completo”:** cada pilar tem requisitos, critérios de aceitação e estado atual documentados.  
> **Última atualização:** 2026-09-22

Este documento complementa:
- [`2026-09-21-rendering-roadmap.md`](2026-09-21-rendering-roadmap.md) — detalhe técnico de rendering 3D
- [`../HISTORY.md`](../HISTORY.md) — fases históricas de desenvolvimento
- [`../../planning/caffeine-studio-ide.md`](../../planning/caffeine-studio-ide.md) — milestones do Doppio (IDE)

---

## Prioridades

| Tier | Significado |
|------|-------------|
| **P0 — Core** | Fundação obrigatória para jogos 3D sérios. Sem isto a engine não é “production-ready”. |
| **P1 — Production** | Necessário para títulos completos (multiplayer, animação rica, serialização avançada). |
| **P2 — Future** | Planeado, mas não bloqueia o primeiro release 3D. |
| **P3 — Maybe** | Desejável; depende de recursos e validação de mercado. |

```mermaid
flowchart LR
    subgraph P0["P0 — Core"]
        R1[Phong 3D + sombras + bump]
        E1[Eventos + timers + tipos custom]
        PHY[Bullet3 rigid body]
        AUD[OpenAL 3D]
        ECS[ECS próprio + editor/runtime]
        RES[Handles + refcount]
    end
    subgraph P1["P1 — Production"]
        NET[Networking + serialização]
        ANIM[Animation subsystem]
        SCN[Scene + tipos de utilizador]
        PBR[Deferred PBR]
    end
    subgraph P2["P2 — Future"]
        SCR[C# ou Python]
    end
    subgraph P3["P3 — Maybe"]
        AND[Android]
    end
    R1 --> PBR
    ECS --> NET
    ECS --> SCN
    RES --> NET
    R1 --> ANIM
```

---

## P0 — Core

### 1. Rendering 3D Phong (sombras dinâmicas, bump mapping, câmaras)

**Estado:** 🟡 Parcial — pipeline GPU híbrido; Lambert/PBR parcial; sombras CPU no editor; normal maps não ligados.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Iluminação Phong (ambient + diffuse + specular) | Materiais com `shininess`, luzes direcionais/pontuais estáveis a 60 FPS |
| Sombras dinâmicas | Shadow mapping GPU para luz direcional principal; sem fallback CPU no caminho default |
| Bump / normal mapping | Normal maps em meshes e terreno; tangentes corretas no vertex buffer |
| Sistema de câmaras 3D | Componente `Camera3D` + follow/orbit/FPS; editor e runtime partilham a mesma API |
| Pipeline único GPU | Sem dual-path CPU raster no viewport shaded (ver rendering roadmap P0) |

**Dependências:** RHI, mesh loading, materiais, `Camera3DComponent`.

**Relação com PBR:** Phong é o **degrau imediato** (P0 rendering). O deferred PBR (P1) evolui sobre a mesma infraestrutura de G-buffer, shadow maps e materiais — não substitui o trabalho de câmaras, sombras e normal mapping.

**Docs:** [`rendering/camera-3d.md`](../rendering/camera-3d.md), [`2026-09-21-rendering-roadmap.md`](2026-09-21-rendering-roadmap.md)

---

### 2. Sistema de eventos (timers + tipos customizados)

**Estado:** 🟡 Parcial — `EventBus` tipado com `publish` / `publishDeferred` existe; timers de engine e tipos custom registáveis ainda limitados.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Pub/sub tipado | `subscribe<T>` / `publish<T>` com handles canceláveis |
| Eventos diferidos | Fila processada em `dispatch()` no game loop |
| **Timers** | `scheduleOnce`, `scheduleRepeating`, cancelamento por handle; integração com fixed timestep |
| **Tipos custom** | API para registar novos tipos de evento (ID estável ou RTTI controlado) sem recompilar o core |
| Thread-safety documentada | Regras claras: o quê pode ser chamado de jobs vs main thread |

**Dependências:** Game loop, `EventBus`.

**Docs:** [`core/events.md`](../core/events.md), [`core/timer.md`](../core/timer.md)

---

### 3. Física rigid body (Bullet3)

**Estado:** 🔴 Não iniciado (3D) — Physics 2D custom existe; menções a Jolt/Bullet em planning apenas.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Integração **Bullet3** | `btDiscreteDynamicsWorld`, shapes (box, sphere, capsule, mesh convex), rigid bodies |
| ECS bridge | `RigidBody3D`, `Collider3D`, sync transform ↔ physics (editor + play mode) |
| Terreno | Heightfield ou mesh collision para terreno sculptado |
| Debug draw | Wireframes de colliders no viewport (toggle) |
| Determinismo razoável | Fixed step; documentar limitações cross-platform |

**Dependências:** ECS, math, terreno collision mesh (`TerrainCollisionMeshBuilder`).

**Nota:** Bullet3 escolhido explicitamente como backend; abstração `IPhysicsWorld3D` para não acoplar ECS ao Bullet em todo o código.

---

### 4. Áudio 3D (OpenAL)

**Estado:** 🟡 Parcial — áudio 2D/spatial básico; OpenAL não integrado como backend principal.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Backend **OpenAL** | Contexto, buffers, sources, listener |
| Formatos comuns | WAV, OGG, FLAC (via decoder); MP3 opcional |
| Áudio 3D | Posição/orientação de listener e sources; atenuação e cone |
| ECS | `AudioEmitter3D`, `AudioListener` ligados a `Transform` / `Position3D` |
| Streaming | Música/long-form sem carregar ficheiro inteiro na RAM |

**Dependências:** Asset manager, ECS, game loop.

**Docs:** [`audio/audio-system.md`](../audio/audio-system.md)

---

### 5. ECS próprio, integrado em runtime e editor

**Estado:** 🟢 Avançado — `World` archetype-based, editor Doppio, runtime `caffeine-runtime`, serialização de cena.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| ECS **escrito pela equipa** | Sem dependência de flecs/EnTT em produção |
| Editor ↔ runtime | Mesmos componentes serializáveis; play mode não corrompe cena |
| Command buffer | Create/destroy seguro durante iteração |
| Reflection mínima | Inspector e serializers gerados ou registados por tipo |
| Systems pipeline | Ordem determinística; prioridades documentadas |

**Dependências:** Memory, scene serializer.

**Docs:** [`ecs/README.md`](../ecs/README.md), [`ecs/scene.md`](../ecs/scene.md)

---

### 6. Gestão de recursos com handles e reference counting

**Estado:** 🟡 Parcial — `AssetManager`, caches de mesh/textura; handles tipados e refcount explícito incompletos.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| **Handles opacos** | `TextureHandle`, `MeshHandle`, etc. — sem ponteiros expostos ao gameplay |
| **Reference counting** | Acquire/release; unload automático quando refcount = 0 |
| Resistente a erros | Load falhado → handle inválido + log; sem crash em uso de handle morto |
| Hot-reload | Recarregar asset mantém handles válidos ou invalidação explícita |
| Thread-safe load | Async load com callback ou future; main thread só consome handles prontos |

**Dependências:** Job system, file I/O, formatos `.caf`.

**Docs:** [`assets/asset-manager.md`](../assets/asset-manager.md)

---

## P1 — Production

### 7. Networking + serialização custom de objetos

**Estado:** 🔴 Não iniciado.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Transporte | UDP com camada de confiabilidade (ou TCP opcional para lobby) |
| **Serialização custom** | Formato binário versionado para componentes/entidades; sem JSON em hot path |
| Replicação ECS | Spawn/despawn/update de entidades autoritativas |
| Interpolação / predição | Documentada para transforms e física |
| Editor opcional | Painel de debug de rede (latency, packet loss) |

**Dependências:** ECS, handles, event bus, scene serialization.

---

### 8. Deferred rendering, physically-based (PBR)

**Estado:** 🟡 Parcial — shader graph com nó PBR; pipeline deferred não completo.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| G-buffer | Albedo, normal, metallic-roughness, depth |
| Deferred lighting | Múltiplas luzes sem custo linear por objeto |
| PBR (GGX / Fresnel) | Materiais energy-conserving; IBL opcional |
| Coexistência com Phong | Migração gradual; materiais legacy convertíveis |

**Dependências:** P0 rendering (sombras, normal maps, pipeline GPU único).

**Docs:** [`2026-09-21-rendering-roadmap.md`](2026-09-21-rendering-roadmap.md) fases P3–P6.

---

### 9. Subsistema de animação

**Estado:** 🟡 Parcial — animation 2D, skeletal stubs, timeline no editor.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Skeletal 3D | Bones, skinning GPU, import glTF |
| State machines | Transitions, blend trees |
| Animation 2D | Sprite clips (já parcial) |
| Timeline editor | Keyframes sincronizados com play mode |
| Eventos de animação | Footsteps, hit frames via event bus |

**Dependências:** ECS, mesh loading, rendering.

**Docs:** [`animation/skeletal-animation.md`](../animation/skeletal-animation.md)

---

### 10. Scene management + serialização de tipos de utilizador

**Estado:** 🟡 Parcial — `SceneSerializer`, blobs por componente; tipos user-defined limitados.

**Deve ter de forma sólida:**

| Requisito | Critério de aceitação |
|-----------|----------------------|
| Cenas hierárquicas | Load/save, streaming opcional |
| **Tipos de utilizador** | API para registar serializers de componentes custom (plugins, gameplay) |
| Versionamento | Migração de formatos `.cscene` / `.caf` |
| Prefabs | Instâncias com override por entidade |
| Async load | Progress callback; sem frame hitch |

**Dependências:** ECS, asset handles, editor.

**Docs:** [`ecs/scene.md`](../ecs/scene.md)

---

## P2 — Future

### 11. Scripting via C# ou Python

**Estado:** 🟡 Lua em produção; C#/Python não decidido.

**Direção:**

| Opção | Prós | Contras |
|-------|------|---------|
| **C#** | Ecossistema Unity-like, tooling maduro | Runtime (Mono/CoreCLR), GC, build |
| **Python** | Prototipagem rápida | Performance, embedding, GIL |

**Critério de aceitação (quando iniciar):** VM/embed estável, hot-reload, bindings ECS gerados ou reflection, sandbox documentado.

**Nota:** Lua permanece suportado; C#/Python seria **camada adicional**, não substituição imediata.

**Docs:** [`scripting/scripting.md`](../scripting/scripting.md)

---

## P3 — Maybe

### 12. Suporte a dispositivos Android

**Estado:** 🔴 Não planeado em detalhe — Linux/desktop é o alvo atual.

**Pré-requisitos antes de comprometer:**

- RHI portável (Vulkan/ GLES via SDL3)
- Input touch + safe areas
- Asset pipeline mobile (compressão ASTC/ETC2)
- Bullet + OpenAL em ARM
- CI com build NDK

**Critério para promover a P1:** produto ou parceiro com requisito Android confirmado.

---

## Matriz resumo

| # | Pilar | Tier | Estado | Backend / notas |
|---|-------|------|--------|-----------------|
| 1 | Phong 3D + sombras + bump + câmaras | P0 | 🟡 Parcial | GPU pipeline; ver rendering roadmap |
| 2 | Eventos + timers + tipos custom | P0 | 🟡 Parcial | `EventBus` existe |
| 3 | Rigid body 3D | P0 | 🔴 Não iniciado | **Bullet3** |
| 4 | Áudio 3D | P0 | 🟡 Parcial | **OpenAL** |
| 5 | ECS próprio + editor/runtime | P0 | 🟢 Avançado | Core da engine |
| 6 | Handles + refcount | P0 | 🟡 Parcial | `AssetManager` |
| 7 | Networking + serialização | P1 | 🔴 Não iniciado | Binário custom |
| 8 | Deferred PBR | P1 | 🟡 Parcial | Após Phong sólido |
| 9 | Animação | P1 | 🟡 Parcial | 2D + skeletal |
| 10 | Scene + tipos user | P1 | 🟡 Parcial | `SceneSerializer` |
| 11 | C# / Python | P2 | 🔴 Futuro | Lua atual |
| 12 | Android | P3 | 🔴 Maybe | Depende de RHI mobile |

---

## Ordem de implementação sugerida

1. **Handles + refcount** (desbloqueia assets estáveis em todo o resto)
2. **Phong + sombras + bump + câmaras** (rendering roadmap P0–P2)
3. **Eventos: timers + tipos custom** (desbloqueia gameplay e networking)
4. **Bullet3** (colisão 3D com terreno)
5. **OpenAL 3D** (paridade sensorial)
6. **Scene + serializers user** (plugins e jogos)
7. **Animação skeletal completa**
8. **Networking**
9. **Deferred PBR**
10. C# / Python / Android conforme prioridade de produto

---

## Como usar este documento

- **Antes de implementar** um subsistema: abrir o pilar correspondente e verificar critérios de aceitação.
- **Ao fechar uma feature:** atualizar coluna “Estado” neste ficheiro e linkar doc técnica em `docs/<módulo>/`.
- **Rendering:** detalhe fase-a-fase continua em [`2026-09-21-rendering-roadmap.md`](2026-09-21-rendering-roadmap.md); este doc define *o quê*; aquele define *como*.
