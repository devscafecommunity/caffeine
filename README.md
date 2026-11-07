# Caffeine Engine

C++20 game engine and **Doppio** scene editor (SDL3). User-facing documentation lives in the **`caffeine-website`** submodule, not in this tree.

## Ecosystem (git submodules)

| Path | What it is |
|------|------------|
| **`caf-pack/`** | Asset packer — `.caf` / `.cap` pipeline |
| **`caffeine-website/`** | Docs site + MCP for agents |
| **`caffeine-launcher/`** | Desktop launcher (Rust) — start studio apps and manage plugins |
| **`doppio-plugins/`** | Official/community Doppio editor plugins (built separately) |

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

## Editor plugins

The engine build compiles **every** plugin from the `doppio-plugins` submodule and
writes the libraries to `build/plugins/`. There is no separate plugin build step:

```bash
./scripts/submodules.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`CAFFEINE_BUILD_DOPPIO_PLUGINS=OFF` is the only way to skip them, and it is intended
for minimal/headless environments — not for normal editor builds.

Start Doppio and enable the plugins you need in **Plugins → Plugin Manager**
(persisted in `~/.config/caffeine/plugins.cfg`).

## Docs

All user-facing documentation, plans, screenshots and PDFs live in the
**`caffeine-website`** submodule (`content/`, `tex/`, `captures/`). Nothing is
duplicated in the engine tree.

```bash
git submodule update --init caffeine-website
cd caffeine-website && npm install && npm run dev
```
