#include "catch.hpp"

#ifdef CF_HAS_SCRIPTING

#include "../src/caffeine/script/ScriptEngine.hpp"
#include "../src/caffeine/script/ScriptSystem.hpp"
#include "../src/caffeine/script/ScriptProperties.hpp"
#include "../src/caffeine/script/ScriptCatalog.hpp"
#include "../src/caffeine/script/DryadHost.hpp"
#include "../src/caffeine/physics/PhysicsWorld3D.hpp"
#include "../src/caffeine/audio/AudioService.hpp"
#include "../src/caffeine/scene/EditorSceneSerializer.hpp"
#include "../src/caffeine/script/ScriptTypes.hpp"
#include "../src/caffeine/ecs/World.hpp"
#include "../src/caffeine/ecs/Components.hpp"
#include "../src/caffeine/input/InputManager.hpp"
#include "../src/caffeine/events/EventBus.hpp"
#include "../src/caffeine/debug/LogSystem.hpp"

#include <fstream>
#include <algorithm>
#include <set>
#include <sstream>
#include <filesystem>
#include <string>

using namespace Caffeine;
using namespace Caffeine::Script;

// ============================================================================
// Helpers
// ============================================================================

static ECS::Transform transformAt(f32 x, f32 y) {
    ECS::Transform t;
    t.position.x = x;
    t.position.y = y;
    return t;
}

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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(42.5f));
    REQUIRE(pos->position.y == Approx(99.5f));

    REQUIRE(pos->rotation.z == Approx(1.5f));
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
    entity.add<ECS::Transform>(transformAt(10.0f, 20.0f));

    REQUIRE(fix.engine.callOnCreate("update_test.lua", entity));

    REQUIRE(fix.engine.callOnUpdate("update_test.lua", entity, 3.0f));

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(13.0f));
    REQUIRE(pos->position.y == Approx(23.0f));
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
    entity.add<ECS::Transform>(transformAt(100.0f, 200.0f));

    REQUIRE(fix.engine.callOnDestroy("destroy_test.lua", entity));

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(0.0f));
    REQUIRE(pos->position.y == Approx(0.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(77.0f));
    REQUIRE(pos->position.y == Approx(88.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(1.0f)); // 0 + 1
    REQUIRE(pos->position.y == Approx(1.0f)); // 0 + 1.0

    system.onUpdate(fix.world, 2.0f); // frame 2: onUpdate(2.0) only

    REQUIRE(pos->position.x == Approx(2.0f)); // 1 + 1
    REQUIRE(pos->position.y == Approx(3.0f)); // 1 + 2.0
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(0.0f));

    fix.input.injectKeyDown(Input::Key::Space);
    system.onUpdate(fix.world, 0.0f); // frame: Space pressed

    REQUIRE(pos->position.x == Approx(1.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(0.0f));

    fix.input.injectKeyDown(Input::Key::D);
    system.onUpdate(fix.world, 0.0f); // frame 2: axis is +1

    REQUIRE(pos->position.x == Approx(1.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(320.0f));
    REQUIRE(pos->position.y == Approx(240.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(5.0f));   // length of (3,4)
    REQUIRE(pos->position.y == Approx(3.0f / 5.0f)); // 0.6
}

// ============================================================================
// 10b. Math binding — 3D vectors and scalar helpers
// ============================================================================

TEST_CASE("Math supports 3D vectors and scalar helpers", "[script][math]") {
    ScriptTestFixture fix;

    const char* code = R"(
        function onCreate(eid)
            local m = caffeine.math
            local c = m.cross(m.vec3(1, 0, 0), m.vec3(0, 1, 0))            -- (0, 0, 1)
            local d = m.distance(m.vec3(0, 0, 0), m.vec3(2, 3, 6))         -- 7
            local r = m.remap(5, 0, 10, 100, 200)                          -- 150
            local a = m.approach(0, 10, 3)                                 -- 3
            local flat = m.add(m.vec2(1, 2), m.vec2(3, 4))                 -- stays 2D
            local keep2d = (flat.z == nil) and 1 or 0
            caffeine.world.setTransform(eid, {x = c.z + d, y = r + a, z = keep2d})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "math_3d.lua"));

    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("math_3d.lua", entity));

    auto* t = entity.get<ECS::Transform>();
    REQUIRE(t != nullptr);
    REQUIRE(t->position.x == Approx(8.0f));
    REQUIRE(t->position.y == Approx(153.0f));
    REQUIRE(t->position.z == Approx(1.0f));
}

