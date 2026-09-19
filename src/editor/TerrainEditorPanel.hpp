#pragma once

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
struct ImNodesEditorContext;
#endif

#include <vector>

namespace Caffeine::Editor {

enum class TerrainGraphNodeType : u8 {
    Noise = 0,
    Ridged,
    Hybrid,
    DiamondSquare,
    SpectralFFT,
    Thermal,
    MicroSculpt,
    Rivers,
    GeoSim,
    Smooth,
    Output,
};

struct TerrainGraphNode {
    int id = 0;
    TerrainGraphNodeType type = TerrainGraphNodeType::Noise;
    float posX = 0.0f;
    float posY = 48.0f;
};

struct TerrainGraphLink {
    int id = 0;
    int fromAttr = 0;
    int toAttr = 0;
};

class TerrainEditorPanel {
public:
    TerrainEditorPanel();
    ~TerrainEditorPanel();

    void render(ECS::World& world, EditorContext& ctx);

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open() { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void drawGeneration(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
    void drawEditTools(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
    void drawDataFile(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
    void drawGenerationGraph(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
    void ensureDefaultGraph();
    void layoutGraphNodes();
    void syncGraphFromStyle(ECS::TerrainGenStyle style);
    void applyGraphToSettings(ECS::TerrainGenerationSettings& gen) const;
    void generateFromGraph(ECS::World& world, ECS::Entity entity, ECS::TerrainComponent& terrain,
                           EditorContext& ctx);
#endif

    bool m_open = true;
#ifdef CF_HAS_IMGUI
    ImNodesEditorContext* m_editorContext = nullptr;
    std::vector<TerrainGraphNode> m_graphNodes;
    std::vector<TerrainGraphLink> m_graphLinks;
    int m_nextNodeId = 1;
    int m_nextLinkId = 1;
    int m_selectedNodeId = 0;
    bool m_graphLayoutDirty = true;
#endif
};

}  // namespace Caffeine::Editor
