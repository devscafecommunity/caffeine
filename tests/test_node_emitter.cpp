#include "catch.hpp"

#ifdef CF_HAS_DRYAD

#include "../src/script/DryadHost.hpp"
#include "../src/script/nodes/NodeEmitter.hpp"
#include "../src/script/nodes/NodeRoundTrip.hpp"
#include "../src/ecs/Components.hpp"
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

#endif
