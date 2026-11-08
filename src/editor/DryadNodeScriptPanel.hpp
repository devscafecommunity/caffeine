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

    ImNodesEditorContext* m_nodesContext = nullptr;
    bool m_ownsImNodesRootContext = false;

    void ensureImNodesReady();

#ifdef CF_HAS_DRYAD
    Script::Nodes::NodeGraph m_graph;
    Script::DryadHost m_previewHost;
    bool m_previewLoaded = false;
#endif

    void rebuildImNodesFromGraph();
    bool syncGraphFromEditor(bool writeFiles);
    bool commitToDisk();
    void addNode(const char* typeName);
    void syncFromDydSource();
    void drawRuntimeControls(EditorContext& ctx);
    void addMotionExample();
    std::string runtimePathForComponent(const EditorContext& ctx) const;
};

} // namespace Caffeine::Editor
