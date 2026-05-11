#include "catch.hpp"

#ifdef CF_HAS_SCRIPTING

#include "../src/script/ScriptEngine.hpp"
#include "../src/script/ScriptSystem.hpp"
#include "../src/script/ScriptTypes.hpp"
#include "../src/ecs/World.hpp"
#include "../src/ecs/Components.hpp"
#include "../src/input/InputManager.hpp"
#include "../src/events/EventBus.hpp"
#include "../src/debug/LogSystem.hpp"

#include <string>

using namespace Caffeine;
using namespace Caffeine::Script;

// ============================================================================
// Helpers
// ============================================================================

struct ScriptTestFixture {
    ECS::World world;
    Input::InputManager input;
    Events::EventBus events;
    ScriptEngine engine;

    ScriptTestFixture() {
        engine.init({&world, &input, &events});
    }

    ScriptTestFixture(const ScriptTestFixture&) = delete;
    ScriptTestFixture& operator=(const ScriptTestFixture&) = delete;

    ~ScriptTestFixture() {
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
// 1. ScriptEngine init/shutdown
// ============================================================================

TEST_CASE("ScriptEngine init and shutdown", "[script]") {
    ECS::World world;
    Input::InputManager input;
    Events::EventBus events;
    ScriptEngine engine;

    REQUIRE(engine.init({&world, &input, &events}));
    // shutdown in destructor
}

// ============================================================================
// 2. loadString stores script and isLoaded reflects it
// ============================================================================

TEST_CASE("loadString marks script as loaded", "[script]") {
    ScriptTestFixture fix;

    REQUIRE(fix.engine.loadString("function onCreate(eid) end", "test.lua"));
    REQUIRE(fix.engine.isLoaded("test.lua"));
    REQUIRE_FALSE(fix.engine.isLoaded("nonexistent.lua"));
}

// ============================================================================
// 3. callOnCreate receives correct entity ID — positions via world binding
// ============================================================================

TEST_CASE("callOnCreate moves entity via world transform binding", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 42.5, y = 99.5, rotation = 1.5, scaleX = 2.0, scaleY = 3.0})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "create_test.lua"));

    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("create_test.lua", entity));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(42.5f));
    REQUIRE(pos->y == Approx(99.5f));

    auto* rot = entity.get<ECS::Rotation>();
    REQUIRE(rot != nullptr);
    REQUIRE(rot->angle == Approx(1.5f));
}

// ============================================================================
// 4. callOnUpdate receives dt
// ============================================================================

TEST_CASE("callOnUpdate receives correct delta time", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid) end
        function onUpdate(eid, dt)
            local t = caffeine.world.getTransform(eid)
            caffeine.world.setTransform(eid, {x = t.x + dt, y = t.y + dt})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "update_test.lua"));

    auto entity = fix.world.create("Test");
    entity.add<ECS::Position2D>(ECS::Position2D{10.0f, 20.0f});

    REQUIRE(fix.engine.callOnCreate("update_test.lua", entity));

    REQUIRE(fix.engine.callOnUpdate("update_test.lua", entity, 3.0f));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(13.0f));
    REQUIRE(pos->y == Approx(23.0f));
}

// ============================================================================
// 5. callOnDestroy fires without error
// ============================================================================

TEST_CASE("callOnDestroy fires cleanly", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onDestroy(eid)
            -- cleanup: reset position to zero
            caffeine.world.setTransform(eid, {x = 0, y = 0})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "destroy_test.lua"));

    auto entity = fix.world.create("Test");
    entity.add<ECS::Position2D>(ECS::Position2D{100.0f, 200.0f});

    REQUIRE(fix.engine.callOnDestroy("destroy_test.lua", entity));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(0.0f));
    REQUIRE(pos->y == Approx(0.0f));
}

// ============================================================================
// 6. ScriptSystem dispatches onCreate and onUpdate
// ============================================================================

