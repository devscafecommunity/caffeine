# Plugin SDK

Doppio is a **lean IDE**. Feature plugins ship separately and load at runtime from `plugins/`.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Doppio (core IDE)                                       │
│  Scene editor, viewport, hierarchy, inspector, assets     │
│  PluginManager + PluginServiceRegistry (empty at start) │
│  NO terrain/procedural/post-process/git UI in core      │
└───────────────────────────┬─────────────────────────────┘
                            │ PluginHostApi (dlopen)
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
  terrain_plugin    procedural_tools_plugin   git_plugin
  (owns TerrainBridge) (owns ProceduralBridge) (owns GitRunner)
```

### Rules

1. **Plugins own their logic** — bridges, panels, services live in `.so` files, not in `doppio` sources.
2. **Core owns ECS + runtime** — `PostProcessComponent`, `TerrainComponent`, procedural systems are engine data/runtime; editor UI for them is plugin territory.
3. **Service registry is generic** — plugins call `registerService` in `OnLoad`; Doppio never hardcodes feature services.
4. **Optional bundling** — CMake `CAFFEINE_BUNDLE_DEFAULT_PLUGINS=OFF` (default): Doppio ships without copying plugins. Turn `ON` for dev convenience.

## Install plugins

```bash
# Build plugins (from build dir)
cmake --build . --target terrain_plugin post_processing_plugin procedural_tools_plugin git_plugin

# Copy into Doppio plugins folder
cp build/plugins/*.so build/doppio/plugins/
```

Or enable bundling at configure time:

```bash
cmake -DCAFFEINE_BUNDLE_DEFAULT_PLUGINS=ON ..
```

## Plugin catalog (reference)

| Plugin | Purpose |
|--------|---------|
| `hello_plugin` | Minimal SDK example |
| `terrain_plugin` | Ultra-realistic terrain generator (Node.js) |
| `procedural_tools_plugin` | Modular procedural streaming tools |
| `post_processing_plugin` | Post-process effect stack UI |
| `git_plugin` | Optional Git project management |

## Host API

See `include/caffeine/plugin/PluginAPI.hpp`:

- `registerPanel`, `registerMenuAction`, `registerComponentDrawer`
- `registerService` / `invokeService`
- `getProjectRootPath`, `getActiveWorld`, `markSceneDirty`

## Services (cross-plugin)

Plugins may expose services for aggregators / stores:

| Service | Owner plugin |
|---------|----------------|
| `terrain.generateUltra` | terrain_plugin |
| `terrain.importHeightmap` | terrain_plugin |
| `procedural.*` | procedural_tools_plugin |

## Inspector drawers

Plugins register component drawers via `registerComponentDrawer`. Doppio Inspector delegates to registered drawers (e.g. Post Process shows plugin UI when plugin is loaded).

## Writing a plugin

```cpp
#include "caffeine/plugin/PluginAPI.hpp"

class MyPlugin : public Caffeine::Editor::IPlugin {
    void OnLoad() override {
        m_host->registerPanel(m_host->editorContext, GetName(), "My Panel", &render, this);
    }
    // ...
};

extern "C" CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
    const Caffeine::Editor::PluginHostApi* host) {
    return new MyPlugin(host);
}
```

Build as `SHARED` library with `CAFFEINE_PLUGIN_EXPORTS=1`, output to `plugins/`.
