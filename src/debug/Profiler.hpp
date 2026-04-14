#pragma once

#include "../core/Types.hpp"
#include "../core/Timer.hpp"
#include "../containers/HashMap.hpp"
#include "../containers/Vector.hpp"

namespace Caffeine::Debug {

class Profiler {
public:
    static Profiler& instance();

    void beginScope(const char* name);
    void endScope(const char* name);

    struct ScopeStats {
        const char* name = nullptr;
        u64  callCount = 0;
        f64  totalMs   = 0.0;
        f64  avgMs     = 0.0;
        f64  minMs     = 1e18;
        f64  maxMs     = 0.0;
    };

    void report(Vector<ScopeStats>& out) const;
    void reset();

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    usize scopeCount() const;

private:
    Profiler() = default;
    ~Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    struct InternalScopeData {
        const char* name = nullptr;
        u64  callCount = 0;
        f64  totalMs   = 0.0;
        f64  minMs     = 1e18;
        f64  maxMs     = 0.0;
        Core::Timer activeTimer;
    };

    bool m_enabled = true;

    static constexpr usize MAX_SCOPES = 256;
    InternalScopeData m_scopes[MAX_SCOPES]{};
    usize m_scopeCount = 0;

    InternalScopeData* findScope(const char* name);
    const InternalScopeData* findScope(const char* name) const;
    InternalScopeData* findOrCreateScope(const char* name);
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
