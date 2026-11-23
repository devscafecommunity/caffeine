#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "input/InputManager.hpp"

#include <memory>
#include <string>

namespace Caffeine::Runtime {

/// The game's simulation without a window: every system `caffeine-runtime` runs between frames (animation,
/// effects, fluids, navigation, 2D and 3D physics, scripts, procedural worlds, UI) in the same order, over a world
/// the caller owns. The editor's Play mode runs it so the scene behaves as it does in the compiled game; the
/// caller renders and restores the scene afterwards.
class GameSimulation {
public:
    GameSimulation();
    ~GameSimulation();
    GameSimulation(const GameSimulation&) = delete;
    GameSimulation& operator=(const GameSimulation&) = delete;

    /// Begins a run on `world`. Scripts resolve against `projectRoot`; `scenePath` is the scene a script's
    /// "reload this scene" refers to. Returns false when nothing can run (the scene still plays without scripts).
    bool start(ECS::World& world, const std::string& projectRoot, const std::string& scenePath);
    void stop();
    bool running() const;

    /// One frame of the game, `dt` seconds long (clamped to 0.1 s).
    void update(ECS::World& world, f32 dt);

    /// The keyboard and mouse the scripts read, fed by whoever shows the game.
    void keyDown(u32 scancode);
    void keyUp(u32 scancode);
    void releaseAllKeys();
    void mouseMove(f32 x, f32 y);
    void mouseButton(u32 button, bool down);

    /// Frames run and seconds simulated since `start`.
    u64 frames() const;
    f32 time() const;
    /// True when the script engine started (Lua scripts run); false in a build without scripting.
    bool scriptsRunning() const;
    /// A script asked for another scene and it was loaded: its path (empty when none), cleared when read.
    std::string takeSceneChange();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace Caffeine::Runtime
