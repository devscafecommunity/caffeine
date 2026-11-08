#include "catch.hpp"

#ifdef CF_HAS_DRYAD

#include "../src/script/DryadHost.hpp"
#include "../src/script/nodes/NodeEmitter.hpp"
#include "../src/script/nodes/NodeGraphIO.hpp"
#include "../src/script/nodes/NodeRoundTrip.hpp"
#include "../src/ecs/Components.hpp"
#include "../src/ecs/Components3D.hpp"
#include "../src/ecs/MeshComponents.hpp"
#include "../src/ecs/World.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace Caffeine;
using namespace Caffeine::Script;
using namespace Caffeine::Script::Nodes;

static NodeGraph sampleMoveGraph() {
    NodeGraph graph;
    graph.scriptFileName = "move.node.dyd";
    graph.nodes = {
        {"n1", NodeKind::OnUpdate, 0, 0},
        {"n2", NodeKind::Branch, 120, 0},
        {"n3", NodeKind::Constant, 0, 80, 0, true, true},
        {"n4", NodeKind::SetPosition, 240, 0},
        {"n5", NodeKind::Constant, 120, 80, 7.0},
        {"n6", NodeKind::Constant, 180, 80, 8.0},
        {"n7", NodeKind::Constant, 240, 80, 9.0},
    };
    graph.edges = {
        {"n1.out", "n2.in"},
        {"n3.value", "n2.condition"},
        {"n2.then", "n4.in"},
        {"n5.value", "n4.x"},
        {"n6.value", "n4.y"},
        {"n7.value", "n4.z"},
    };
    return graph;
}

TEST_CASE("Node emitter is deterministic", "[dryad][nodes]") {
    const NodeGraph graph = sampleMoveGraph();
    const EmitResult a = NodeEmitter::emit(graph);
    const EmitResult b = NodeEmitter::emit(graph);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    REQUIRE(a.dyd == b.dyd);
    REQUIRE(a.nodeJson == b.nodeJson);
    REQUIRE(a.dyd.find("// @node n2") != std::string::npos);
    REQUIRE(a.dyd.find("if (true)") != std::string::npos);
    REQUIRE(a.dyd.find("&&") == std::string::npos);
}

TEST_CASE("Emitted Dryad runs in DryadHost", "[dryad][nodes]") {
    const std::string scriptPath = "emitter_move.dyd";
    const std::string jsonPath = "emitter_move.node.json";
    NodeGraph graph = sampleMoveGraph();
    graph.scriptFileName = "emitter_move.dyd";

    std::string writeErr;
    REQUIRE(NodeEmitter::writeSiblingFiles(scriptPath, graph, &writeErr));

    ECS::World world;
    ECS::Entity entity = world.create("EmitterEntity");
    auto& transform = entity.getOrAdd<ECS::Transform>();
    transform.position = Vec3(1, 2, 3);

    DryadHost host;
    host.setWorld(&world);
    REQUIRE(host.load(scriptPath));

    REQUIRE(host.call(scriptPath, "onUpdate", entity, 0.016f));
    REQUIRE(transform.position.x == 7.0f);
    REQUIRE(transform.position.y == 8.0f);
    REQUIRE(transform.position.z == 9.0f);

    std::ifstream jsonIn(jsonPath);
    REQUIRE(jsonIn.good());
    std::string json((std::istreambuf_iterator<char>(jsonIn)), std::istreambuf_iterator<char>());
    REQUIRE(json.find("\"type\": \"OnUpdate\"") != std::string::npos);
    REQUIRE(json.find("emitter_move.dyd") != std::string::npos);

    std::remove(scriptPath.c_str());
    std::remove(jsonPath.c_str());
}

TEST_CASE("Round-trip from @node markers reproduces emitted .dyd", "[dryad][nodes]") {
    NodeGraph graph = sampleMoveGraph();
    graph.scriptFileName = "roundtrip.dyd";
    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);

    const ImportResult imported = importFromDyd(emitted.dyd, graph.scriptFileName, &graph);
    REQUIRE(imported.status == ImportStatus::Success);

    const EmitResult again = NodeEmitter::emit(imported.graph);
    REQUIRE(again.ok);
    REQUIRE(again.dyd == emitted.dyd);
}

