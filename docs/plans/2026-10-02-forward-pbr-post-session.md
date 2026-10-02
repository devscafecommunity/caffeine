# Sessão 2026-10-02 — Forward PBR, reflexos, pós-processamento GPU

> Resumo técnico do bloco que move renderização e materiais para o **core** (`caffeine-core`), com o Doppio como cliente.

---

## Pipeline de renderização

- Cena **HDR linear** (`RGBA16F`) → `PostProcessStack` → saída `RGBA8`.
- **TAA** por defeito (`PostProcessStack::defaults`, `antiAliasing.mode = 1`); jitter na projeção.
- **SSAO** suave nos defaults quando não há `PostProcessComponent` na câmara.
- Alvos por vista (ping-pong) para histórico TAA/SSR; `renderScale` 0.25–2× no editor.
- Céu GPU (`sky.frag`), grelha GPU (`grid.frag`), sem overlays ImGui de “fake post”.
- Sombras: atlas direcional 4096, cascatas 2048, spot 1024, point faces 512; projeção z remapeada para SDL `[0,1]`; PCF 12-tap com bias por texel.

## Materiais PBR (`scene_lit.frag`)

- Metallic-roughness + clearcoat, sheen, iridescence, transmission/IOR, alpha modes.
- Pass translúcido: cópia do opaco + blend / transmissão.
- Texturas sRGB→linear na CPU; ORM, normal strength, emission map.

## Reflexos (`ReflectionMode::Probe` por defeito)

- Probes por objeto (LRU, captura incremental, cubemap HDR com mips).
- SSR opcional (post + features); reflexão planar HDR; `excludeEntity` na captura.
- Material preview e snapshot usam a mesma stack.

## ECS opcional (não são settings do editor)

- `ForwardRenderFeaturesComponent` — instancing, IBL, oclusão coarse, reflexos, volumétricos.
- `PostProcessComponent` — stack modular executado na GPU.
- `PostProcessFields.hpp` — reflexão `effect.field` para Lua/C++.

## Scripting Lua

- `caffeine.postprocess.*` completo (`get`/`set`/`getValue`/`setValue`/`effects`/`fields`/`blend`/`handle`).
- `customEffectScript` → `onPostProcess(entityId, dt, api)` em play mode (`ScriptSystem`).
- `caffeine.forwardrender.*` (`ForwardRenderBindings.cpp`).

## Editor (Doppio)

- Material Editor: presets, todas as secções PBR, `AssetField` reutilizável.
- `MaterialPreviewRenderer` substitui `PreviewRenderer` CPU.
- Settings → Viewport → **Render scale** (persistido).
- `cameraFarPlane()` dinâmico; cap de framebuffer 3840.
- Inspector: Forward Render Features + Post Process (UI partilhada em `PostProcessEditorUI.hpp`).

## Ferramentas e testes

- `caffeine-render-snapshot` — PNG de regressão.
- Testes: `[material]`, `[forwardrender]`, `[render]`, `test_mesh_lod`, `test_physics3d`, etc.

## Documentação

- `docs/rendering/*` (PBR, reflexos, forward features, post, snapshot, mesh LOD, volumétricos).
- `docs/editor/material-editor.md`
- `docs/physics/physics-3d.md`