// ============================================================================
// ScriptComponent: enabled, order and per-entity properties
// ============================================================================

TEST_CASE("ScriptComponent properties parse into typed exposed variables", "[script][component]") {
    const auto vars = parseScriptProperties("speed = 4.5; god=true\nlabel=hello world;broken;=x");
    REQUIRE(vars.size() == 3);
    REQUIRE(vars[0].name == "speed");
    REQUIRE(vars[0].kind == ScriptEngine::ExposedVar::Kind::Number);
    REQUIRE(vars[0].number == Approx(4.5));
    REQUIRE(vars[1].kind == ScriptEngine::ExposedVar::Kind::Boolean);
    REQUIRE(vars[1].boolean);
    REQUIRE(vars[2].kind == ScriptEngine::ExposedVar::Kind::String);
    REQUIRE(vars[2].string == "hello world");
}

TEST_CASE("ScriptSystem honours enabled, order and properties", "[script][component]") {
    ScriptTestFixture fix;
    REQUIRE(fix.engine.loadString(R"(
        speed = 1
        function onUpdate(eid, dt)
            local t = caffeine.world.getTransform(eid)
            caffeine.world.setTransform(eid, {x = speed, y = t.y})
        end
    )", "props.lua"));

    ScriptSystem system(&fix.engine);
    auto on = fix.createEntity("props.lua");
    on.get<ScriptComponent>()->properties = "speed=7";
    auto off = fix.createEntity("props.lua");
    off.get<ScriptComponent>()->enabled = false;
    off.add<ECS::Transform>(transformAt(0, 0));
    on.add<ECS::Transform>(transformAt(0, 0));

    system.onUpdate(fix.world, 0.016f);

    REQUIRE(on.get<ECS::Transform>()->position.x == Approx(7.0f));
    REQUIRE(off.get<ECS::Transform>()->position.x == Approx(0.0f));  // disabled: never ran
}

TEST_CASE("ScriptComponent fields survive a scene round trip", "[script][component][scene]") {
    ECS::World world;
    auto e = world.create("Scripted");
    ScriptComponent sc;
    sc.scriptPath = "scripts/a.lua";
    sc.enabled = false;
    sc.executionOrder = -5;
    sc.properties = "speed=2";
    e.add<ScriptComponent>(std::move(sc));

    std::vector<u8> bytes;
    Editor::SceneSerializer out(world);
    REQUIRE(out.serializeToMemory(bytes));

    ECS::World restored;
    Editor::SceneSerializer in(restored);
    REQUIRE(in.deserializeFromMemory(bytes));

    bool found = false;
    ECS::ComponentQuery q;
    q.with<ScriptComponent>();
    restored.forEach<ScriptComponent>(q, [&](ECS::Entity, ScriptComponent& r) {
        found = true;
        REQUIRE(r.scriptPath == "scripts/a.lua");
        REQUIRE_FALSE(r.enabled);
        REQUIRE(r.executionOrder == -5);
        REQUIRE(r.properties == "speed=2");
    });
    REQUIRE(found);
}

TEST_CASE("Isolated scripts keep private state per entity", "[script][component]") {
    ScriptTestFixture fix;
    REQUIRE(fix.engine.loadString("", "unused.lua"));
    {
        std::ofstream f("iso_test.lua");
        f << "count = 0\nfunction onUpdate(eid, dt)\n  count = count + 1\n"
             "  caffeine.world.setTransform(eid, {x = count, y = 0})\nend\n";
    }
    ScriptSystem system(&fix.engine);
    auto a = fix.createEntity("iso_test.lua");
    auto b = fix.createEntity("iso_test.lua");
    a.add<ECS::Transform>(transformAt(0, 0));
    b.add<ECS::Transform>(transformAt(0, 0));
    a.get<ScriptComponent>()->isolated = true;
    b.get<ScriptComponent>()->isolated = true;
    system.onUpdate(fix.world, 0.016f);
    system.onUpdate(fix.world, 0.016f);
    // Shared state would give 2 and 4; private state gives 2 and 2.
    REQUIRE(a.get<ECS::Transform>()->position.x == Approx(2.0f));
    REQUIRE(b.get<ECS::Transform>()->position.x == Approx(2.0f));
    std::remove("iso_test.lua");
}

TEST_CASE("Script catalog groups scripts by header tag or folder", "[script][catalog]") {
    const auto tagged = parseScriptMeta("scripts/x.lua",
        "-- @group Gameplay/Combat/Melee\n-- @name Sword\n-- @tags weapon, player\n-- @description Hits.\nfoo=1\n");
    REQUIRE(tagged.group == std::vector<std::string>{"Gameplay", "Combat", "Melee"});
    REQUIRE(tagged.name == "Sword");
    REQUIRE(tagged.tags.size() == 2);
    REQUIRE(tagged.description == "Hits.");

    const auto byFolder = parseScriptMeta("scripts/AI/Patrol/walk.dyd", "// plain\n");
    REQUIRE(byFolder.group == std::vector<std::string>{"AI", "Patrol"});
    REQUIRE(byFolder.name == "walk");

    const auto loose = parseScriptMeta("scripts/solo.lua", "");
    REQUIRE(loose.group.empty());

    const auto tree = buildScriptTree({tagged, byFolder, loose});
    REQUIRE(tree.find("Gameplay/Combat/Melee")->scripts.size() == 1);
    REQUIRE(tree.find("Gameplay")->totalScripts() == 1);
    REQUIRE(tree.find("AI/Patrol")->scripts[0].name == "walk");
    REQUIRE(tree.find("Ungrouped")->scripts.size() == 1);
    REQUIRE(tree.find("Nope") == nullptr);
}

TEST_CASE("Dump script API names", "[.apidump]") {
    ScriptTestFixture fix;
    std::ofstream out("/tmp/lua_api.txt");
    for (const auto& n : fix.engine.apiNames()) out << n << "\n";
#ifdef CF_HAS_DRYAD
    std::ofstream d("/tmp/dryad_api.txt");
    for (const auto& n : DryadHost::nativeNames()) d << n << "\n";
#endif
}

TEST_CASE("Entity names, hierarchy and random work from Lua", "[script][world]") {
    ScriptTestFixture fix;
    const char* code = R"(
        function onCreate(eid)
            local a = caffeine.world.create()
            local b = caffeine.world.create()
            caffeine.entity.setName(a, "Boss")
            caffeine.entity.setName(b, "Minion")
            local ok = caffeine.hierarchy.setParent(b, a)
            local cycle = caffeine.hierarchy.setParent(a, b)          -- refused: cycle
            local kids = caffeine.hierarchy.children(a)
            local found = caffeine.entity.find("Minion")
            caffeine.random.setSeed(42)
            local r1 = caffeine.random.range(0, 1)
            caffeine.random.setSeed(42)
            local r2 = caffeine.random.range(0, 1)
            local n = caffeine.random.int(3, 3)
            local score = 0
            if ok then score = score + 1 end
            if not cycle then score = score + 10 end
            if #kids == 1 and kids[1] == b and found == b then score = score + 100 end
            if r1 == r2 then score = score + 1000 end
            if caffeine.hierarchy.root(b) == a and n == 3 then score = score + 10000 end
            caffeine.world.setTransform(eid, {x = score, y = 0})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "world_api.lua"));
    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("world_api.lua", entity));
    REQUIRE(entity.get<ECS::Transform>()->position.x == Approx(11111.0f));
}

