# Runtime native modules

Caffeine can load **game simulation** libraries at play time. This is separate from **Doppio editor plugins** (`PluginAPI.hpp`).

## Layout

```
your-project/
  native/
    my_sim.so      # Linux
    my_sim.dll     # Windows
```

Set the project search root via `ScriptEngine::setSearchRoot` (build output / project root).

## ABI (version 1)

Header: `include/caffeine/runtime/NativeModuleAPI.h`

Required export:

```c
CAFFEINE_NATIVE_EXPORT const CaffeineNativeModuleInfo* caffeine_native_module_info(void);
```

Optional exports:

```c
CAFFEINE_NATIVE_EXPORT void caffeine_native_on_load(void* host_opaque);
CAFFEINE_NATIVE_EXPORT void caffeine_native_on_unload(void* host_opaque);
CAFFEINE_NATIVE_EXPORT int caffeine_native_call(const char* function, const char* args_json,
                                                char* out_buf, uint32_t out_capacity);
```

- `caffeine_native_call` returns `0` on success; non-zero on error (message in `out_buf` when possible).
- Arguments and results are UTF-8 JSON strings for portability.

## Lua

```lua
caffeine.native.load("my_sim")
local result = caffeine.native.call("my_sim", "step", '{"dt":0.016}')
```

## Threading

Unless documented otherwise, native code runs on the **main thread** with the Lua VM and ECS. Do not call Lua from worker threads without synchronization.

## CI

Ship modules for each target platform; the engine does not cross-compile native code.
