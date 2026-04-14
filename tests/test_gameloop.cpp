#include "catch.hpp"
#include <thread>
#include <chrono>
#include "../src/core/GameLoop.hpp"

using namespace Caffeine;

TEST_CASE("GameLoopConfig - Default values", "[gameloop][config]") {
    GameLoopConfig cfg;
    REQUIRE_THAT(cfg.fixedDeltaTime, Catch::Matchers::WithinAbs(1.0 / 60.0, 1e-9));
    REQUIRE_THAT(cfg.maxFrameTime, Catch::Matchers::WithinAbs(0.25, 1e-9));
    REQUIRE(cfg.targetFPS == 0);
    REQUIRE(cfg.vsync == true);
    REQUIRE(cfg.interpolation == true);
}

TEST_CASE("GameState - Init is default", "[gameloop][state]") {
    GameLoop loop;
    REQUIRE(loop.state() == GameState::Init);
}

TEST_CASE("GameLoop - Init transitions to Running", "[gameloop][state]") {
    GameLoop loop;
    loop.init();
    REQUIRE(loop.state() == GameState::Running);
}

TEST_CASE("GameLoop - Pause transitions from Running", "[gameloop][state]") {
    GameLoop loop;
    loop.init();
    loop.pause();
    REQUIRE(loop.state() == GameState::Paused);
}

TEST_CASE("GameLoop - Resume transitions from Paused to Running", "[gameloop][state]") {
    GameLoop loop;
    loop.init();
    loop.pause();
    loop.resume();
    REQUIRE(loop.state() == GameState::Running);
}

TEST_CASE("GameLoop - Shutdown from Running", "[gameloop][state]") {
    GameLoop loop;
    loop.init();
    loop.shutdown();
    REQUIRE(loop.state() == GameState::Shutdown);
}

TEST_CASE("GameLoop - Shutdown from Paused", "[gameloop][state]") {
    GameLoop loop;
    loop.init();
    loop.pause();
    loop.shutdown();
    REQUIRE(loop.state() == GameState::Shutdown);
}

TEST_CASE("GameLoop - Pause from Init is ignored", "[gameloop][state]") {
    GameLoop loop;
    loop.pause();
    REQUIRE(loop.state() == GameState::Init);
}

TEST_CASE("GameLoop - Resume from Init is ignored", "[gameloop][state]") {
    GameLoop loop;
    loop.resume();
    REQUIRE(loop.state() == GameState::Init);
}

TEST_CASE("GameLoop - Tick does nothing before init", "[gameloop][state]") {
    GameLoop loop;
    loop.tick(1.0 / 60.0);
    REQUIRE(loop.frameCount() == 0);
}

TEST_CASE("GameLoop - Tick does nothing after shutdown", "[gameloop][state]") {
    GameLoop loop;
    loop.init();
    loop.shutdown();
    loop.tick(1.0 / 60.0);
    REQUIRE(loop.frameCount() == 0);
}

TEST_CASE("GameLoop - Frame count increments on tick", "[gameloop][tick]") {
    GameLoop loop;
    loop.init();
    loop.tick(1.0 / 60.0);
    REQUIRE(loop.frameCount() == 1);
    loop.tick(1.0 / 60.0);
    REQUIRE(loop.frameCount() == 2);
}

TEST_CASE("GameLoop - Elapsed time accumulates", "[gameloop][tick]") {
    GameLoop loop;
    loop.init();
    loop.tick(1.0 / 60.0);
    loop.tick(1.0 / 60.0);
    REQUIRE(loop.elapsedTime() > 0.0);
}

TEST_CASE("GameLoop - Fixed update fires at correct rate", "[gameloop][tick]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    GameLoop loop(cfg);
    loop.init();

    int fixedUpdateCount = 0;
    loop.onFixedUpdate = [&](f64) { fixedUpdateCount++; };

    loop.tick(1.0 / 60.0);
    REQUIRE(fixedUpdateCount == 1);
}

TEST_CASE("GameLoop - Multiple fixed updates per frame when dt is large", "[gameloop][tick]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    GameLoop loop(cfg);
    loop.init();

    int fixedUpdateCount = 0;
    loop.onFixedUpdate = [&](f64) { fixedUpdateCount++; };

    loop.tick(3.0 / 60.0);
    REQUIRE(fixedUpdateCount == 3);
}

TEST_CASE("GameLoop - No fixed update when dt is too small", "[gameloop][tick]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    GameLoop loop(cfg);
    loop.init();

    int fixedUpdateCount = 0;
    loop.onFixedUpdate = [&](f64) { fixedUpdateCount++; };

    loop.tick(0.001);
    REQUIRE(fixedUpdateCount == 0);
}

TEST_CASE("GameLoop - Spiral of death prevention clamps accumulator", "[gameloop][tick]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    cfg.maxFrameTime = 0.25;
    GameLoop loop(cfg);
    loop.init();

    int fixedUpdateCount = 0;
    loop.onFixedUpdate = [&](f64) { fixedUpdateCount++; };

    loop.tick(10.0);

    int maxPossibleUpdates = static_cast<int>(cfg.maxFrameTime / cfg.fixedDeltaTime);
    REQUIRE(fixedUpdateCount <= maxPossibleUpdates);
}