// ----------------------------------------------------------------------------
// Every scriptable function must be documented, and every documented function must exist.
// The reference lives in src/caffeine/script/api/*.json (see scripts/generate-script-api-docs.py).
// ----------------------------------------------------------------------------

static std::set<std::string> registryNames(const char* key) {
    namespace fs = std::filesystem;
    std::set<std::string> names;
    const std::string needle = std::string("\"") + key + "\": \"";
    for (const auto& file : fs::directory_iterator(CF_SCRIPT_API_DIR)) {
        if (file.path().extension() != ".json") continue;
        std::ifstream in(file.path());
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) {
            const size_t start = pos + needle.size();
            names.insert(text.substr(start, text.find('"', start) - start));
        }
    }
    // A namespace header ("lua": "caffeine.world") is a prefix of its functions, not a function.
    for (auto it = names.begin(); it != names.end();) {
        const std::string prefix = *it + ".";
        const bool isNamespace = std::any_of(names.begin(), names.end(), [&](const std::string& other) {
            return other.size() > prefix.size() && other.compare(0, prefix.size(), prefix) == 0;
        });
        it = isNamespace ? names.erase(it) : std::next(it);
    }
    return names;
}

TEST_CASE("Lua API is fully documented", "[script][api-docs]") {
    ScriptTestFixture fix;
    const auto documented = registryNames("lua");
    std::set<std::string> actual;
    for (const auto& n : fix.engine.apiNames()) actual.insert(n);
    for (const auto& n : actual) { INFO("undocumented Lua function: " << n); CHECK(documented.count(n) == 1); }
    for (const auto& n : documented) { INFO("documented but missing in Lua: " << n); CHECK(actual.count(n) == 1); }
}

