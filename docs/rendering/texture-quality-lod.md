# Texture Quality LOD — Distância aos Visualizadores

> **Namespace:** `Caffeine::Render`  
> **Ficheiros:** `src/render/TextureQuality.hpp`, `src/render/GpuTextureCache.{hpp,cpp}`  
> **Status:** ✅ Implementado (editor + runtime GPU)

---

## Visão geral

O sistema de **texture quality LOD** reduz a resolução das texturas GPU conforme a distância entre o objeto e os **visualizadores** (câmaras). As texturas **não são removidas** — apenas entram em cache com dimensões menores.

Objetivos:

- Menor uso de memória GPU em cenas grandes
- Menos banda de textura em objetos distantes
- Qualidade total perto das câmaras (editor, `Camera3D`, Camera Preview)

---

## Visualizadores

A distância usada para cada draw é o **mínimo** entre a posição do objeto e cada visualizador activo:

| Contexto | Visualizadores |
|----------|----------------|
| Scene Viewport | Câmara do editor (orbit) + todas as entidades com `Camera3DComponent` |
| Camera Preview | Só a posição da câmara do painel |
| Runtime | Posição da câmara activa (quando `textureQualityViewers` é preenchido) |

```cpp
f32 dist = distanceToNearestViewer(worldPos, options.textureQualityViewers);
u32 tier = textureQualityTier(dist, options.textureQuality);
```

---

## Curva de qualidade

Configuração em `TextureQualitySettings`:

| Campo | Default | Descrição |
|-------|---------|-----------|
| `enabled` | `true` | Liga/desliga o LOD |
| `fullRadius` | `35.0` m | Dentro deste raio → tier 0 (100%) |
| `falloffDistance` | `100.0` m | Decaimento linear além do raio |
| `minScale` | `0.25` | Escala mínima no limite do falloff (1/4 res) |

### Escala contínua

```
distance <= fullRadius          → scale = 1.0
distance >= fullRadius + falloff → scale = minScale
entre os dois                   → interpolação linear
```

### Tiers discretos (cache GPU)

| Tier | Escala aprox. | Exemplo (1024² base) |
|------|---------------|----------------------|
| 0 | 100% | 1024 × 1024 |
| 1 | 50% | 512 × 512 |
| 2 | 25% | 256 × 256 |
| 3 | 12.5% | 128 × 128 (mín. 16 px) |

Limites em `textureQualityTier()`: `>= 0.75` → tier 0, `>= 0.375` → tier 1, `>= 0.1875` → tier 2, senão tier 3.

---

## GpuTextureCache

Cada tier é uma entrada separada no cache:

```
textures/albedo.png      → tier 0
textures/albedo.png#q1   → tier 1
textures/albedo.png#q2   → tier 2
textures/albedo.png#q3   → tier 3
```

### API

```cpp
GpuTexture* acquire(const std::string& path, u32 qualityTier = 0);
GpuTexture* acquireFromPixels(const std::string& key, const u8* pixels,
                              u32 w, u32 h, u32 qualityTier = 0);
```

No upload (`loadTexture`):

1. `stbi_load` da imagem original (tier 0) ou reutilização em memória
2. `downscaleRgba()` com `halveRgbaImage()` por cada nível de tier
3. Upload GPU com dimensões reduzidas + geração de mipmaps

`release()` e `invalidate()` limpam **todos** os tiers (`0 .. kTextureQualityMaxTier`).

---

## Integração no render

### GpuSceneRenderOptions

```cpp
struct GpuSceneRenderOptions {
    bool wireframeMeshes = false;
    bool enableShadows   = true;
    TextureQualitySettings textureQuality{};
    std::vector<Vec3>      textureQualityViewers;
};
```

### GpuSceneRenderer

Em `gatherMeshDraws()`, cada `MeshDraw` recebe `viewerDistance` (centro do AABB ou chunk de terreno).

Em `resolveMeshAlbedo()` e binding de splat do terreno:

```cpp
const u32 tier = textureQualityTier(draw.viewerDistance, options.textureQuality);
auto* tex = m_textureCache.acquire(path, tier);
```

---

## Configuração no editor

**Settings → Viewport → Texture quality**

| Preferência JSON | Campo `EditorContext` |
|------------------|----------------------|
| `texture_quality_enabled` | `textureQualityEnabled` |
| `texture_quality_radius` | `textureQualityRadius` |
| `texture_quality_falloff` | `textureQualityFalloff` |
| `texture_quality_min_scale` | `textureQualityMinScale` |

Persistido em `~/.config/caffeine/editor_preferences.json`.

Ficheiros: `EditorPreferences.{hpp,cpp}`, `SettingsPanel.cpp`, `EditorContext.hpp`.

---

## Diagrama de fluxo

```
Viewer positions
       │
       ▼
distanceToNearestViewer(meshCenter)
       │
       ▼
textureQualityTier(distance, settings)
       │
       ▼
GpuTextureCache::acquire(path, tier)
       │
       ├── cache hit  → bind texture existente
       └── cache miss → load + downscale + upload GPU
```

---

## Limitações

| Item | Notas |
|------|-------|
| Sem transição suave | Troca de tier é discreta; sem blend entre resoluções |
| Downscale no load | Não usa filtros GPU separados; box-filter em CPU (`halveRgbaImage`) |
| Terreno | Splat layers usam o mesmo tier por chunk |
| Normal maps | Seguem o mesmo tier que o albedo |

---

## Ver também

- [Scene Viewport](../editor/scene-viewport.md) — visualizadores e `GpuSceneRenderOptions`
- [Sessão 2026-09-22](../plans/2026-09-22-viewport-rendering-quality-session.md) — changelog completo
- [Asset Manager](../assets/asset-manager.md) — carregamento de imagens
- [Rendering Roadmap](../plans/2026-09-21-rendering-roadmap.md) — P1/P2 LOD
