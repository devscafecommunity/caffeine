# Caffeine Engine

C++20 game engine (SDL3) and **Doppio**, its Rust/Iced editor. The engine exposes a
C ABI (`libcaffeine-capi`) so any frontend can drive it. User-facing documentation
lives in the **`caffeine-website`** submodule, not in this tree.

## Ecosystem (git submodules)

| Path | What it is |
|------|------------|
| **`caf-pack/`** | Asset packer — `.caf` / `.cap` pipeline |
| **`caffeine-website/`** | Docs site + MCP for agents |
| **`caffeine-launcher/`** | Desktop launcher (Rust) — start studio apps and manage plugins |
| **`doppio-plugins/`** | Official/community Doppio C++ plugins (built with the engine, loaded on demand) |

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

## Doppio (Rust/Iced editor)

Doppio is the `doppio/` workspace: `crates/engine` is the `libloading` bridge to
`libcaffeine-capi`, and `crates/ui` holds the components, panels and windows.
Build the C ABI first, then run the editor:

```bash
cmake --build build --target caffeine-capi -j
cd doppio
cargo run -p doppio-ui -- ../assets/benchmarks/dense_outdoor.caf
```

Checks used by CI/agents: `./scripts/check-rust-ui.sh` (fmt, tests, clippy and the
core/ImGui coupling audit). The old C++ editor is frozen in `doppio-archived/`;
architecture notes live in [`doppio/docs/rust-ui.md`](doppio/docs/rust-ui.md).

## Editor plugins

The engine build compiles **every** plugin from the `doppio-plugins` submodule and
writes the libraries to `build/plugins/`. There is no separate plugin build step:

```bash
./scripts/submodules.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`CAFFEINE_BUILD_DOPPIO_PLUGINS=OFF` is the only way to skip them, and it is intended
for minimal/headless environments — not for normal builds.

Plugins target the legacy C++/ImGui plugin SDK (`include/caffeine/plugin/`,
`include/caffeine/ui/`), not the Rust UI. Install the libraries in a `plugins/`
directory and enable the ones you need in the Plugin Manager
(persisted in `~/.config/caffeine/plugins.cfg`).

## Docs

All user-facing documentation, plans, screenshots and PDFs live in the
**`caffeine-website`** submodule (`content/`, `tex/`, `captures/`). Nothing is
duplicated in the engine tree.

```bash
git submodule update --init caffeine-website
cd caffeine-website && npm install && npm run dev
```
