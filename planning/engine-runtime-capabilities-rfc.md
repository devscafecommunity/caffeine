# Capacidades de runtime (RFC) — lacunas genéricas

> **Status:** Aprovado (direção) — implementação por fases  
> **Âmbito:** Caffeine runtime + core (Doppio fora de scope)  
> **Origem:** feedback de migração de jogo 2D complexo; refinado com princípio **engine neutra**

---

## Princípios (obrigatórios em todas as entregas)

1. **Opcional e modular** — cada capacidade pode ser ignorada por projetos que não precisam; sem “taxa” obrigatória em startup ou em APIs core.
2. **Sem viés de produto** — nada nomeado ou desenhado para um título, provedor de mapas, ou género; extensões via componentes, serviços e Lua, não hardcode de domínio.
3. **Integração seamless** — reutilizar ECS, `UISystem`, input, render RHI, assets e docs existentes; evitar segundo pipeline paralelo (ex.: UI só ImGui no shipping sem caminho RHI).
4. **Editor vs runtime** — extrair do Doppio apenas o que for reutilizável; contratos documentados em `docs/scripting/runtime-apis.md`.
5. **Runtime native ≠ editor plugin** — `PluginAPI` / `.so` do IDE permanece separado de módulos nativos carregados em play mode.

---

## Estado atual (referência rápida)

| Área | Já existe | Falta |
|------|-----------|--------|
| UI retained | `UISystem`, componentes, `hitTest`, runtime desenha widgets | Lua/HUD pipeline, contrato play-mode, render fora de ImGui (meta longo prazo) |
| Picking | Editor (`pickEntity2D`, raycast), UI `hitTest`, colisão 2D | Serviço unificado runtime + Lua |
| Spatial | `Octree` 3D | Serviço 2D opcional, LOD 2D política genérica |
| Scripting | `loadScript`, search roots | `require`, `AssetText`, `fixedUpdate` / tempo |
| Mapas | Terrain 3D, tilemap editor (grelha) | Componente mapa **genérico** (ver §2) |
| Overlays 2D | Debug/gizmos editor | Batch vectorial play mode + API Lua |
| Native play | — | `dlopen` + contrato C + bridge Lua (P2) |
| Game loop helpers | `onUpdate` | Scene stack / `timeScale` fino (opcional) |

---

## 1. Módulos nativos no runtime

**Decisão:** Aprovar como **P2**.

- Carregar bibliotecas do projeto (`native/` ou path no `project.caffeine`) com ciclo de vida ligado ao play mode.
- Contrato C estável documentado; ponte Lua genérica (`caffeine.native.call` ou equivalente), sem namespaces por jogo.
- Documentar threading (main thread vs worker) e ABI/CI desde o início.

**Não fazer:** misturar com plugins do Doppio.

**Pastas sugeridas:** `src/script/` (bridge), `docs/scripting/native-modules.md`.

---

## 2. Mapas 2D — componente genérico (não mapa “da vida real”)

**Decisão:** **Não** implementar MapView Mercator, HTTP tiles, OSM, ou pipeline georreferenciado fixo — isso está **fora de scope** da engine e enviesa terceiros.

**Em vez disso:**

- Componente e/ou serviço **genérico** (ex. `MapLayer`, `TiledSurface2D`) para organizar **camadas de conteúdo 2D em espaço mundo** com:
  - bounds / região lógica (coordenadas definidas pelo projeto, não WGS84 obrigatório);
  - hooks para **carregamento assíncrono de chunks** (callback C++/Lua: “preciso desta região” → utilizador devolve textura/mesh/dados);
  - integração opcional com **LOD** e **spatial queries** (§6) quando o projeto quiser escala.
- Zoom/pan como **comportamento de câmara 2D** ou helper no componente, sem assumir projeção específica.
- Qualquer HTTP, lat/lon, ou raster mundial fica **100% no jogo/plugin do utilizador**, usando os hooks acima.

**Beneficia:** estratégia, overworld, dashboards — cada um com o seu sistema de coordenadas e fonte de dados.

**Prioridade:** **P1** (depois de overlays + picking + contrato UI), não bloqueante para P0.

**Pastas sugeridas:** `src/map/` (leve, sem deps de rede), ou extensão ECS em `src/ecs/components/`.

---

## 3. Overlays vectoriais 2D em play mode

**Decisão:** Aprovar — **P0**.

