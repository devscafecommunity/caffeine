# Material Editor

> **Painel:** Doppio → Material Editor  
> **Core:** `Assets::MaterialSurface`, `MaterialFile` (`.mat`), `MaterialCache`, `Render::MaterialPreviewRenderer`  
> **Shader:** `scene_lit.frag` / `terrain_lit.frag`

O editor de materiais vive no **core** da engine. O Doppio só abre o painel, publica alterações no `MaterialCache` e mostra a textura GPU devolvida pelo preview renderer.

---

## Fluxo de dados

1. Abrir ou criar um `.mat` (`CAFMAT2`).
2. Sliders e combos alteram `MaterialSurface` em memória.
3. `publishLive()` grava no `MaterialCache` com a chave do ficheiro (ou `__caffeine_material_preview__.mat` durante edição).
4. O viewport e o preview leem a revisão do cache; o carimbo da cena (`EditorPanelUtils`) inclui bytes do material para redesenhar sem mover a câmara.
5. **File → Save** serializa `CAFMAT2` no disco.

Browse de texturas usa `Widgets::AssetField` + `BrowseSession` (mesmo fluxo que o Asset Browser).

---

## Secções do painel

| Secção | Campos |
|--------|--------|
| **Preset** | Lista agrupada por categoria (`MaterialPresets.hpp`: Mirror, Chrome, Gold, Clear Glass, …) |
| **Surface** | Albedo (RGBA), metallic, roughness, reflectance |
| **Maps & UV** | Albedo / normal / ORM / emission maps; normal strength; AO strength; UV tiling & offset |
| **Transparency** | Alpha mode (Opaque / Cutout / Blend), cutoff, transmission, IOR |
| **Clear coat** | Clearcoat, coat roughness |
| **Sheen** | Cor, roughness (tecido/veludo) |
| **Iridescence** | Força, espessura do filme (nm), IOR do filme |
| **Emission** | Cor, strength, mapa de emissão |

Albedo no editor é **sRGB**; o renderer lineariza na GPU.

---

## Pré-visualização GPU

`MaterialPreviewRenderer` monta um `World` mínimo (esfera + chão opcional, luz direcional, céu da cena ativa) e chama `GpuSceneRenderer` com:

- HDR + stack de pós-processamento (TAA por defeito)
- **Reflection probes** (até 1 probe, resolução 32–256)
- Mesmo `scene_lit` que o viewport

Controles **Orbit**, **Height** e **Floor** no painel. O renderer pede frames extra enquanto `needsAnotherFrame()` (TAA, probes, auto-exposure).

Ver [`../rendering/materials-pbr.md`](../rendering/materials-pbr.md) e [`../rendering/reflections.md`](../rendering/reflections.md).

---

## Ficheiros

| Ficheiro | Função |
|----------|--------|
| `src/assets/MaterialTypes.hpp` | `MaterialSurface`, `MaterialAlphaMode` |
| `src/assets/MaterialFile.cpp` | Leitura/escrita `CAFMAT2`, `sanitizeMaterialSurface` |
| `src/assets/MaterialPresets.hpp` | Presets nomeados |
| `src/render/MaterialPreviewRenderer.cpp` | Preview offscreen |
| `src/editor/MaterialEditorPanel.cpp` | UI ImGui |