#ifdef CF_HAS_DRYAD
TEST_CASE("Dryad natives are fully documented", "[dryad][api-docs]") {
    const auto documented = registryNames("dryad");
    std::set<std::string> actual;
    for (const auto& n : DryadHost::nativeNames()) actual.insert(n);
    for (const auto& n : actual) { INFO("undocumented Dryad native: " << n); CHECK(documented.count(n) == 1); }
    for (const auto& n : documented) { INFO("documented but missing in Dryad: " << n); CHECK(actual.count(n) == 1); }
}
#endif

TEST_CASE("physics3d queries and forces work from Lua", "[script][physics3d]") {
    ScriptTestFixture fix;
    Physics3D::PhysicsWorld3D physics;
    if (!physics.isAvailable()) { WARN("Bullet3 not enabled in this build"); return; }
    fix.engine.setPhysics3D(&physics);

    Physics3D::BodyDesc ground;
    ground.entityId = 1;
    ground.shape = Physics3D::ColliderShape3D::Box;
    ground.halfExtents = Vec3(5.0f, 0.5f, 5.0f);
    ground.bodyType = Physics3D::BodyType3D::Static;
    ground.mass = 0.0f;
    physics.upsertBody(ground);
    Physics3D::BodyDesc ball;
    ball.entityId = 2;
    ball.position = Vec3(0.0f, 3.0f, 0.0f);
    ball.shape = Physics3D::ColliderShape3D::Sphere;
    ball.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    physics.upsertBody(ball);
    physics.step(1.0f / 60.0f);

    const char* code = R"(
        function onCreate(eid)
            local hit = caffeine.physics3d.raycast(0, 10, 0, 0, -1, 0, 50)    -- hits the ball first
            local miss = caffeine.physics3d.raycast(0, 10, 0, 0, 1, 0, 50)    -- nothing above
            local ok = caffeine.physics3d.setVelocity(2, 4, 0, 0)
            local v = caffeine.physics3d.velocity(2)
            local nope = caffeine.physics3d.addForce(1, 0, 10, 0)              -- static: refused
            local score = 0
            if hit and hit.entity == 2 and hit.distance > 6 and hit.distance < 7.5 then score = score + 1 end
            if miss == nil then score = score + 10 end
            if ok and v and v.x > 3.9 then score = score + 100 end
            if not nope then score = score + 1000 end
            caffeine.world.setTransform(eid, {x = score, y = 0})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "phys3d.lua"));
    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("phys3d.lua", entity));
    REQUIRE(entity.get<ECS::Transform>()->position.x == Approx(1111.0f));
}

static void writeTestWav(const std::string& path, int samples, int rate) {
    std::ofstream f(path, std::ios::binary);
    auto u32le = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16le = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(samples) * 2;
    f.write("RIFF", 4); u32le(36 + dataBytes); f.write("WAVEfmt ", 8);
    u32le(16); u16le(1); u16le(1); u32le(static_cast<uint32_t>(rate)); u32le(static_cast<uint32_t>(rate) * 2); u16le(2); u16le(16);
    f.write("data", 4); u32le(dataBytes);
    for (int i = 0; i < samples; ++i) u16le(static_cast<uint16_t>((i % 100) * 100));
}

TEST_CASE("AudioService decodes WAV and is a safe no-op without a device", "[script][audio]") {
    writeTestWav("audio_test.wav", 800, 8000);
    Audio::AudioService audio;
    std::string error;
    REQUIRE(audio.preload("audio_test.wav", &error));
    REQUIRE(audio.duration("audio_test.wav") == Approx(0.1f));
    REQUIRE_FALSE(audio.preload("missing.wav"));
    if (!audio.init()) {  // no device (CI): nothing plays, nothing crashes
        REQUIRE(audio.play("audio_test.wav") == 0);
        REQUIRE_FALSE(audio.stop(1));
    }
    std::remove("audio_test.wav");
}