- Camada `Overlay2D` (batch) no forward 2D: linhas, arcos, círculos, polígonos, trails; cor/alpha; espaço world ou screen.
- API Lua: `caffeine.draw2d.*` (nomes finais na doc).
- Ordem de desenho documentada relativamente a sprites e UI.
- Implementar sobre **RHI/render 2D**, não depender de ImGui `ImDrawList` no runtime de produção (ImGui pode permanecer no editor/preview).

**Pastas:** `src/render/Overlay2D.*`, bindings em `src/script/`.

---

## 4. UI de runtime (retained mode)

**Decisão:** Aprovar — **P0**, com reserva: **evoluir o que existe**, não reescrever.

- Base: `UISystem`, `UIComponents`, `UIRenderer::drawWidgets`, testes em `tests/test_ui.cpp`.
- Entregar: contrato play-mode, exposição Lua (criar/ligar widgets ou consumir HUD serializado do Doppio), input routing e foco.
- Documentar relação `HudEditorPanel` → runtime.

**Pastas:** `src/ui/`, `docs/scripting/runtime-apis.md`.

---

## 5. Picking em coordenadas de mundo

**Decisão:** Aprovar — **P0** (junto com §3).

- Serviço `PickQuery`: screen → world → candidatos (entidades, layers registados, callbacks Lua).
- Refatorar lógica útil do editor (`SceneViewport`) para core reutilizável; runtime não depende do viewport do Doppio.
- Colisão ECS permanece caminho opcional, não o único.

**Pastas:** `src/scene/PickQuery.hpp` ou `src/input/`.

---

## 6. Escala: marcadores, LOD, culling

**Decisão:** Aprovar — **P2** (ativar quando projetos precisarem).

- Política de LOD por distância/zoom **genérica** (sprites / instancing 2D).
- Índice espacial 2D opcional (grid/quadtree) como **serviço**, utilizável por render, picking e §2.
- Reutilizar ideias de `Octree` onde fizer sentido; não forçar 3D em jogos 2D.
- Budgets documentados (orientação, não limites rígidos no core).

**Pastas:** `src/spatial/`, ligação opcional em `src/map/` e render.

---

## 7. Scripting: módulos Lua e dados tabulares

**Decisão:** Aprovar — **P1**.

- `package.path` alinhado a `project.caffeine` → `scripts/` (comportamento `require` previsível).
- `AssetText.load` (ou equivalente) para JSON/CSV/texto em `assets/`, cache e hot-reload opcional no editor.
- Não duplicar APIs de asset já existentes; consolidar na doc.

**Pastas:** `src/script/`, `docs/scripting/`.

---

## 8. Loop de jogo: estados, tempo e determinismo

**Decisão:** Aprovar — **opcional fino**, **P1–P2**.

- Helpers `GameState` / scene stack (push/pop/pause), independentes de domínio.
- Relógio pausável, `timeScale`; `fixedUpdate(dt)` alinhado ao passo de física quando existir timestep fixo.
- Hook de seed/replay para testes — sem impor arquitetura de jogo completa.

**Pastas:** `src/core/` ou `src/runtime/`, doc em `runtime-apis.md`.

---

## Priorização de implementação (engine)

| Ordem | Entrega | Notas |
|-------|---------|--------|
| 1 | §5 Picking + §3 Overlay2D | Mesmo espaço de coordenadas; desbloqueia interação em mapas custom |
| 2 | §4 Contrato UI play + Lua/HUD | Sobre `UISystem` existente |
| 3 | §7 `require` + AssetText | Higiene projetos médios |
| 4 | §2 Componente mapa genérico | Hooks async/LOD; **sem** HTTP/Mercator na engine |
| 5 | §8 Game state / tempo | Fino, opt-in |
| 6 | §6 Spatial/LOD + §1 Native | Quando escala ou sim fora da VM exigir |

---

## Critérios de aceitação transversais

- [x] Projeto mínimo (sprite + script) não linka nem inicializa módulos não usados.
- [x] APIs novas aparecem em `docs/scripting/runtime-apis.md` com exemplos neutros.
- [x] Testes unitários onde há lógica pura (`hitTest`, pick, layout, require paths).
- [x] Nenhum provedor externo, URL template, ou tipo de coordenada geográfica hardcoded no core.

---

## Changelog do RFC

- **2025-10-14:** Consolidação pós-revisão — §2 redefinido (mapa genérico, liberdade do utilizador); §1–8 aprovados com princípios de neutralidade e modularidade.
- **2026-10-14:** Phase 1 + Phase 2 implemented. Public patch notes (English): [docs/releases/runtime-capabilities-patch-notes.md](../docs/releases/runtime-capabilities-patch-notes.md).
