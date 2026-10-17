# Scripting Module — Design Document

> **Issue:** #44 — Scripting  
> **Fase:** 6 — O Olimpo  
> **Namespace:** `Caffeine::Script`  
> **Status:** 📅 Design Approved  

## Overview

Expose Caffeine Engine to an embedded scripting language (Lua 5.4 + sol2) so designers
and scribes can program game behaviors without C++. The system is opt-in via CMake flag
`CAFFEINE_ENABLE_SCRIPTING=ON` — C++-only games have zero scripting overhead.

The design prioritizes **simplicity** and **future extensibility**: all Lua-specific types
are hidden behind a clean `ScriptEngine` abstraction in the `.cpp` file. No `sol::` type
ever appears in a public header.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Scripting language | Lua 5.4 | Industry familiarity, stable, sol2 integration |
| Binding library | sol2 (header-only) | Automatic C++→Lua binding, minimal glue code |
| Abstraction layer | Opaque Pimpl (no virtual interface) | Extensible without OOP overhead — swap `.cpp` for new language |
| Script storage | Raw `std::string` path in ScriptComponent | Plain text files, not binary `.caf` assets |
| Build model | Opt-in (`CAFFEINE_ENABLE_SCRIPTING=OFF` by default) | Zero overhead for C++-only games |
| Sandboxing | Selective sol2 library opening | `os`, `io`, `debug`, `load` never exposed |

## File Structure

```
src/script/
├── ScriptEngine.hpp      # Public API — NO sol2/Lua types visible
├── ScriptEngine.cpp      # Lua 5.4 + sol2 implementation + binding registration
├── ScriptTypes.hpp       # ScriptComponent (ECS component)
├── ScriptSystem.hpp      # ScriptSystem (ISystem implementation)
├── ScriptSystem.cpp      # onUpdate dispatch, lifecycle management
├── ScriptWatcher.hpp     # Hot-reload file watcher (editor mode)
└── ScriptWatcher.cpp     # Platform file monitoring

tests/
└── test_script.cpp       # ScriptEngine init/load/call + ScriptSystem tests
```

Key rule: **No `sol/` include or `sol::` type ever appears in a `.hpp` file.**

## ScriptEngine API

```cpp
class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    // init receives engine subsystems for Lua binding registration.
    // All pointers are captured by the binding lambdas.
    bool init(ECS::World* world, Input::InputManager* input,
              Events::EventBus* events);
    void shutdown();

    // Loading — returns false on error, fills outError if provided
    bool loadScript(const std::string& path, std::string* outError = nullptr);
    bool reloadScript(const std::string& path, std::string* outError = nullptr);
    bool isLoaded(const std::string& path) const;

    // Lifecycle dispatch — each loads the script if not yet loaded,
    // looks up the function by name, calls it with the given args.
    // Returns false if function missing or runtime error.
    bool callOnCreate(const std::string& path, ECS::Entity entity);
    bool callOnUpdate(const std::string& path, ECS::Entity entity, f32 dt);
    bool callOnDestroy(const std::string& path, ECS::Entity entity);
    bool callOnCollision(const std::string& path, ECS::Entity entity,
                         ECS::Entity other);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void registerBindings();  // registers caffeine.* globals in Lua state
};
```

The `Impl` struct in ScriptEngine.cpp holds:
- `sol::state m_lua` — the Lua VM state
- `ECS::World* m_world`, `Input::InputManager* m_input`, `Events::EventBus* m_events` — subsystem pointers
- `HashMap<std::string, sol::protected_function> m_scripts` — path → compiled Lua chunk
- String-key event registry for Lua event bridge

## ScriptComponent & ScriptSystem

```cpp
// ScriptTypes.hpp
struct ScriptComponent {
    std::string scriptPath;  // e.g. "scripts/hero.lua"
};

// ScriptSystem.hpp
class ScriptSystem : public ECS::ISystem {
public:
    explicit ScriptSystem(ScriptEngine* engine);
    void onUpdate(ECS::World& world, f32 dt) override;

private:
    ScriptEngine* m_engine;
    Vector<ECS::Entity> m_initialized;  // entities that had onCreate called
};
```

