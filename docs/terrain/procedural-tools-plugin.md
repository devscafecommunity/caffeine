# 🎲 Procedural Tools Plugin

> **Plugin:** `procedural_tools_plugin.so`  
> **Scripts:** `assets/plugins/procedural/scripts/`  
> **Runtime API:** `caffeine.procedural.*` (Lua)

---

## Visão Geral

Ferramentas **modulares** para streaming procedural — não é um sistema de “tipos de jogo”. Combina:

| Ferramenta | Opções | Função |
|------------|--------|--------|
| **Terrain height** | `flat`, `hills`, `fbm` | Perfil de altura por chunk |
| **Structure spawner** | `none`, `markers`, `rooms`, `track` | Cubos opcionais por chunk |
| **Lua director** | qualquer `.lua` | Lógica de streaming em play mode |
| **Noise API** | `hash`, `noise2d`, `fbm2d` | Dados para scripts custom |

Exploração, backrooms e corrida infinita são **benchmarks** (exemplos de referência), não arquetipos obrigatórios.

---

## Setup (Editor)

1. Abrir painel **Procedural Tools**
2. Configurar **Streaming** (seed, chunk size, view radius)
3. Escolher **Terrain height** e **Structure spawner** (ou `none` + Lua)
4. Definir **Script path** (default: `custom_director.lua`)
5. **Install Lua scripts** → copia templates para `scripts/procedural/`
6. **Create Streaming Setup** → cria `ProceduralTerrain` + `ProceduralDirector`
7. **Play** — chunks carregam à volta da câmara

### Benchmarks

Na secção **Benchmarks (examples)** — carrega uma cena de referência para estudar ou duplicar. Não limita o que podes construir.

| Benchmark | Terrain | Structures | Script |
|-----------|---------|------------|--------|
| Exploration | hills | markers | `exploration_director.lua` |
| Backrooms | flat | rooms | `backrooms_director.lua` |
| Endless race | flat | track | `endless_race_director.lua` |

---

## Lua API (`caffeine.procedural`)

```lua
-- Noise / coords
caffeine.procedural.hash(x, z, seed, salt)
caffeine.procedural.noise2d(x, z, seed)
caffeine.procedural.fbm2d(x, z, seed, octaves, lacunarity, persistence)
caffeine.procedural.worldToChunk(worldCoord, chunkSize)

-- Streaming
local focus = caffeine.procedural.getFocusChunk(directorId, chunkSize)
caffeine.procedural.stream(directorId, terrainId, focus.cx, focus.cz)
caffeine.procedural.isChunkLoaded(directorId, cx, cz)
caffeine.procedural.unloadChunk(directorId, cx, cz)

-- Terrain bake
caffeine.procedural.fillTerrainNoise(terrainId, seed, amplitude)
caffeine.procedural.generateTerrainChunk(terrainId, cx, cz, seed, "hills")

-- Structures
caffeine.procedural.spawnCube(directorId, cx, cz, x, y, z, sx, sy, sz)
```

---

## Director script (template)

```lua
local state = { chunkSize = 64 }

function onCreate(directorId)
  state.id = directorId
  state.terrain = caffeine.procedural.findTerrain()
end

function onUpdate(directorId, dt)
  local f = caffeine.procedural.getFocusChunk(state.id, state.chunkSize)
  caffeine.procedural.stream(state.id, state.terrain, f.cx, f.cz)
end
```

---

## Componentes ECS

| Campo | Função |
|-------|--------|
| `terrainProfile` | `flat`, `hills`, `fbm` |
| `structureProfile` | `none`, `markers`, `rooms`, `track` |
| `customScriptPath` | Lua director |
| `seed`, `chunkSize`, `viewRadius` | Streaming |
| `ProceduralSpawnTag` | Entidades removidas no unload |

---

## Serviços (Plugin SDK)

| Serviço | Uso |
|---------|-----|
| `procedural.installScripts` | Copia templates Lua |
| `procedural.createStreamingSetup` | Cria cena com ferramentas escolhidas |
| `procedural.loadBenchmark` | Carrega benchmark de referência |

---

## Core (engine)

| Módulo | Ficheiro |
|--------|----------|
| Profiles | `src/procedural/ProceduralProfiles.*` |
| Noise | `src/procedural/ProceduralNoise.*` |
| Geradores | `src/procedural/ProceduralGenerator.*` |
| Streaming | `src/procedural/ProceduralStreamer.*` |
| Tick play mode | `src/procedural/ProceduralWorldSystem.*` |
