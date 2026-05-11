/**
 * @file test_script_seq.cpp
 * @brief Standalone sequential test for ScriptEngine crash debugging
 *
 * Tests all scripting features sequentially in one process to catch
 * cross-contamination or use-after-free bugs.
 *
 * Build:
 *   g++ -std=c++20 -g -O0 -o test_script_seq tests/test_script_seq.cpp \
 *       -Isrc -Ibuild/_deps/sol2-src/include \
 *       -Ibuild/_deps/lua54-src/src \
 *       build/libCaffeine.a build/liblua54.a \
 *       -lm -ldl -lstdc++
 *
 * Or via CMake:
 *   add_executable(script_seq tests/test_script_seq.cpp)
 *   target_link_libraries(script_seq Caffeine lua54)
 */

#include "script/ScriptEngine.hpp"
#include "script/ScriptSystem.hpp"
#include "script/ScriptTypes.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "input/InputManager.hpp"
#include "events/EventBus.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Caffeine;
using namespace Caffeine::Script;

// ============================================================================
// Test infrastructure
// ============================================================================

static int g_total = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) \
    do { \
        g_total++; \
        printf("  TEST %d: %s ... ", g_total, name); \
        fflush(stdout); \
        bool _ok = true;

#define END_TEST(name) \
        if (_ok) { \
            printf("PASS\n"); \
            g_passed++; \
        } else { \
            printf("FAIL\n"); \
            g_failed++; \
        } \
    } while(0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("\n    FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        _ok = false; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        printf("\n    FAIL at %s:%d: %s == %s  (got %lld, expected %lld)\n", \
               __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
        _ok = false; \
    } \
} while(0)

#define CHECK_APPROX(a, b, eps) do { \
    auto _a = (a); auto _b = (b); \
    if (std::abs(_a - _b) > (eps)) { \
        printf("\n    FAIL at %s:%d: %s ≈ %s  (got %f, expected %f, eps %f)\n", \
               __FILE__, __LINE__, #a, #b, (double)_a, (double)_b, (double)(eps)); \
        _ok = false; \
    } \
} while(0)

struct TestFixture {
    ECS::World world;
    Input::InputManager input;
    Events::EventBus events;
    ScriptEngine engine;

    TestFixture() {
        engine.init({&world, &input, &events});
    }

    TestFixture(const TestFixture&) = delete;
    TestFixture& operator=(const TestFixture&) = delete;

    ~TestFixture() {
        engine.shutdown();
    }

    ECS::Entity createEntity(const char* scriptPath) {
        auto e = world.create("TestEntity");
        ScriptComponent sc;
        sc.scriptPath = scriptPath;
        e.add<ScriptComponent>(std::move(sc));
        return e;
    }
};

// ============================================================================
// Test 1: init/shutdown
// ============================================================================

void testInitShutdown() {
    TEST("ScriptEngine init and shutdown");
    {
        ECS::World w;
        Input::InputManager in;
        Events::EventBus ev;
        ScriptEngine eng;
        CHECK(eng.init({&w, &in, &ev}));
    }
    END_TEST("ScriptEngine init and shutdown");
}

// ============================================================================
// Test 2: loadString
// ============================================================================

void testLoadString() {
    TEST("loadString marks script as loaded");
    TestFixture fix;
    CHECK(fix.engine.loadString("function onCreate(eid) end", "test.lua"));
    CHECK(fix.engine.isLoaded("test.lua"));
    CHECK(!fix.engine.isLoaded("nonexistent.lua"));
    END_TEST("loadString marks script as loaded");
}

// ============================================================================
// Test 3: callOnCreate sets transform
// ============================================================================

void testCallOnCreateTransform() {
    TEST("callOnCreate moves entity via world transform binding");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 42.5, y = 99.5, rotation = 1.5, scaleX = 2.0, scaleY = 3.0})
        end
    )";
    CHECK(fix.engine.loadString(code, "create_test.lua"));
    auto entity = fix.world.create("Test");
    CHECK(fix.engine.callOnCreate("create_test.lua", entity));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 42.5f, 0.001f);
    CHECK_APPROX(pos->y, 99.5f, 0.001f);
    auto* rot = entity.get<ECS::Rotation>();
    CHECK(rot != nullptr);
    CHECK_APPROX(rot->angle, 1.5f, 0.001f);
    END_TEST("callOnCreate moves entity via world transform binding");
}

// ============================================================================
// Test 4: callOnUpdate with dt
// ============================================================================

