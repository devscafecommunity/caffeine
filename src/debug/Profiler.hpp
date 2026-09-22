#pragma once

#include "../core/Types.hpp"
#include "../core/Timer.hpp"
#include "../containers/Vector.hpp"

namespace Caffeine::Debug {

class Profiler {
public:
    static Profiler& instance();

    void beginScope(const char* name);
    void endScope(const char* name);

    static constexpr u32 kInvalidNode = static_cast<u32>(-1);

    struct ScopeStats {
        const char* name = nullptr;
        u32 nodeIndex = kInvalidNode;
        u32 parentIndex = kInvalidNode;
        u32 firstChildIndex = kInvalidNode;
        u32 nextSiblingIndex = kInvalidNode;
        u32 depth = 0;
        u64  callCount = 0;
        f64  totalMs   = 0.0;  // inclusive (scope + children)
        f64  selfMs    = 0.0;  // exclusive (scope only)
        f64  avgMs     = 0.0;
        f64  avgSelfMs = 0.0;
        f64  minMs     = 0.0;
        f64  maxMs     = 0.0;
    };

    // Fills out[0..nodeCount()-1]; index 0 is the virtual root (no samples).
    void report(Vector<ScopeStats>& out) const;

    void reset();

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    usize scopeCount() const;

private:
    Profiler();
    ~Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    struct ScopeNode {
        u32 parent = kInvalidNode;
        u32 firstChild = kInvalidNode;
        u32 nextSibling = kInvalidNode;
        const char* name = nullptr;
        u64  callCount = 0;
        f64  totalMs = 0.0;
        f64  selfMs = 0.0;
        f64  minMs = 1e18;
        f64  maxMs = 0.0;
    };

    struct ActiveScope {
        u32 nodeIndex = kInvalidNode;
        f64 childrenMs = 0.0;
        Core::Timer timer;
    };

    static constexpr usize MAX_SCOPE_NODES = 512;
    static constexpr usize MAX_SCOPE_DEPTH = 64;

    bool m_enabled = true;

    ScopeNode m_nodes[MAX_SCOPE_NODES]{};
    usize m_nodeCount = 0;
    ActiveScope m_activeStack[MAX_SCOPE_DEPTH]{};
    usize m_activeDepth = 0;

    u32 findOrCreateChild(u32 parentIndex, const char* name);
    void initRoot();
};

class ProfileScope {
public:
    explicit ProfileScope(const char* name);
    ~ProfileScope();

private:
    const char* m_name;
};

} // namespace Caffeine::Debug

#define CF_PROFILE_SCOPE(name) \
    Caffeine::Debug::ProfileScope _cfProfileScope_##__LINE__(name)