Lifecycle in `onUpdate`:
1. `forEach<ScriptComponent>` — iterate all entities with scripts
2. First encounter → `engine->callOnCreate(path, entity)`, track in `m_initialized`
3. Every frame → `engine->callOnUpdate(path, entity, dt)`
4. Script load failures → logged via LogSystem, entity skipped

## Lua Bindings API (`caffeine.*`)

All bindings registered inside ScriptEngine.cpp under a global `caffeine` Lua table.

### `caffeine.world` — Entity & Component CRUD

| Lua API | Implementation |
|---------|---------------|
| `caffeine.world.create()` | `world->create()` → returns entity ID |
| `caffeine.world.destroy(entity)` | `world->destroy(Entity(entity))` |
| `caffeine.world.hasComponent(entity, "Type")` | String→ComponentID map lookup |
| `caffeine.world.getTransform(entity)` | Reads Position2D+Rotation+Scale2D → table |
| `caffeine.world.setTransform(entity, tbl)` | Writes table fields to components |
| `caffeine.world.getRigidBody2D(entity)` | Returns RigidBody2D fields |
| `caffeine.world.setRigidBody2D(entity, tbl)` | Writes to RigidBody2D |
| `caffeine.world.getSprite(entity)` | Returns Sprite fields |
| `caffeine.world.setSprite(entity, tbl)` | Writes to Sprite |

### `caffeine.input` — Keyboard, Mouse, Axis

| Lua API | Implementation |
|---------|---------------|
| `caffeine.input.isKeyDown("Space")` | String→Key map → raw key state query |
| `caffeine.input.getAxis("Horizontal")` | String→Axis map → `axisState().value` |
| `caffeine.input.mousePosition()` | `mousePosition()` → `{x, y}` |
| `caffeine.input.isMouseButtonDown(1)` | String→MouseButton → raw state |

### `caffeine.events` — String-based Event Bridge

| Lua API | Implementation |
|---------|---------------|
| `caffeine.events.on("name", fn)` | Registers callback in string-keyed event map |
| `caffeine.events.emit("name", arg1, ...)` | Calls all callbacks registered for "name" |
| `caffeine.events.off(handle)` | Removes callback by handle ID |

### `caffeine.debug` — Logging

| Lua API | Implementation |
|---------|---------------|
| `caffeine.debug.log(msg)` | `LogSystem::instance().log(Info, "Lua", msg)` |
| `caffeine.debug.warn(msg)` | `LogSystem::instance().log(Warn, "Lua", msg)` |
| `caffeine.debug.error(msg)` | `LogSystem::instance().log(Error, "Lua", msg)` |

### `caffeine.math` — Vector Math

| Lua API | Implementation |
|---------|---------------|
| `caffeine.math.vec2(x, y)` | Returns `{x, y}` table |
| `caffeine.math.add(v1, v2)` | Component-wise addition |
| `caffeine.math.length(v)` | `sqrt(x² + y²)` |
| `caffeine.math.normalize(v)` | Divide by length |
| `caffeine.math.dot(v1, v2)` | `x1*x2 + y1*y2` |
| `caffeine.math.sub(v1, v2)` | Component-wise subtraction |

## Hot-Reload (Editor Mode)

```cpp
class ScriptWatcher {
public:
    void init(ScriptEngine* engine, std::string_view scriptsDir);
    void shutdown();
    void poll();  // call each frame

private:
    ScriptEngine* m_engine = nullptr;
    std::string m_dir;
    HashMap<std::string, std::filesystem::file_time_type> m_mtimes;
};
```

Uses `std::filesystem::last_write_time()` to detect file modifications.
On change → `engine->reloadScript(path)` which re-executes the Lua chunk
(global functions like `onCreate`, `onUpdate` are replaced in the Lua environment).

