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

Doppio does **not** build or preload feature plugins. Build them from the
`doppio-plugins` submodule against the public plugin SDK, install the libraries in a
`plugins/` directory, and enable only the ones you need in **Plugins → Plugin
Manager** (persisted in `~/.config/caffeine/plugins.cfg`).

```bash
cmake -S doppio-plugins -B doppio-plugins/build \
  -DCAFFEINE_ROOT="$PWD" \
  -DIMGUI_DIR="$PWD/build/_deps/imgui-src"
cmake --build doppio-plugins/build -j
cp doppio-plugins/build/plugins/*.so build/plugins/
```

## Launcher

```bash
./scripts/build-launcher.sh
./caffeine-launcher/target/release/caffeine-launcher
```

## Docs

All user-facing documentation, plans, screenshots and PDFs live in the
**`caffeine-website`** submodule (`content/`, `tex/`, `captures/`). Nothing is
duplicated in the engine tree.

```bash
git submodule update --init caffeine-website
cd caffeine-website && npm install && npm run dev
```