TEST_CASE("GameLoop - Interpolation alpha is in [0, 1)", "[gameloop][tick]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    cfg.interpolation = true;
    GameLoop loop(cfg);
    loop.init();

    loop.tick(0.5 / 60.0);
    f64 alpha = loop.interpolationAlpha();
    REQUIRE(alpha >= 0.0);
    REQUIRE(alpha < 1.0);
}

TEST_CASE("GameLoop - Alpha is zero when interpolation disabled", "[gameloop][tick]") {
    GameLoopConfig cfg;
    cfg.interpolation = false;
    GameLoop loop(cfg);
    loop.init();

    loop.tick(0.5 / 60.0);
    REQUIRE(loop.interpolationAlpha() == 0.0);
}

TEST_CASE("GameLoop - Paused state skips fixed update", "[gameloop][tick]") {
    GameLoop loop;
    loop.init();

    int fixedUpdateCount = 0;
    loop.onFixedUpdate = [&](f64) { fixedUpdateCount++; };

    loop.pause();
    loop.tick(1.0 / 60.0);
    REQUIRE(fixedUpdateCount == 0);
}

TEST_CASE("GameLoop - Paused state still calls render", "[gameloop][tick]") {
    GameLoop loop;
    loop.init();

    int renderCount = 0;
    loop.onRender = [&](f64) { renderCount++; };

    loop.pause();
    loop.tick(1.0 / 60.0);
    REQUIRE(renderCount == 1);
}

TEST_CASE("GameLoop - onBeginFrame and onEndFrame called per tick", "[gameloop][callbacks]") {
    GameLoop loop;
    loop.init();

    int beginCount = 0;
    int endCount = 0;
    loop.onBeginFrame = [&]() { beginCount++; };
    loop.onEndFrame = [&]() { endCount++; };

    loop.tick(1.0 / 60.0);
    REQUIRE(beginCount == 1);
    REQUIRE(endCount == 1);

    loop.tick(1.0 / 60.0);
    REQUIRE(beginCount == 2);
    REQUIRE(endCount == 2);
}

TEST_CASE("GameLoop - onRender called once per tick", "[gameloop][callbacks]") {
    GameLoop loop;
    loop.init();

    int renderCount = 0;
    loop.onRender = [&](f64) { renderCount++; };

    loop.tick(3.0 / 60.0);
    REQUIRE(renderCount == 1);
}

TEST_CASE("GameLoop - Fixed update receives correct dt", "[gameloop][callbacks]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    GameLoop loop(cfg);
    loop.init();

    f64 receivedDt = 0.0;
    loop.onFixedUpdate = [&](f64 dt) { receivedDt = dt; };

    loop.tick(1.0 / 60.0);
    REQUIRE_THAT(receivedDt, Catch::Matchers::WithinAbs(1.0 / 60.0, 1e-9));
}

TEST_CASE("GameLoop - IGameCallbacks interface works", "[gameloop][callbacks]") {
    struct TestCallbacks : IGameCallbacks {
        int fixedCount = 0;
        int renderCount = 0;
        int beginCount = 0;
        int endCount = 0;

        void onBeginFrame() override { beginCount++; }
        void onFixedUpdate(f64) override { fixedCount++; }
        void onRender(f64) override { renderCount++; }
        void onEndFrame() override { endCount++; }
    };

    TestCallbacks cb;
    GameLoop loop;
    loop.setCallbacks(&cb);
    loop.init();

    loop.tick(1.0 / 60.0);
    REQUIRE(cb.beginCount == 1);
    REQUIRE(cb.fixedCount == 1);
    REQUIRE(cb.renderCount == 1);
    REQUIRE(cb.endCount == 1);
}

TEST_CASE("GameLoop - Deterministic accumulation over 3600 frames", "[gameloop][integration]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 60.0;
    GameLoop loop(cfg);
    loop.init();

    int fixedUpdateCount = 0;
    loop.onFixedUpdate = [&](f64) { fixedUpdateCount++; };

    for (int i = 0; i < 3600; ++i) {
        loop.tick(1.0 / 60.0);
    }

    REQUIRE(fixedUpdateCount == 3600);
}

TEST_CASE("GameLoop - Custom config applies", "[gameloop][config]") {
    GameLoopConfig cfg;
    cfg.fixedDeltaTime = 1.0 / 30.0;
    cfg.maxFrameTime = 0.5;
    cfg.targetFPS = 120;
    cfg.vsync = false;
    cfg.interpolation = false;

    GameLoop loop(cfg);
    REQUIRE_THAT(loop.config().fixedDeltaTime, Catch::Matchers::WithinAbs(1.0 / 30.0, 1e-9));
    REQUIRE_THAT(loop.config().maxFrameTime, Catch::Matchers::WithinAbs(0.5, 1e-9));
    REQUIRE(loop.config().targetFPS == 120);
    REQUIRE(loop.config().vsync == false);
    REQUIRE(loop.config().interpolation == false);
}
