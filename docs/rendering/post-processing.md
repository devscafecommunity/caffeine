# 🎨 Post-Processing Stack

> **Component:** `PostProcessComponent` on camera entities  
> **Plugin:** `post_processing_plugin.so`  
> **Lua API:** `caffeine.postprocess.*`  
> **Preview:** ImGui overlay (editor); full GPU stack planned in rendering roadmap P3+

---

## Filosofia

O stack é **modular** — cada efeito é um módulo independente com toggle e parâmetros. Presets (Cinematic, Horror, Arcade) são **benchmark looks** opcionais, não tipos de jogo.

---

## Módulos de efeito

| Módulo | Categoria | Parâmetros principais |
|--------|-----------|----------------------|
| **Anti-aliasing** | Pre-processing | FXAA / TAA, sharpness |
| **Ambient occlusion** | Pre-processing | intensity, radius, bias |
| **Auto exposure** | Color | min/max, adaptation speed |
| **Color grading** | Color | exposure, contrast, saturation, temperature, tint |
| **Bloom** | Image | intensity, threshold, scatter |
| **Depth of field** | Image | focus distance, aperture, max blur |
| **Motion blur** | Image | intensity, max velocity |
| **Chromatic aberration** | Image | intensity |
| **Lens distortion** | Image | intensity |
| **Grain** | Image | intensity, size |
| **Vignette** | Image | intensity, smoothness |
| **Screen space reflections** | Screen space | intensity, max roughness |
| **Deferred fog** | Atmosphere | density, start/end, color |

---

## Editor

1. Selecionar entidade **Camera2D** ou **Camera3D**
2. Menu **Effects → Add Post Process to Camera** ou botão no painel
3. Ativar módulos individualmente na secção correspondente
4. (Opcional) Aplicar benchmark look como ponto de partida

UI partilhada: `include/caffeine/postprocess/PostProcessEditorUI.hpp`

---

## Scripting

### Controlar efeitos em runtime

```lua
caffeine.postprocess.setEnabled(cameraId, true)
caffeine.postprocess.setExposure(cameraId, 1.2)
caffeine.postprocess.setBloom(cameraId, 0.3)
caffeine.postprocess.setVignette(cameraId, 0.4)
caffeine.postprocess.enableEffect(cameraId, "motionBlur", true)
caffeine.postprocess.enableEffect(cameraId, "ssr", true)
caffeine.postprocess.setCustomScript(cameraId, "scripts/postprocessing/custom_effect.lua")
local snapshot = caffeine.postprocess.get(cameraId)
```

### Efeitos custom (Lua)

Definir `customEffectScript` no componente e implementar `onCreate` / `onUpdate` para animar ou combinar módulos.

Template: `assets/plugins/post_processing/scripts/custom_effect.lua`

Escrita de **efeitos GPU custom** (shaders HLSL/GLSL no stack) está planeado para quando o render graph GPU unificar passes de pós-processamento — ver [`plans/2026-09-21-rendering-roadmap.md`](../plans/2026-09-21-rendering-roadmap.md) fase P3+.

---

## Ficheiros

| Ficheiro | Função |
|----------|--------|
| `src/ecs/PostProcessComponents.hpp` | Structs por efeito |
| `include/caffeine/postprocess/PostProcessEditorUI.hpp` | UI ImGui partilhada |
| `include/caffeine/postprocess/PostProcessPresets.hpp` | Benchmark looks |
| `src/render/PostProcessRenderer.hpp` | Preview overlay (editor) |
| `src/script/PostProcessBindings.cpp` | Lua API |
| `apps/plugins/post_processing_plugin/` | Painel do plugin |