TEST_CASE("audio, scene and session APIs work from Lua", "[script][audio][scene]") {
    ScriptTestFixture fix;
    Audio::AudioService audio;  // not initialised: the API must still answer
    fix.engine.setAudio(&audio);
    fix.engine.sceneRequests().setCurrent("scenes/start.scene");
    const char* code = R"(
        function onCreate(eid)
            local score = 0
            if caffeine.audio.isAvailable() == false then score = score + 1 end
            if caffeine.audio.play("nope.wav") == 0 then score = score + 10 end
            caffeine.session.set("coins", 7)
            caffeine.session.set("name", "Ana")
            caffeine.session.set("hard", true)
            if caffeine.session.get("coins") == 7 and caffeine.session.get("name") == "Ana"
               and caffeine.session.get("hard") == true and caffeine.session.get("zzz") == nil then score = score + 100 end
            caffeine.session.remove("coins")
            if not caffeine.session.has("coins") then score = score + 1000 end
            if caffeine.scene.current() == "scenes/start.scene" and not caffeine.scene.isLoading() then score = score + 10000 end
            caffeine.scene.load("scenes/level2.scene")
            if caffeine.scene.isLoading() then score = score + 100000 end
            caffeine.world.setTransform(eid, {x = score, y = 0})
        end
    )";
    REQUIRE(fix.engine.loadString(code, "audio_scene.lua"));
    auto entity = fix.world.create("Test");
    REQUIRE(fix.engine.callOnCreate("audio_scene.lua", entity));
    REQUIRE(entity.get<ECS::Transform>()->position.x == Approx(111111.0f));
    REQUIRE(fix.engine.sceneRequests().hasPending());
    REQUIRE(fix.engine.sceneRequests().take() == "scenes/level2.scene");
    REQUIRE_FALSE(fix.engine.sceneRequests().hasPending());
    Script::SessionStore::instance().clear();
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(13.0f));  // 10 + 3
    REQUIRE(pos->position.y == Approx(10*3 + 20*4.0f)); // 110
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
    entity.add<ECS::Transform>(transformAt(0.0f, 0.0f));

    REQUIRE(fix.engine.callOnCreate("sandbox.lua", entity));

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(0.0f)); // dofile was nil so transform wasn't set
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

    auto* p1 = e1.get<ECS::Transform>();
    auto* p2 = e2.get<ECS::Transform>();
    auto* p3 = e3.get<ECS::Transform>();
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);
    REQUIRE(p1->position.x == Approx(1.0f));
    REQUIRE(p2->position.x == Approx(2.0f));
    REQUIRE(p3->position.x == Approx(3.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(10.0f));
    REQUIRE(pos->position.y == Approx(20.0f));
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

    auto* pos = entity.get<ECS::Transform>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == Approx(5.0f));
    REQUIRE(pos->position.y == Approx(10.0f));
}

// ============================================================================
// Generic component access (caffeine.component, reflection-driven)
// ============================================================================

#include "../src/caffeine/ecs/Components3D.hpp"
#include "../src/caffeine/ecs/MeshComponents.hpp"
#include "../src/caffeine/physics/PhysicsComponents3D.hpp"
#include "../src/caffeine/ecs/TerrainComponents.hpp"
#include "../src/caffeine/ecs/PostProcessComponents.hpp"
#include "../src/caffeine/audio/AudioComponents.hpp"
#include "../src/caffeine/navigation/NavVolume.hpp"

TEST_CASE("caffeine.component reads and writes any reflected component", "[script][component]") {
    ScriptTestFixture fix;
    auto entity = fix.world.create("Thing");

    const char* code = R"(
        function onCreate(eid)
            local C = caffeine.component
            assert(not C.has(eid, "Rigid Body 3D"))
            C.add(eid, "rigidbody3d")                         -- names ignore case and spaces
            assert(C.has(eid, "Rigid Body 3D"))
            C.setField(eid, "Rigid Body 3D", "Mass", 12.5)
            C.set(eid, "Rigid Body 3D", { Friction = 0.9, ["Body type"] = "Static" })
            local rb = C.get(eid, "Rigid Body 3D")
            assert(rb.Mass == 12.5)
            result_mass = rb.Mass
            result_type = C.getField(eid, "Rigid Body 3D", "Body type")

            C.set(eid, "Position 3D", { Position = { x = 1, y = 2, z = 3 } })
            local p = C.getField(eid, "Position 3D", "Position")
            result_pos = p.x + p.y * 10 + p.z * 100

            C.set(eid, "Mesh Filter", { Primitive = "Sphere", Mesh = "a.obj" })
            result_prim = C.getField(eid, "Mesh Filter", "Primitive")

            local names = C.names(eid)
            result_count = #names
            C.remove(eid, "Rigid Body 3D")
            result_removed = not C.has(eid, "Rigid Body 3D")
            result_list = #C.list()
        end
    )";
    REQUIRE(fix.engine.loadString(code, "component_test.lua"));
    REQUIRE(fix.engine.callOnCreate("component_test.lua", entity));

    // The C++ side sees what the script did.
    auto* mesh = entity.get<ECS::MeshFilterComponent>();
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->primitive == ECS::MeshPrimitive::Sphere);
    REQUIRE(mesh->customMeshPath == "a.obj");
    auto* pos = entity.get<ECS::Position3D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.z == Approx(3.0f));
    REQUIRE_FALSE(entity.has<Physics3D::RigidBody3D>());
}

