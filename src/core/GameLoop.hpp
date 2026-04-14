#pragma once

#include "Types.hpp"
#include <functional>

namespace Caffeine {

enum class GameState : u8 {
    Init,
    Running,
    Paused,
    Shutdown
};

struct GameLoopConfig {
    f64  fixedDeltaTime = 1.0 / 60.0;
    f64  maxFrameTime   = 0.25;
    u32  targetFPS      = 0;
    bool vsync          = true;
    bool interpolation  = true;
};

class IGameCallbacks {
public:
    virtual ~IGameCallbacks() = default;

    virtual void onBeginFrame() {}
    virtual void onFixedUpdate(f64 dt) { (void)dt; }
    virtual void onRender(f64 alpha) { (void)alpha; }
    virtual void onEndFrame() {}
};

class GameLoop {
public:
    explicit GameLoop(const GameLoopConfig& config = {});
    ~GameLoop();

    void init();
    void tick(f64 deltaTime);
    void pause();
    void resume();
    void shutdown();

    GameState state()              const { return m_state; }
    f64       elapsedTime()        const { return m_elapsedTime; }
    f64       interpolationAlpha() const { return m_alpha; }
    u64       frameCount()         const { return m_frameCount; }
    f64       accumulator()        const { return m_accumulator; }

    void setCallbacks(IGameCallbacks* callbacks);

    std::function<void(f64 dt)>    onFixedUpdate;
    std::function<void(f64 alpha)> onRender;
    std::function<void()>          onBeginFrame;
    std::function<void()>          onEndFrame;

    const GameLoopConfig& config() const { return m_config; }

private:
    void processFixedUpdate(f64 dt);

    GameLoopConfig   m_config;
    IGameCallbacks*  m_callbacks   = nullptr;
    GameState        m_state       = GameState::Init;
    f64              m_accumulator = 0.0;
    f64              m_elapsedTime = 0.0;
    f64              m_alpha       = 0.0;
    u64              m_frameCount  = 0;
};

}
