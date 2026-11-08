#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <string>

#ifdef CF_HAS_DRYAD
#include "script/DryadHost.hpp"
#include "script/nodes/NodeGraph.hpp"
#endif

struct ImNodesEditorContext;

namespace Caffeine::Editor {

class EditorContext;

/// Visual node canvas for Dryad scripts (.dyd + sibling .node.json). Play mode
/// runs the generated .dyd through ScriptSystem; this panel can also attach the
/// script to the selected entity and run a one-shot preview.
class DryadNodeScriptPanel {
public:
    DryadNodeScriptPanel();
    ~DryadNodeScriptPanel();

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void openFromScript(const std::filesystem::path& scriptPath);
    const std::filesystem::path& scriptPath() const { return m_scriptPath; }

    void onImGuiRender(EditorContext& ctx);

private:
    bool m_open = false;
    std::filesystem::path m_scriptPath;
    std::string m_status;
    int m_nextNodeSerial = 2;
    int m_nextLinkId = 1;
    bool m_pendingGraphSync = true;
    bool m_resetView = true;
    bool m_deleteRequested = false;
    bool m_addNodeAtMouse = false;
    f32 m_addNodeScreenX = 0.0f;
    f32 m_addNodeScreenY = 0.0f;

    ImNodesEditorContext* m_nodesContext = nullptr;
    bool m_ownsImNodesRootContext = false;

    void ensureImNodesReady();

#ifdef CF_HAS_DRYAD
    Script::Nodes::NodeGraph m_graph;
    Script::DryadHost m_previewHost;
#endif

    void rebuildImNodesFromGraph();
    bool syncGraphFromEditor(bool writeFiles);
    bool commitToDisk();
    void addNode(const char* typeName);
    void addNodeAt(const char* typeName, f32 canvasX, f32 canvasY);
    void addNodePopupItems();
    void deleteSelection();
    void arrangeGraph();
    void screenToCanvas(f32 screenX, f32 screenY, f32& canvasX, f32& canvasY) const;
    void syncFromDydSource();
    void drawRuntimeControls(EditorContext& ctx);
    void addMotionExample();
};

} // namespace Caffeine::Editor
