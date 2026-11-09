# Doppio

Doppio is the graphical IDE for **Caffeine**. It is an *application* on top of the
engine, not part of it. A different front-end must be able to consume
`caffeine-core` without this directory.

```
Vulkan / D3D12 / Metal
        ↓
      SDL3 GPU
        ↓
  caffeine-core  (src/, include/caffeine/)
        ↓
     doppio      (apps/doppio, src/doppio/)
        ↓
    plugins      (doppio-plugins submodule)
```

## Build

```bash
./scripts/submodules.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target doppio -j
./build/doppio
```

`doppio` links `caffeine-core` and `doppio-ui-kit` (alias `Doppio::UIKit`).
`caffeine-runtime` is the standalone game runtime and does not link the IDE.

## Ownership

| Path | Owner | Notes |
|---|---|---|
| `src/doppio/editor/` | Doppio | Panels, workspace, viewport, build UI |
| `src/doppio/ui_kit/` | Doppio | ImGui widgets, theme, icons, node canvas |
| `src/` (other) | Caffeine core | No ImGui, no panels, no IDE state |
| `src/caffeine/project`, `src/caffeine/scene` | Core services | Project metadata, view context, scene I/O |
| `doppio-plugins/` | Plugins | Optional tools that only call core APIs |

See [`docs/ui-kit.md`](docs/ui-kit.md) for the composable element model
(Objects → Cards → Panels), the element catalogue, plugin usage, debugging and
the migration map from the older free-function widgets.

General user documentation lives in the `caffeine-website` submodule; these
pages are Doppio-specific.
