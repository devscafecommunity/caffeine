# Reflexos

> **Resolver:** `Render::resolveForwardRenderFeatures`  
> **GPU:** `GpuSceneRenderer` (planar, SSR, probes por objeto)  
> **Shader:** `scene_lit.frag` (slots environment + probe + SSR)

Configura no componente **Forward Render Features** (Inspector) ou via `caffeine.forwardrender.*`.  
**Sem componente na cena**, `budgetSafeForwardDefaults()` usa **Probe** + screen-space trace ligado.

| Modo | O que faz | Custo |
|------|-----------|--------|
| **Off** | Apenas IBL / céu | Mínimo |
| **Planar** | Redesenho espelhado em Y (`planeY`), alvo HDR, `resolutionScale` | +1 cena (sem sombras no espelho) |
| **Screen-space** | SSR no shader (histórico de cor/profundidade) | Taps por pixel |
| **Probe** (default) | Cubemap HDR por objeto glossy (LRU, ≤ `maxReflectionProbes` por frame, 1 captura/frame) | 6 faces × resolução da probe |

**Only when camera settled** adia capturas caras enquanto a câmara move (TAA ainda converge).

## Probes por objeto

- Objetos com roughness baixa entram na fila; os mais próximos da câmara recebem probe.
- Cubemap `RGBA16F` com mips; amostragem no shader por draw (slot 9).
- Entidade em captura excluída do cubemap (`excludeEntity`) para evitar auto-reflexo.
- Faces do cubemap: projeção com flip Y para NDC SDL (top-left, y-up).

## SSR

- Lê cor e profundidade do **frame anterior** (por `viewId`).
- Parâmetros no `PostProcessComponent.screenSpaceReflections` ou no bloco Reflections do forward features.

## Céu (environment map)

`GpuEnvironmentMap` — equirectangular + mips (slot 11):

- Fonte: `SkyboxComponent` ativo ou `GpuSceneRenderOptions::environmentPath`.
- Especular: mip por roughness; difuso: mip mais baixo quando IBL ligado.

Onde probe/planar não têm geometria, cai para o céu.

## Material Editor

`MaterialPreviewRenderer`: mundo mínimo + `GpuSceneRenderer` + 1 probe. Ver [`../editor/material-editor.md`](../editor/material-editor.md).

## Ver também

- [`forward-render-features.md`](forward-render-features.md)
- [`materials-pbr.md`](materials-pbr.md)
- [`render-snapshot.md`](render-snapshot.md)