## Sandboxing

`sol::state` opens only these libraries:
- `sol::lib::base` (limited — `print` → routes to caffeine.debug.log)
- `sol::lib::math`
- `sol::lib::string`
- `sol::lib::table`
- `sol::lib::pcall` (error handling)

Explicitly NOT opened: `os`, `io`, `debug`, `load`, `loadstring`, `coroutine`, `utf8`.

In editor mode (`CF_EDITOR_MODE`), `os` and `io` restrictions may be relaxed.

## Build System Integration

```cmake
option(CAFFEINE_ENABLE_SCRIPTING "Enable Lua scripting support" OFF)

if(CAFFEINE_ENABLE_SCRIPTING)
    include(FetchContent)
    FetchContent_Declare(sol2
        GIT_REPOSITORY https://github.com/ThePhD/sol2.git
        GIT_TAG v3.3.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sol2)

    find_package(Lua 5.4 QUIET)
    if(NOT LUA_FOUND)
        FetchContent_Declare(lua
            GIT_REPOSITORY https://github.com/lua/lua.git
            GIT_TAG v5.4.7
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(lua)
    endif()

    target_compile_definitions(Caffeine PUBLIC CF_HAS_SCRIPTING=1)
    target_include_directories(Caffeine PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(Caffeine PRIVATE ${LUA_LIBRARIES})

    target_sources(Caffeine PRIVATE
        src/script/ScriptEngine.cpp
        src/script/ScriptSystem.cpp
        src/script/ScriptWatcher.cpp
    )
endif()
```

InputManager needs one new public method: `bool isKeyDown(Key key) const;` — exposes
raw key state (currently private) for the Lua binding.

## Testing Strategy

File: `tests/test_script.cpp`

| # | Test | Coverage |
|---|------|----------|
| 1 | ScriptEngine init/shutdown | VM creation, binding registration, clean teardown |
| 2 | loadScript simple string | Load Lua code string, no errors |
| 3 | loadScript file not found | Returns false, error message populated |
| 4 | callOnCreate | Script's onCreate(entity) receives correct entity ID |
| 5 | callOnUpdate | Script's onUpdate(entity, dt) receives dt correctly |
| 6 | callOnDestroy | Script cleanup function called |
| 7 | ScriptComponent lifecycle | ScriptSystem processes entities with ScriptComponent |
| 8 | Input binding - isKeyDown | Lua reads injected key state |
| 9 | Input binding - getAxis | Lua reads axis values |
| 10 | Debug binding - log | Lua log routes to LogSystem |
| 11 | Math binding - vec2 ops | add, sub, length, normalize, dot return correct values |
| 12 | Sandbox - os blocked | os.execute() errors in Lua |
| 13 | Error handling | Lua runtime errors caught, no crash |
| 14 | Hot-reload | reloadScript picks up modified function body |
| 15 | Script reuse | Same script path on two entities — loaded once |

## Acceptance Criteria

- [x] Design approved by issue author
- [ ] ScriptEngine initializes Lua VM and registers caffeine.* bindings
- [ ] ScriptEngine::callOnUpdate executes per-frame game logic from .lua
- [ ] ScriptSystem processes all SkeletalAnimator— *(entidades com ScriptComponent) cada frame*
- [ ] Transform bindings (get/set position, rotation, scale) work correctly
- [ ] Input bindings (isKeyDown, getAxis, mousePosition) work correctly
- [ ] Lua runtime errors are caught and logged without engine crash
- [ ] Hot-reload: modifying a .lua on disk reloads without restart
- [ ] Sandbox: os.execute() returns error in game scripts
- [ ] All tests pass (15+ test cases)
- [ ] Build with `CAFFEINE_ENABLE_SCRIPTING=ON` produces working binary
- [ ] Build with `CAFFEINE_ENABLE_SCRIPTING=OFF` has zero scripting overhead