void testCallOnUpdate() {
    TEST("callOnUpdate receives correct delta time");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid) end
        function onUpdate(eid, dt)
            local t = caffeine.world.getTransform(eid)
            caffeine.world.setTransform(eid, {x = t.x + dt, y = t.y + dt})
        end
    )";
    CHECK(fix.engine.loadString(code, "update_test.lua"));
    auto entity = fix.world.create("Test");
    entity.add<ECS::Position2D>(ECS::Position2D{10.0f, 20.0f});
    CHECK(fix.engine.callOnCreate("update_test.lua", entity));
    CHECK(fix.engine.callOnUpdate("update_test.lua", entity, 3.0f));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 13.0f, 0.001f);
    CHECK_APPROX(pos->y, 23.0f, 0.001f);
    END_TEST("callOnUpdate receives correct delta time");
}

// ============================================================================
// Test 5: callOnDestroy
// ============================================================================

void testCallOnDestroy() {
    TEST("callOnDestroy fires cleanly");
    TestFixture fix;
    const char* code = R"(
        function onDestroy(eid)
            caffeine.world.setTransform(eid, {x = 0, y = 0})
        end
    )";
    CHECK(fix.engine.loadString(code, "destroy_test.lua"));
    auto entity = fix.world.create("Test");
    entity.add<ECS::Position2D>(ECS::Position2D{100.0f, 200.0f});
    CHECK(fix.engine.callOnDestroy("destroy_test.lua", entity));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 0.0f, 0.001f);
    CHECK_APPROX(pos->y, 0.0f, 0.001f);
    END_TEST("callOnDestroy fires cleanly");
}

// ============================================================================
// Test 6: ScriptSystem onCreate
// ============================================================================

void testSystemOnCreate() {
    TEST("ScriptSystem calls onCreate on first frame");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 77.0, y = 88.0})
        end
        function onUpdate(eid, dt) end
    )";
    CHECK(fix.engine.loadString(code, "sys_test.lua"));
    auto entity = fix.createEntity("sys_test.lua");
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 77.0f, 0.001f);
    CHECK_APPROX(pos->y, 88.0f, 0.001f);
    END_TEST("ScriptSystem calls onCreate on first frame");
}

// ============================================================================
// Test 7: ScriptSystem onUpdate
// ============================================================================

void testSystemOnUpdate() {
    TEST("ScriptSystem calls onUpdate every frame after onCreate");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 0, y = 0})
        end
        function onUpdate(eid, dt)
            local t = caffeine.world.getTransform(eid)
            caffeine.world.setTransform(eid, {x = t.x + 1, y = t.y + dt})
        end
    )";
    CHECK(fix.engine.loadString(code, "sys_update.lua"));
    auto entity = fix.createEntity("sys_update.lua");
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 1.0f);
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 1.0f, 0.001f);
    CHECK_APPROX(pos->y, 1.0f, 0.001f);
    system.onUpdate(fix.world, 2.0f);
    CHECK_APPROX(pos->x, 2.0f, 0.001f);
    CHECK_APPROX(pos->y, 3.0f, 0.001f);
    END_TEST("ScriptSystem calls onUpdate every frame after onCreate");
}

// ============================================================================
// Test 8: Input isKeyDown
// ============================================================================

void testInputKeyDown() {
    TEST("Input isKeyDown reflects injected key state");
    TestFixture fix;
    const char* code = R"(
        function onUpdate(eid, dt)
            if caffeine.input.isKeyDown("Space") then
                caffeine.world.setTransform(eid, {x = 1, y = 0})
            else
                caffeine.world.setTransform(eid, {x = 0, y = 0})
            end
        end
    )";
    CHECK(fix.engine.loadString(code, "input_key.lua"));
    auto entity = fix.createEntity("input_key.lua");
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 0.0f, 0.001f);
    fix.input.injectKeyDown(Input::Key::Space);
    system.onUpdate(fix.world, 0.0f);
    CHECK_APPROX(pos->x, 1.0f, 0.001f);
    END_TEST("Input isKeyDown reflects injected key state");
}

// ============================================================================
// Test 9: Input getAxis
// ============================================================================

void testInputAxis() {
    TEST("Input getAxis returns correct axis value");
    TestFixture fix;
    const char* code = R"(
        function onUpdate(eid, dt)
            local h = caffeine.input.getAxis("Horizontal")
            caffeine.world.setTransform(eid, {x = h, y = 0})
        end
    )";
    CHECK(fix.engine.loadString(code, "input_axis.lua"));
    auto entity = fix.createEntity("input_axis.lua");
    fix.input.bindAxis(Input::Axis::MoveX,
                       Input::Binding::fromKey(Input::Key::A),
                       Input::Binding::fromKey(Input::Key::D));
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 0.0f, 0.001f);
    fix.input.injectKeyDown(Input::Key::D);
    system.onUpdate(fix.world, 0.0f);
    CHECK_APPROX(pos->x, 1.0f, 0.001f);
    END_TEST("Input getAxis returns correct axis value");
}