TEST_CASE("ScriptSystem calls onCreate on first frame", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 77.0, y = 88.0})
        end
        function onUpdate(eid, dt) end
    )";
    REQUIRE(fix.engine.loadString(code, "sys_test.lua"));

    auto entity = fix.createEntity("sys_test.lua");

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(77.0f));
    REQUIRE(pos->y == Approx(88.0f));
}

TEST_CASE("ScriptSystem calls onUpdate every frame after onCreate", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setTransform(eid, {x = 0, y = 0})
        end
        function onUpdate(eid, dt)
            local t = caffeine.world.getTransform(eid)
            caffeine.world.setTransform(eid, {x = t.x + 1, y = t.y + dt})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "sys_update.lua"));

    auto entity = fix.createEntity("sys_update.lua");

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 1.0f); // frame 1: onCreate + onUpdate(1.0)

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(1.0f)); // 0 + 1
    REQUIRE(pos->y == Approx(1.0f)); // 0 + 1.0

    system.onUpdate(fix.world, 2.0f); // frame 2: onUpdate(2.0) only

    REQUIRE(pos->x == Approx(2.0f)); // 1 + 1
    REQUIRE(pos->y == Approx(3.0f)); // 1 + 2.0
}

// ============================================================================
// 7. Input binding — isKeyDown
// ============================================================================

TEST_CASE("Input isKeyDown reflects injected key state", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onUpdate(eid, dt)
            if caffeine.input.isKeyDown("Space") then
                caffeine.world.setTransform(eid, {x = 1, y = 0})
            else
                caffeine.world.setTransform(eid, {x = 0, y = 0})
            end
        end
    )";
    REQUIRE(fix.engine.loadString(code, "input_key.lua"));

    auto entity = fix.createEntity("input_key.lua");

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f); // frame: key not pressed

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(0.0f));

    fix.input.injectKeyDown(Input::Key::Space);
    system.onUpdate(fix.world, 0.0f); // frame: Space pressed

    REQUIRE(pos->x == Approx(1.0f));
}

// ============================================================================
// 8. Input binding — getAxis
// ============================================================================

TEST_CASE("Input getAxis returns correct axis value", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onUpdate(eid, dt)
            local h = caffeine.input.getAxis("Horizontal")
            caffeine.world.setTransform(eid, {x = h, y = 0})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "input_axis.lua"));

    auto entity = fix.createEntity("input_axis.lua");

    // Bind axis: A = negative, D = positive
    fix.input.bindAxis(Input::Axis::MoveX,
                       Input::Binding::fromKey(Input::Key::A),
                       Input::Binding::fromKey(Input::Key::D));

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f); // frame 1: axis is 0

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(0.0f));

    fix.input.injectKeyDown(Input::Key::D);
    system.onUpdate(fix.world, 0.0f); // frame 2: axis is +1

    REQUIRE(pos->x == Approx(1.0f));
}

// ============================================================================
// 9. Input binding — mousePosition
// ============================================================================

TEST_CASE("Input mousePosition returns correct coords", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onUpdate(eid, dt)
            local mx, my = caffeine.input.mousePosition()
            caffeine.world.setTransform(eid, {x = mx, y = my})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "input_mouse.lua"));

    auto entity = fix.createEntity("input_mouse.lua");

    fix.input.injectMouseMove(320.0f, 240.0f);

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(320.0f));
    REQUIRE(pos->y == Approx(240.0f));
}

// ============================================================================
// 10. Math binding — length and normalize
// ============================================================================

TEST_CASE("Math vec2 length and normalize work", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            local a = caffeine.math.vec2(3, 4)
            local len = caffeine.math.length(a)
            local n = caffeine.math.normalize(a)
            caffeine.world.setTransform(eid, {x = len, y = n.x})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "math_len.lua"));

    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("math_len.lua", entity));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(5.0f));   // length of (3,4)
    REQUIRE(pos->y == Approx(3.0f / 5.0f)); // 0.6
}

// ============================================================================
// 11. Math binding — add, sub, dot, scale
// ============================================================================

