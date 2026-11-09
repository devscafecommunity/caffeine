#include "Profiler.hpp"
#include <cstring>
#include <algorithm>

namespace Caffeine::Debug {

Profiler::Profiler() {
    initRoot();
}

Profiler& Profiler::instance() {
    static Profiler s;
    return s;
}

void Profiler::initRoot() {
    m_nodeCount = 1;
    m_activeDepth = 0;
    m_nodes[0] = ScopeNode{};
    m_nodes[0].name = "<root>";
    m_nodes[0].parent = kInvalidNode;
}

void Profiler::beginScope(const char* name) {
    if (!m_enabled || !name) return;
    if (m_activeDepth >= MAX_SCOPE_DEPTH) return;

    const u32 parentIndex =
        (m_activeDepth > 0) ? m_activeStack[m_activeDepth - 1].nodeIndex : 0u;
    const u32 nodeIndex = findOrCreateChild(parentIndex, name);
    if (nodeIndex == kInvalidNode) return;

    ActiveScope& active = m_activeStack[m_activeDepth++];
    active.nodeIndex = nodeIndex;
    active.childrenMs = 0.0;
    active.timer.reset();
    active.timer.start();
}

void Profiler::endScope(const char* name) {
    if (!m_enabled) return;
    if (m_activeDepth == 0) return;

    ActiveScope& active = m_activeStack[m_activeDepth - 1];
    ScopeNode& node = m_nodes[active.nodeIndex];

    if (name && node.name && std::strcmp(node.name, name) != 0) {
        // RAII scopes should always match; still close the innermost scope.
    }

    active.timer.stop();
    const f64 elapsedMs = active.timer.elapsed().millis();
    const f64 selfMs = std::max(0.0, elapsedMs - active.childrenMs);

    node.callCount++;
    node.totalMs += elapsedMs;
    node.selfMs += selfMs;
    if (elapsedMs < node.minMs) node.minMs = elapsedMs;
    if (elapsedMs > node.maxMs) node.maxMs = elapsedMs;

    if (m_activeDepth > 1) {
        m_activeStack[m_activeDepth - 2].childrenMs += elapsedMs;
    }

    m_activeDepth--;
}

void Profiler::report(Vector<ScopeStats>& out) const {
    out.clear();
    out.resize(m_nodeCount);

    for (usize i = 0; i < m_nodeCount; ++i) {
        const ScopeNode& node = m_nodes[i];
        ScopeStats& stats = out[i];
        stats.name = node.name;
        stats.nodeIndex = static_cast<u32>(i);
        stats.parentIndex = node.parent;
        stats.firstChildIndex = node.firstChild;
        stats.nextSiblingIndex = node.nextSibling;
        stats.callCount = node.callCount;
        stats.totalMs = node.totalMs;
        stats.selfMs = node.selfMs;
        stats.avgMs = (node.callCount > 0) ? node.totalMs / static_cast<f64>(node.callCount) : 0.0;
        stats.avgSelfMs =
            (node.callCount > 0) ? node.selfMs / static_cast<f64>(node.callCount) : 0.0;
        stats.minMs = (node.callCount > 0) ? node.minMs : 0.0;
        stats.maxMs = node.maxMs;

        stats.depth = 0;
        u32 parent = node.parent;
        while (parent != kInvalidNode && parent < m_nodeCount) {
            stats.depth++;
            parent = m_nodes[parent].parent;
        }
    }
}

void Profiler::reset() {
    initRoot();
}

usize Profiler::scopeCount() const {
    return (m_nodeCount > 0) ? m_nodeCount - 1 : 0;
}

u32 Profiler::findOrCreateChild(u32 parentIndex, const char* name) {
    if (parentIndex >= m_nodeCount) return kInvalidNode;

    u32 child = m_nodes[parentIndex].firstChild;
    while (child != kInvalidNode) {
        if (m_nodes[child].name == name
            || (m_nodes[child].name && std::strcmp(m_nodes[child].name, name) == 0)) {
            return child;
        }
        child = m_nodes[child].nextSibling;
    }

    if (m_nodeCount >= MAX_SCOPE_NODES) return kInvalidNode;

    const u32 index = static_cast<u32>(m_nodeCount++);
    ScopeNode& node = m_nodes[index];
    node = ScopeNode{};
    node.parent = parentIndex;
    node.name = name;
    node.nextSibling = m_nodes[parentIndex].firstChild;
    m_nodes[parentIndex].firstChild = index;
    return index;
}

ProfileScope::ProfileScope(const char* name) : m_name(name) {
    Profiler::instance().beginScope(m_name);
}

ProfileScope::~ProfileScope() {
    Profiler::instance().endScope(m_name);
}

} // namespace Caffeine::Debug
