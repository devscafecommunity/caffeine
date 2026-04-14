#include "Profiler.hpp"
#include <cstring>
#include <algorithm>

namespace Caffeine::Debug {

Profiler& Profiler::instance() {
    static Profiler s;
    return s;
}

void Profiler::beginScope(const char* name) {
    if (!m_enabled) return;

    InternalScopeData* scope = findOrCreateScope(name);
    if (!scope) return;

    scope->activeTimer.reset();
    scope->activeTimer.start();
}

void Profiler::endScope(const char* name) {
    if (!m_enabled) return;

    InternalScopeData* scope = findScope(name);
    if (!scope) return;

    scope->activeTimer.stop();
    f64 ms = scope->activeTimer.elapsed().millis();

    scope->callCount++;
    scope->totalMs += ms;
    if (ms < scope->minMs) scope->minMs = ms;
    if (ms > scope->maxMs) scope->maxMs = ms;
}

void Profiler::report(Vector<ScopeStats>& out) const {
    out.clear();
    for (usize i = 0; i < m_scopeCount; ++i) {
        const auto& s = m_scopes[i];
        ScopeStats stats;
        stats.name = s.name;
        stats.callCount = s.callCount;
        stats.totalMs = s.totalMs;
        stats.avgMs = (s.callCount > 0) ? s.totalMs / static_cast<f64>(s.callCount) : 0.0;
        stats.minMs = s.minMs;
        stats.maxMs = s.maxMs;
        out.pushBack(stats);
    }
}

void Profiler::reset() {
    m_scopeCount = 0;
    for (usize i = 0; i < MAX_SCOPES; ++i) {
        m_scopes[i] = InternalScopeData{};
    }
}

usize Profiler::scopeCount() const {
    return m_scopeCount;
}

Profiler::InternalScopeData* Profiler::findScope(const char* name) {
    for (usize i = 0; i < m_scopeCount; ++i) {
        if (strcmp(m_scopes[i].name, name) == 0) {
            return &m_scopes[i];
        }
    }
    return nullptr;
}

const Profiler::InternalScopeData* Profiler::findScope(const char* name) const {
    for (usize i = 0; i < m_scopeCount; ++i) {
        if (strcmp(m_scopes[i].name, name) == 0) {
            return &m_scopes[i];
        }
    }
    return nullptr;
}

Profiler::InternalScopeData* Profiler::findOrCreateScope(const char* name) {
    InternalScopeData* existing = findScope(name);
    if (existing) return existing;

    if (m_scopeCount >= MAX_SCOPES) return nullptr;

    auto& scope = m_scopes[m_scopeCount];
    scope.name = name;
    scope.callCount = 0;
    scope.totalMs = 0.0;
    scope.minMs = 1e18;
    scope.maxMs = 0.0;
    ++m_scopeCount;
    return &scope;
}

ProfileScope::ProfileScope(const char* name) : m_name(name) {
    Profiler::instance().beginScope(m_name);
}

ProfileScope::~ProfileScope() {
    Profiler::instance().endScope(m_name);
}

} // namespace Caffeine::Debug
