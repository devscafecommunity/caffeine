# 🏔️ Terrain System

> **Namespace:** `Caffeine::Terrain`, `Caffeine::ECS::TerrainComponent`  
> **Arquivos:** `src/terrain/*`, `src/ecs/TerrainComponents.hpp`  
> **Status:** ✅ Implementado (runtime + editor sculpt/paint)  
> **Geração procedural:** plugin — ver [`terrain-plugin.md`](terrain-plugin.md)

---

## Visão Geral

O terreno na Caffeine é um **heightfield editável**: dados em `TerrainHeightmap`, renderização via `TerrainMeshBuilder` + LOD por chunks, pintura de splat em `TerrainSplatmap`. A engine **não** embute mais geração procedural no core — isso ficou no plugin **Terrain Generator**, que usa o algoritmo em `assets/general-ultra-realistic-terrain-algorithm`.

**Separação de responsabilidades:**

| Camada | Responsabilidade |
|--------|------------------|
| **Core** | Heightmap, mesh, LOD, colisão coarse, sculpt/paint, `.cterrain` |
| **Terrain Editor** | Ferramentas de escultura, splat e export de dados |
| **Terrain Generator (plugin)** | Presets, Node.js, import de heightmap |

---

## TerrainComponent

```cpp
struct TerrainComponent {
    u32 resolutionX = 257;
    u32 resolutionZ = 257;
    f32 worldSizeX, worldSizeZ;   // metros (1 u = 1 m)
    f32 maxHeight;                // altura 0–1 do heightmap → metros

    bool useChunks = true;
    u32 chunkVertexCount = 33;
    u32 maxLodLevels = 2;
    f32 lodDistanceScale = 520.0f;

    u32 collisionSampleStep = 4;  // passo do mesh de colisão
    bool buildCollisionMesh = true;

    char terrainDataPath[256];    // caminho .cterrain no projeto
};
```

O heightmap interno guarda valores **normalizados** `[0, 1]`. Na mesh, `y = sample * maxHeight`.

---

## Pipeline de Dados

```
TerrainComponent (ECS)
       │
       ▼
TerrainCache::initializeEntity / syncEntity
       │
       ├── TerrainHeightmap  (CPU, editável)
       ├── TerrainSplatmap   (4 camadas)
       ├── Mesh3D render     (TerrainMeshBuilder + tangentes)
       ├── Mesh3D collision  (TerrainCollisionMeshBuilder, decimado)
       └── TerrainChunk[]    (LOD + frustum cull)
```

### TerrainMeshBuilder

- Gera vértices com normais do heightmap (`sampleNormalBilinear`).
- UVs em tiling conforme `textureTileSize` / `splatTileSize`.
- **Tangentes** calculadas por triângulo (base para normal mapping).

### Colisão

`TerrainCollisionMeshBuilder` reutiliza `buildRegion` com `collisionSampleStep` (ex.: 4 → ~16× menos triângulos). Acesso via `TerrainCache::collisionMeshFor(entity)`.

---

## Editor

| Painel | Função |
|--------|--------|
| **Inspector → Terrain** | Resolução, tamanho, LOD, splat, colisão |
| **Terrain Editor** | Sculpt (raise/lower/smooth/flatten/noise), splat paint, export `.cterrain` |

Geração procedural: painel **Terrain Generator** (plugin).

---

## Serialização

`SceneSerializer` grava `TerrainComponent` no blob de cena (versão **9**). Campos de geração legados (v2–v8) são lidos e descartados para compatibilidade. Dados de height/splat pesados vivem em `.cterrain` referenciado por `terrainDataPath`.

---

## API Útil

```cpp
TerrainCache& cache = TerrainCache::instance();
cache.initializeEntity(world, entity);
cache.heightmapFor(entity)->setNormalized(x, z, h);
cache.syncEntity(world, entity);

Terrain::importHeightmapFile(world, entity, path, worldSizeX, worldSizeZ);
```
