# Ferramenta `caffeine-render-snapshot`

> **Target CMake:** `caffeine-render-snapshot` (requer SDL3 e `CAFFEINE_BUILD_HEADLESS=OFF`)  
> **Fonte:** `tools/render-snapshot/main.cpp`

Renderiza uma cena de referência pela **mesma pipeline** que o jogo/editor (HDR, probes, SSR, sombras, `PostProcessStack`) e grava um PNG — útil para regressões visuais sem abrir o Doppio.

---

## Build

```bash
cmake -S . -B build -DCAFFEINE_BUILD_HEADLESS=OFF
cmake --build build --target caffeine-render-snapshot -j
```

---

## Uso

```text
caffeine-render-snapshot [out.png] [width] [height] [frames] [skyboxIndex] [view 0|1|2]
```

| Argumento | Default | Descrição |
|-----------|---------|-----------|
| `out.png` | `render-snapshot.png` | Ficheiro de saída |
| `width` / `height` | 1280 × 720 | Resolução interna |
| `frames` | 16 | Frames para TAA/probes assentarem |
| `skyboxIndex` | 0 | Preset de céu (`EnvironmentSystem`) |
| `view` | 0 | 0 = frontal, 1 = lateral, 2 = afastada |

### Variáveis de ambiente

| Variável | Efeito |
|----------|--------|
| `SNAP_NO_IBL=1` | Desliga IBL |
| `SNAP_NO_SHADOWS=1` | Desliga sombras direcionais |

---

## Cena de referência

- Chão 24×24 (roughness ~0.35)
- Esferas: Mirror (centro), Gold, Clear Glass
- Cubos coloridos + Glossy Plastic
- Sol direcional (intensidade ~2.5)

Materiais vêm de `Assets::materialPresets()`.

---

## Ver também

- [`reflections.md`](reflections.md)
- [`post-processing.md`](post-processing.md)
- [`forward-render-features.md`](forward-render-features.md)