TEST_CASE("Import without markers is a no-op signal", "[dryad][nodes]") {
    const ImportResult imported = importFromDyd("function onUpdate(entity, dt) { }\n", "plain.dyd");
    REQUIRE(imported.status == ImportStatus::NoMarkers);
}

TEST_CASE("GetPosition constant add runs", "[dryad][nodes]") {
    NodeGraph graph;
    graph.scriptFileName = "offset.dyd";
    graph.nodes = {
        {"e1", NodeKind::OnUpdate, 0, 0},
        {"e2", NodeKind::SetPosition, 100, 0},
        {"e3", NodeKind::GetPosition, 0, 60},
        {"e4", NodeKind::Constant, 60, 60, 1.0},
    };
    graph.edges = {
        {"e1.out", "e2.in"},
        {"e3.x", "e2.x"},
        {"e4.value", "e2.y"},
        {"e3.z", "e2.z"},
    };

    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);
    REQUIRE(emitted.dyd.find("caffeine_get_position_x(entity)") != std::string::npos);

    const std::string path = "offset.dyd";
    std::ofstream out(path);
    out << emitted.dyd;
    out.close();

    ECS::World world;
    ECS::Entity entity = world.create("Offset");
    auto& transform = entity.getOrAdd<ECS::Transform>();
    transform.position = Vec3(4, 5, 6);

    DryadHost host;
    host.setWorld(&world);
    REQUIRE(host.load(path));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));
    REQUIRE(transform.position.x == 4.0f);
    REQUIRE(transform.position.y == 1.0f);
    REQUIRE(transform.position.z == 6.0f);

    std::remove(path.c_str());
}

TEST_CASE("Translate node emits and executes", "[dryad][nodes][3d]") {
    NodeGraph graph;
    graph.scriptFileName = "translate.dyd";
    graph.nodes = {
        {"t1", NodeKind::OnUpdate, 0, 0},
        {"t2", NodeKind::Translate, 100, 0},
        {"t3", NodeKind::Constant, 0, 60, 0.5},
        {"t4", NodeKind::Constant, 60, 60, 1.0},
        {"t5", NodeKind::Constant, 120, 60, 0.0},
    };
    graph.edges = {
        {"t1.out", "t2.in"},
        {"t3.value", "t2.x"},
        {"t4.value", "t2.y"},
        {"t5.value", "t2.z"},
    };

    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);
    REQUIRE(emitted.dyd.find("caffeine_translate(entity, 0.5, 1, 0);") != std::string::npos);

    const ImportResult imported = importFromDyd(emitted.dyd, graph.scriptFileName, &graph);
    REQUIRE(imported.status == ImportStatus::Success);
    const EmitResult again = NodeEmitter::emit(imported.graph);
    REQUIRE(again.ok);
    REQUIRE(again.dyd == emitted.dyd);

    const std::string path = "translate.dyd";
    {
        std::ofstream out(path);
        out << emitted.dyd;
    }

    ECS::World world;
    ECS::Entity entity = world.create("Translate3D");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(1, 0, 0);
    entity.getOrAdd<ECS::MeshFilterComponent>();

    DryadHost host;
    host.setWorld(&world);
    REQUIRE(host.load(path));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));
    const auto* position = entity.get<ECS::Position3D>();
    REQUIRE(position != nullptr);
    REQUIRE(position->position.x == 1.5f);
    REQUIRE(position->position.y == 1.0f);
    REQUIRE(position->position.z == 0.0f);

    std::remove(path.c_str());
}

TEST_CASE("DeltaTime wires into Translate and round-trips", "[dryad][nodes]") {
    NodeGraph graph;
    graph.scriptFileName = "motion.dyd";
    graph.nodes = {
        {"m1", NodeKind::OnUpdate, 0, 0},
        {"m2", NodeKind::Translate, 200, 0},
        {"m3", NodeKind::DeltaTime, 40, 100},
    };
    graph.edges = {
        {"m1.out", "m2.in"},
        {"m3.value", "m2.y"},
    };

    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);
    REQUIRE(emitted.dyd.find("caffeine_translate(entity, 0, dt, 0);") != std::string::npos);

    const ImportResult imported = importFromDyd(emitted.dyd, graph.scriptFileName, &graph);
    REQUIRE(imported.status == ImportStatus::Success);
    int deltaTimeNodes = 0;
    for (const auto& node : imported.graph.nodes) {
        if (node.kind == NodeKind::DeltaTime) ++deltaTimeNodes;
    }
    REQUIRE(deltaTimeNodes == 1);

    const EmitResult again = NodeEmitter::emit(imported.graph);
    REQUIRE(again.ok);
    REQUIRE(again.dyd == emitted.dyd);
}


