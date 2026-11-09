#pragma once

namespace Caffeine::Editor {

/// Searchable node library. Nodes are grouped by category and can be dragged
/// onto any NodeCanvas that accepts the "DRYAD_NODE" payload.
class DryadNodesPanel {
public:
    void render();

    void open() { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

private:
    bool m_open = true;
    char m_search[128] = {};
};

} // namespace Caffeine::Editor
