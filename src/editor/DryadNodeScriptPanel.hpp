#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <string>

#ifdef CF_HAS_DRYAD
#include "script/nodes/NodeGraph.hpp"
#endif

struct ImNodesEditorContext;

namespace Caffeine::Editor {

class EditorContext;

/// Visual node canvas for Dryad scripts (.dyd + sibling .node.json). Does not run the graph in play mode.
class DryadNodeScriptPanel {
public:
    DryadNodeScriptPanel();
    ~DryadNodeScriptPanel();

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void openFromScript(const std::filesystem::path& scriptPath);

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
#endif

    void rebuildImNodesFromGraph();
    bool syncGraphFromEditor(bool writeFiles);
    bool commitToDisk();
    void addNode(const char* typeName);
    void syncFromDydSource();
};

} // namespace Caffeine::Editor