// ============================================================================
// Test 10: Input mousePosition
// ============================================================================

void testInputMouse() {
    TEST("Input mousePosition returns correct coords");
    TestFixture fix;
    const char* code = R"(
        function onUpdate(eid, dt)
            local mx, my = caffeine.input.mousePosition()
            caffeine.world.setTransform(eid, {x = mx, y = my})
        end
    )";
    CHECK(fix.engine.loadString(code, "input_mouse.lua"));
    auto entity = fix.createEntity("input_mouse.lua");
    fix.input.injectMouseMove(320.0f, 240.0f);
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 320.0f, 0.001f);
    CHECK_APPROX(pos->y, 240.0f, 0.001f);
    END_TEST("Input mousePosition returns correct coords");
}

// ============================================================================
// Test 11: Math vec2 length/normalize
// ============================================================================

void testMathLength() {
    TEST("Math vec2 length and normalize work");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            local a = caffeine.math.vec2(3, 4)
            local len = caffeine.math.length(a)
            local n = caffeine.math.normalize(a)
            caffeine.world.setTransform(eid, {x = len, y = n.x})
        end
    )";
    CHECK(fix.engine.loadString(code, "math_len.lua"));
    auto entity = fix.world.create("Test");
    CHECK(fix.engine.callOnCreate("math_len.lua", entity));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 5.0f, 0.001f);
    CHECK_APPROX(pos->y, 3.0f/5.0f, 0.001f);
    END_TEST("Math vec2 length and normalize work");
}

// ============================================================================
// Test 12: Math vec2 arithmetic
// ============================================================================

void testMathArith() {
    TEST("Math vec2 arithmetic operations work");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            local a = caffeine.math.vec2(10, 20)
            local b = caffeine.math.vec2(3, 4)
            local sum = caffeine.math.add(a, b)
            local d = caffeine.math.dot(a, b)
            caffeine.world.setTransform(eid, {x = sum.x, y = d})
        end
    )";
    CHECK(fix.engine.loadString(code, "math_arith.lua"));
    auto entity = fix.world.create("Test");
    CHECK(fix.engine.callOnCreate("math_arith.lua", entity));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 13.0f, 0.001f);
    CHECK_APPROX(pos->y, 10*3 + 20*4.0f, 0.001f);
    END_TEST("Math vec2 arithmetic operations work");
}

// ============================================================================
// Test 13: Sandbox
// ============================================================================

void testSandbox() {
    TEST("Sandbox blocks dofile and load");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            if dofile ~= nil then
                caffeine.world.setTransform(eid, {x = 1, y = 0})
            end
        end
    )";
    CHECK(fix.engine.loadString(code, "sandbox.lua"));
    auto entity = fix.world.create("Test");
    entity.add<ECS::Position2D>(ECS::Position2D{0.0f, 0.0f});
    CHECK(fix.engine.callOnCreate("sandbox.lua", entity));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 0.0f, 0.001f);
    END_TEST("Sandbox blocks dofile and load");
}

// ============================================================================
// Test 14: Lua error handling
// ============================================================================

void testErrorHandling() {
    TEST("Lua runtime error in callOnCreate doesn't crash");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            error("intentional crash test")
        end
    )";
    CHECK(fix.engine.loadString(code, "crash_test.lua"));
    auto entity = fix.world.create("Test");
    CHECK(!fix.engine.callOnCreate("crash_test.lua", entity));
    CHECK(fix.engine.loadString("function test() end", "still_works.lua"));
    END_TEST("Lua runtime error in callOnCreate doesn't crash");
}

// ============================================================================
// Test 15: Multiple entities with same script
// ============================================================================

void testMultiEntity() {
    TEST("Multiple entities with same script each get onCreate");
    TestFixture fix;
    const char* code = R"(
        counter = 0
        function onCreate(eid)
            counter = counter + 1
            caffeine.world.setTransform(eid, {x = counter, y = 0})
        end
        function onUpdate(eid, dt) end
    )";
    CHECK(fix.engine.loadString(code, "multi.lua"));
    auto e1 = fix.createEntity("multi.lua");
    auto e2 = fix.createEntity("multi.lua");
    auto e3 = fix.createEntity("multi.lua");
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);
    auto* p1 = e1.get<ECS::Position2D>();
    auto* p2 = e2.get<ECS::Position2D>();
    auto* p3 = e3.get<ECS::Position2D>();
    CHECK(p1 != nullptr);
    CHECK(p2 != nullptr);
    CHECK(p3 != nullptr);
    CHECK_APPROX(p1->x, 1.0f, 0.001f);
    CHECK_APPROX(p2->x, 2.0f, 0.001f);
    CHECK_APPROX(p3->x, 3.0f, 0.001f);
    END_TEST("Multiple entities with same script each get onCreate");
}

