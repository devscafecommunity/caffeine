# Caffeine Engine

C++20 game engine and **Doppio** scene editor (SDL3). User-facing documentation lives in the **`caffeine-website`** submodule, not in this tree.

## Ecosystem (git submodules)

| Path | What it is |
|------|------------|
| **`caf-pack/`** | Asset packer — `.caf` / `.cap` pipeline |
| **`Convoy/`** | 2D art station |
| **`WaveShaper/`** | Audio DAW for game audio |
| **`caffeine-website/`** | Docs site + MCP for agents |
| **`caffeine-launcher/`** | Desktop launcher (Rust) — start studio apps and manage plugins |

Initialize everything:

```bash
git submodule update --init --recursive
# or
./scripts/submodules.sh
```

Agent-oriented layout and policies: [`AGENTS.md`](AGENTS.md).

## Build (engine)

**Requirements:** C++20, CMake 3.20+, SDL3.

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

Helpers: `scripts/build.sh`, `scripts/build.fish`, `scripts/build.bat`.

Optional suite targets: `CAFFEINE_BUILD_CONVOY` (default ON), `CAFFEINE_BUILD_WAVESHAPER` (default OFF).

## Launcher

```bash
./scripts/build-launcher.sh
./caffeine-launcher/target/release/caffeine-launcher
```

## Docs site

```bash
git submodule update --init caffeine-website
cd caffeine-website && npm install && npm run dev
```