TEST_CASE("Math vec2 arithmetic operations work", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            local a = caffeine.math.vec2(10, 20)
            local b = caffeine.math.vec2(3, 4)
            local sum = caffeine.math.add(a, b)
            local diff = caffeine.math.sub(a, b)
            local d = caffeine.math.dot(a, b)
            local s = caffeine.math.scale(a, 2)
            caffeine.world.setTransform(eid, {x = sum.x, y = d})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "math_arith.lua"));

    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("math_arith.lua", entity));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(13.0f));  // 10 + 3
    REQUIRE(pos->y == Approx(10*3 + 20*4.0f)); // 110
}

// ============================================================================
// 12. Sandbox — dangerous globals are blocked
// ============================================================================

TEST_CASE("Sandbox blocks dofile and load", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            -- dofile, load, loadfile should be nil
            if dofile ~= nil then
                caffeine.world.setTransform(eid, {x = 1, y = 0})
            end
        end
    )";
    REQUIRE(fix.engine.loadString(code, "sandbox.lua"));

    auto entity = fix.world.create("Test");
    entity.add<ECS::Position2D>(ECS::Position2D{0.0f, 0.0f});

    REQUIRE(fix.engine.callOnCreate("sandbox.lua", entity));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(0.0f)); // dofile was nil so transform wasn't set
}

// ============================================================================
// 13. Lua error in lifecycle doesn't crash
// ============================================================================

TEST_CASE("Lua runtime error in callOnCreate doesn't crash", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            error("intentional crash test")
        end
    )";
    REQUIRE(fix.engine.loadString(code, "crash_test.lua"));

    auto entity = fix.world.create("Test");
    // Should return false but not crash
    REQUIRE_FALSE(fix.engine.callOnCreate("crash_test.lua", entity));
    // Engine is still usable after error
    REQUIRE(fix.engine.loadString("function test() end", "still_works.lua"));
}

// ============================================================================
// 14. Multiple entities with same script
// ============================================================================

TEST_CASE("Multiple entities with same script each get onCreate", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        counter = 0
        function onCreate(eid)
            counter = counter + 1
            caffeine.world.setTransform(eid, {x = counter, y = 0})
        end
        function onUpdate(eid, dt) end
    )";
    REQUIRE(fix.engine.loadString(code, "multi.lua"));

    auto e1 = fix.createEntity("multi.lua");
    auto e2 = fix.createEntity("multi.lua");
    auto e3 = fix.createEntity("multi.lua");

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 0.0f);

    auto* p1 = e1.get<ECS::Position2D>();
    auto* p2 = e2.get<ECS::Position2D>();
    auto* p3 = e3.get<ECS::Position2D>();
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);
    REQUIRE(p1->x == Approx(1.0f));
    REQUIRE(p2->x == Approx(2.0f));
    REQUIRE(p3->x == Approx(3.0f));
}

// ============================================================================
// 15. Event binding — on/emit/off (smoke test, observable via components)
// ============================================================================

TEST_CASE("Event on and emit trigger Lua callbacks", "[script]") {
    ScriptTestFixture fix;

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
    REQUIRE(fix.engine.loadString(code, "event_test.lua"));

    auto entity = fix.createEntity("event_test.lua");

    ScriptSystem system(&fix.engine);
    system.onUpdate(fix.world, 10.0f); // onCreate + onUpdate -> emit(10, 20)

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(10.0f));
    REQUIRE(pos->y == Approx(20.0f));
}

// ============================================================================
// 16. Velocity bindings
// ============================================================================

TEST_CASE("World getVelocity and setVelocity work", "[script]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            caffeine.world.setVelocity(eid, {x = 5.0, y = 10.0})
            local v = caffeine.world.getVelocity(eid)
            caffeine.world.setTransform(eid, {x = v.x, y = v.y})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "vel_test.lua"));

    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("vel_test.lua", entity));

    auto* pos = entity.get<ECS::Position2D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == Approx(5.0f));
    REQUIRE(pos->y == Approx(10.0f));
}

#else // !CF_HAS_SCRIPTING

TEST_CASE("Scripting disabled — placeholder", "[script]") {
    // This file compiles to nothing when scripting is disabled
    REQUIRE(true);
}

#endif // CF_HAS_SCRIPTING