TEST_CASE("Unconnected vector inputs use inline canvas defaults", "[dryad][nodes]") {
    NodeGraph graph;
    graph.scriptFileName = "inline.dyd";
    GraphNode onUpdate{"i1", NodeKind::OnUpdate, 0, 0};
    GraphNode setPosition{"i2", NodeKind::SetPosition, 200, 0};
    setPosition.inputX = 1.25;
    setPosition.inputY = -2.5;
    setPosition.inputZ = 4.0;
    graph.nodes = {onUpdate, setPosition};
    graph.edges = {{"i1.out", "i2.in"}};

    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);
    REQUIRE(emitted.dyd.find("caffeine_set_position(entity, 1.25, -2.5, 4);") !=
            std::string::npos);

    const std::string path = "inline.node.json";
    const std::string dydPath = "inline.dyd";
    {
        std::ofstream out(dydPath);
        out << emitted.dyd;
    }
    {
        std::ofstream out(path);
        out << emitted.nodeJson;
    }
    const LoadResult loaded = loadFromJsonFile(path);
    INFO("json load error: " << loaded.error);
    REQUIRE(loaded.ok);
    const GraphNode* restored = loaded.graph.findNode("i2");
    REQUIRE(restored != nullptr);
    REQUIRE(restored->x == Approx(200.0f));
    REQUIRE(restored->y == Approx(0.0f));
    REQUIRE(restored->inputX == Approx(1.25));
    REQUIRE(restored->inputY == Approx(-2.5));
    REQUIRE(restored->inputZ == Approx(4.0));

    std::remove(path.c_str());
    std::remove(dydPath.c_str());
}

TEST_CASE("Unconnected Branch condition uses its inline default", "[dryad][nodes]") {
    NodeGraph graph;
    graph.scriptFileName = "inline_branch.dyd";
    GraphNode onUpdate{"b1", NodeKind::OnUpdate, 0, 0};
    GraphNode branch{"b2", NodeKind::Branch, 200, 0};
    branch.inputCondition = true;
    GraphNode setPosition{"b3", NodeKind::SetPosition, 400, 0};
    setPosition.inputX = 7.0;
    graph.nodes = {onUpdate, branch, setPosition};
    graph.edges = {{"b1.out", "b2.in"}, {"b2.then", "b3.in"}};

    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);
    REQUIRE(emitted.dyd.find("if (true)") != std::string::npos);
    REQUIRE(emitted.dyd.find("caffeine_set_position(entity, 7, 0, 0);") != std::string::npos);
}


TEST_CASE("Node JSON reloads constant values and bool mode", "[dryad][nodes]") {
    NodeGraph graph;
    graph.scriptFileName = "constants.dyd";
    GraphNode onUpdate{"c1", NodeKind::OnUpdate, 12, 34};
    GraphNode number{"c2", NodeKind::Constant, 120, 220};
    number.numberValue = 3.5;
    GraphNode boolean{"c3", NodeKind::Constant, 240, 340};
    boolean.constantIsBool = true;
    boolean.boolValue = true;
    graph.nodes = {onUpdate, number, boolean};

    const EmitResult emitted = NodeEmitter::emit(graph);
    REQUIRE(emitted.ok);
    const LoadResult loaded = loadFromJsonString(emitted.nodeJson);
    REQUIRE(loaded.ok);

    const GraphNode* restoredOnUpdate = loaded.graph.findNode("c1");
    const GraphNode* restoredNumber = loaded.graph.findNode("c2");
    const GraphNode* restoredBool = loaded.graph.findNode("c3");
    REQUIRE(restoredOnUpdate != nullptr);
    REQUIRE(restoredNumber != nullptr);
    REQUIRE(restoredBool != nullptr);
    REQUIRE(restoredOnUpdate->x == Approx(12.0f));
    REQUIRE(restoredOnUpdate->y == Approx(34.0f));
    REQUIRE(restoredNumber->numberValue == Approx(3.5));
    REQUIRE(restoredBool->constantIsBool);
    REQUIRE(restoredBool->boolValue);
}

#endif
