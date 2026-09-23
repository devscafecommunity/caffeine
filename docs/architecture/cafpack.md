# Cafpack — Camada Unificada de Assets

> **Namespace:** `CafPack`  
> **Repositório:** [`caf-pack/`](../../caf-pack/) (submodule)  
> **Status:** 🟡 Fase 1 — biblioteca activa; migração de tipos em curso

---

## Visão

O **Cafpack** é o "fio condutor" do ecossistema Caffeine Studio (estilo Adobe Creative Suite):

```
Convoy (arte)      ──┐
WaveShaper (áudio) ──┼──► Cafpack ──► .caf / .cap ──► Doppio / Runtime
Ferramentas futuras──┘         ▲
                               │
                 única fonte de verdade para leitura/escrita de dados
```

**Regra central:** todo novo tipo de asset (mesh, textura, áudio, animação, scene, prefab…) é definido **primeiro no Cafpack**. Nenhuma app inventa formato interno — só consome a API.

---

## Contrato por aplicação

| App | Responsabilidade | API Cafpack |
|-----|------------------|-------------|
| **Convoy** | Editar texturas/meshes | Export → `.caf` ou append a `.cap` |
| **WaveShaper** | Editar áudio | Export → `.caf Audio` |
| **Doppio** | IDE / preview / pack projeto | `Packer`, `Reader`, `HeaderGenerator` |
| **Runtime** | Carregar jogo | `Reader::loadCap` (zero parsing) |
| **caf-pack CLI** | CI / batch | `caf-pack --input raw --output game.cap` |

---

## Estrutura da biblioteca (`caf-pack-lib`)

```
caf-pack/
├── include/
│   ├── caffeine/CafTypes.hpp   ← fonte de verdade (.caf + .cap)
│   ├── cafpack/Types.hpp       ← alias público
│   └── caf-pack/
│       ├── Reader.hpp          ← ler .caf / .cap
│       ├── Registry.hpp        ← registo de processors
│       ├── Packer.hpp          ← agregar → .cap
│       ├── HeaderGenerator.hpp ← IDs hash para C++
│       └── *Processor.hpp      ← PNG, WAV, OBJ → .caf
└── src/
```

### Adicionar um novo tipo de asset

1. Adicionar valor em `CafAssetType` (`include/caffeine/CafTypes.hpp`)
2. Definir struct de metadata no mesmo ficheiro
3. Implementar `AssetProcessor` em `src/MyProcessor.cpp`
4. Registar em `makeDefaultRegistry()` (`Registry.cpp`)
5. Consumir em Doppio/Runtime via `Reader` — **sem bypass**

---

## Formatos

| Extensão | Descrição | Magic |
|----------|-----------|-------|
| `.caf` | Asset binário individual | `0x43414621` ("CAF!") |
| `.cap` | Pack de assets (mmap) | `0x4341502F` ("CAP/") |

Documentação detalhada do pack: [`caf-pack/README.md`](../../caf-pack/README.md).

### Dois headers CAF (migração)

| Header | Magic | Uso |
|--------|-------|-----|
| `caf-pack/include/caffeine/CafTypes.hpp` | `CAF!` | Assets importados + `.cap` |
| `src/core/io/CafTypes.hpp` | `0xCAFECAFE` | Scene/Prefab engine-internal (legado) |

**Fase 2** unifica num único schema; até lá, runtime assets usam sempre caf-pack.

---

## Pipeline de projeto

```
my_game/
├── raw_assets/          ← PNG, WAV, OBJ (fonte)
├── processed/           ← opcional: .caf individuais
├── game.cap             ← pack final (mmap no runtime)
└── game_assets.hpp      ← IDs hash gerados pelo HeaderGenerator
```

### Export manual (qualquer ferramenta)

```bash
./scripts/export-to-cap.sh raw_assets/ game.cap include/game_assets.hpp
```

Convoy e WaveShaper devem invocar o mesmo script ou linkar `caf-pack-lib` directamente.

---

## Integração CMake (monorepo)

```bash
git submodule update --init --recursive caf-pack Convoy WaveShaper
cmake -B build -DCAFFEINE_BUILD_CONVOY=ON -DCAFFEINE_BUILD_WAVESHAPER=ON
cmake --build build
```

| Target | Origem |
|--------|--------|
| `caf-pack-lib` | Submodule caf-pack |
| `caf-pack` | CLI |
| `doppio` | IDE (liga `caf-pack-lib`, `CF_HAS_CAF_PACK=1`) |
| `convoy` | Submodule Convoy |
| `waveshaper` | Submodule WaveShaper |

Desligar ferramentas: `-DCAFFEINE_BUILD_CONVOY=OFF` / `WAVESHAPER=OFF`.

---

## Fases de migração

| Fase | Entregável | Estado |
|------|------------|--------|
| **0** | Submodule caf-pack populado + `.gitmodules` | ✅ |
| **1** | `Reader`, `Registry`, CapLoader via Reader | ✅ |
| **2** | Unificar `CafTypes` engine ↔ caf-pack | 📅 |
| **3** | MeshLoader/AssetManager sem bypass PNG/OBJ | 📅 |
| **4** | Convoy/WaveShaper export nativo → Cafpack | 📅 |
| **5** | Deprecar `tools/caf-encode` | 📅 |

---

## Princípios

1. **Um tipo, um sítio** — schema + processor + reader no caf-pack
2. **Sem bypass** — loaders da engine leem `.caf`/`.cap`, não PNG cru em runtime
3. **Projeto unificado** — um `.cap` por jogo, partilhado por todas as apps
4. **Mesmo CMake root** — suite compila junta na raiz do monorepo

---

## Ver também

- [`ECOSYSTEM_WORKFLOW.md`](../ECOSYSTEM_WORKFLOW.md)
- [`assets/caf-format.md`](../assets/caf-format.md) — formato engine-internal (legado)
- [`plans/2026-05-17-unified-ecosystem-build.md`](../plans/2026-05-17-unified-ecosystem-build.md)