TEST_CASE("caffeine.component reaches the whole registry, including generated components", "[script][component]") {
    ScriptTestFixture fix;
    auto entity = fix.world.create("Thing");
    const char* code = R"(
        function onCreate(eid)
            local C = caffeine.component
            registry_size = #C.list()
            C.set(eid, "Terrain", { ["Max height"] = 321, ["Splat Layer Paths 2"] = "my/rock.png" })
            C.set(eid, "Post Process", { ["Bloom: Intensity"] = 0.8, Enabled = true })
            C.set(eid, "Audio Emitter", { ["Clip path"] = "sfx/hit.wav", Loop = true })
            C.set(eid, "Nav Agent", { Speed = 7, Destination = { x = 1, y = 2, z = 3 } })
            C.set(eid, "Environment Effects", { ["Time of day"] = 6.5 })
            terrain_h = C.getField(eid, "Terrain", "Max height")
            terrain_path = C.getField(eid, "Terrain", "Splat Layer Paths 2")
            bloom = C.getField(eid, "Post Process", "Bloom: Intensity")
            dest = C.getField(eid, "Nav Agent", "Destination")
        end
    )";
    REQUIRE(fix.engine.loadString(code, "component_registry.lua"));
    REQUIRE(fix.engine.callOnCreate("component_registry.lua", entity));
    REQUIRE(entity.has<ECS::TerrainComponent>());
    REQUIRE(entity.get<ECS::TerrainComponent>()->maxHeight == Approx(321.0f));
    REQUIRE(std::string(entity.get<ECS::TerrainComponent>()->splatLayerPaths[1]) == "my/rock.png");
    REQUIRE(entity.get<ECS::PostProcessComponent>()->bloom.intensity == Approx(0.8f));
    REQUIRE(std::string(entity.get<Audio::AudioEmitter>()->clipPath.cStr()) == "sfx/hit.wav");
    REQUIRE(entity.get<Navigation::NavAgent>()->destination.z == Approx(3.0f));
}

TEST_CASE("caffeine.component reports mistakes with helpful messages", "[script][component]") {
    ScriptTestFixture fix;
    auto entity = fix.world.create("Thing");
    const char* code = R"(
        function onCreate(eid)
            local C = caffeine.component
            local ok, err = pcall(C.has, eid, "Flux Capacitor")
            unknown_component = (not ok) and err:find("unknown component", 1, true) ~= nil
            C.add(eid, "Rigid Body 3D")
            ok, err = pcall(C.setField, eid, "Rigid Body 3D", "Nope", 1)
            unknown_field = (not ok) and err:find("has no field", 1, true) ~= nil
            ok, err = pcall(C.setField, eid, "Rigid Body 3D", "Mass", "heavy")
            wrong_type = (not ok) and err:find("expects a number", 1, true) ~= nil
            ok, err = pcall(C.setField, eid, "Rigid Body 3D", "Body type", "Floaty")
            bad_enum = (not ok) and err:find("options", 1, true) ~= nil
            ok, err = pcall(C.get, 999999, "Rigid Body 3D")
            dead_entity = (not ok) and err:find("does not exist", 1, true) ~= nil
            no_component = C.get(eid, "Collider 3D") == nil
            assert(unknown_component and unknown_field and wrong_type and bad_enum and dead_entity and no_component)
        end
    )";
    REQUIRE(fix.engine.loadString(code, "component_errors.lua"));
    REQUIRE(fix.engine.callOnCreate("component_errors.lua", entity));
}

#else // !CF_HAS_SCRIPTING

TEST_CASE("Scripting disabled — placeholder", "[script]") {
    // This file compiles to nothing when scripting is disabled
    REQUIRE(true);
}

#endif // CF_HAS_SCRIPTING