// ============================================================================
// Test 16: Event bindings
// ============================================================================

void testEvents() {
    TEST("Event on and emit trigger Lua callbacks");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            caffeine.events.on("move", function(x, y)
                caffeine.world.setTransform(eid, {x = x, y = y})
            end)
        end
        function onUpdate(eid, dt)
            caffeine.events.emit("move", dt, dt * 2)
        end
    )";
    CHECK(fix.engine.loadString(code, "event_test.lua"));
    auto entity = fix.createEntity("event_test.lua");
    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 10.0f);
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 10.0f, 0.001f);
    CHECK_APPROX(pos->y, 20.0f, 0.001f);
    END_TEST("Event on and emit trigger Lua callbacks");
}

// ============================================================================
// Test 17: Velocity bindings
// ============================================================================

void testVelocity() {
    TEST("World getVelocity and setVelocity work");
    TestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setVelocity(eid, {x = 5.0, y = 10.0})
            local v = caffeine.world.getVelocity(eid)
            caffeine.world.setTransform(eid, {x = v.x, y = v.y})
        end
    )";
    CHECK(fix.engine.loadString(code, "vel_test.lua"));
    auto entity = fix.world.create("Test");
    CHECK(fix.engine.callOnCreate("vel_test.lua", entity));
    auto* pos = entity.get<ECS::Position2D>();
    CHECK(pos != nullptr);
    CHECK_APPROX(pos->x, 5.0f, 0.001f);
    CHECK_APPROX(pos->y, 10.0f, 0.001f);
    END_TEST("World getVelocity and setVelocity work");
}

// ============================================================================
// Test 18: Isolated init/shutdown cycle (stress test)
// ============================================================================

void testRepeatedInit() {
    TEST("Multiple init/shutdown cycles");
    for (int i = 0; i < 5; i++) {
        ECS::World w;
        Input::InputManager in;
        Events::EventBus ev;
        ScriptEngine eng;
        CHECK(eng.init({&w, &in, &ev}));
        CHECK(eng.loadString("function onCreate(eid) end", "a.lua"));
        CHECK(eng.isLoaded("a.lua"));
        auto entity = w.create("Test");
        CHECK(eng.callOnCreate("a.lua", entity));
    }
    END_TEST("Multiple init/shutdown cycles");
}

// ============================================================================
// Test 19: Interleaved script engines
// ============================================================================

void testInterleavedEngines() {
    TEST("Two engines active simultaneously with different scripts");
    ECS::World w1, w2;
    Input::InputManager in1, in2;
    Events::EventBus ev1, ev2;
    ScriptEngine eng1, eng2;

    CHECK(eng1.init({&w1, &in1, &ev1}));
    CHECK(eng2.init({&w2, &in2, &ev2}));

    const char* code1 = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 111, y = 222})
        end
    )";
    const char* code2 = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 333, y = 444})
        end
    )";

    CHECK(eng1.loadString(code1, "eng1.lua"));
    CHECK(eng2.loadString(code2, "eng2.lua"));

    auto e1 = w1.create("E1");
    auto e2 = w2.create("E2");

    CHECK(eng1.callOnCreate("eng1.lua", e1));
    CHECK(eng2.callOnCreate("eng2.lua", e2));

    auto* p1 = e1.get<ECS::Position2D>();
    auto* p2 = e2.get<ECS::Position2D>();
    CHECK(p1 != nullptr);
    CHECK(p2 != nullptr);
    CHECK_APPROX(p1->x, 111.0f, 0.001f);
    CHECK_APPROX(p2->x, 333.0f, 0.001f);

    eng1.shutdown();
    eng2.shutdown();
    END_TEST("Two engines active simultaneously with different scripts");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("ScriptEngine Sequential Test Suite\n");
    printf("==================================\n\n");

    testInitShutdown();
    testLoadString();
    testCallOnCreateTransform();
    testCallOnUpdate();
    testCallOnDestroy();
    testSystemOnCreate();
    testSystemOnUpdate();
    testInputKeyDown();
    testInputAxis();
    testInputMouse();
    testMathLength();
    testMathArith();
    testSandbox();
    testErrorHandling();
    testMultiEntity();
    testEvents();
    testVelocity();
    testRepeatedInit();
    testInterleavedEngines();

    printf("\n==================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           g_passed, g_failed, g_total);

    return g_failed > 0 ? 1 : 0;
}
