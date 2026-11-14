#pragma once
#include "caffeine/ui/UI.hpp"
#include "core/Types.hpp"
#include "debug/Profiler.hpp"
#include "debug/SystemMetrics.hpp"
#include "containers/Vector.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class ProfilerWindow {
public:
    ProfilerWindow() = default;

    void pushFrameTime(f32 ms) {
        if (m_paused) return;
        m_frameTimes[m_frameIdx % 120] = ms;
        ++m_frameIdx;
    }

    f32 lastFrameTime() const {
        if (m_frameIdx == 0) return 0.0f;
        return m_frameTimes[(m_frameIdx - 1) % 120];
    }

    void pause()  { m_paused = true; }
    void resume() { m_paused = false; }
    bool isPaused() const { return m_paused; }
    bool isOpen()   const { return m_open; }
    void close()    { m_open = false; }
    void open()     { m_open = true; }

    const std::array<f32, 120>& frameTimes() const { return m_frameTimes; }
    u32 frameIndex() const { return m_frameIdx; }

#ifdef CF_HAS_IMGUI
    void render(const Debug::Profiler& profiler) {
        if (!m_open) return;

        if (m_forceScopeRefresh || (ImGui::GetFrameCount() % 15u) == 0u) {
            profiler.report(m_cachedScopes);
            m_forceScopeRefresh = false;
        }
        if (m_expandAllRequested) {
            m_expandAllRequested = false;
            m_expandedScopes.clear();
            for (usize i = 0; i < m_cachedScopes.size(); ++i) {
                if (m_cachedScopes[i].firstChildIndex != Debug::Profiler::kInvalidNode) {
                    m_expandedScopes.insert(static_cast<u32>(i));
                }
            }
        }

        using namespace Caffeine::UIKit;
        using namespace Caffeine::UIKit::Elements;

        Panel root("Profiler", "Frame and scope timings", "bell", &m_open);
        UIContext ui;

        const f32 lastMs = lastFrameTime();
        const f32 fps = lastMs > 0.0f ? 1000.0f / lastMs : 0.0f;

        auto& actions = root.emplace<Row>("profiler_actions");
        auto& pause = actions.emplace<Button>(m_paused ? "Resume" : "Pause");
        pause.setWidth(88.0f);
        pause.setTooltip("Pause or resume frame sampling");
        pause.setOnClick([this] { m_paused = !m_paused; });
        auto& reset = actions.emplace<Button>("Reset Stats");
        reset.setWidth(104.0f);
        reset.setTooltip("Clear profiler counters");
        reset.setOnClick([this] {
            Debug::Profiler::instance().reset();
            m_forceScopeRefresh = true;
        });

        auto& scopeActions = root.emplace<Row>("profiler_scope_actions");
        auto& expand = scopeActions.emplace<Button>("Expand all");
        expand.setWidth(84.0f);
        expand.setOnClick([this] { m_expandAllRequested = true; });
        auto& collapse = scopeActions.emplace<Button>("Collapse all");
        collapse.setWidth(92.0f);
        collapse.setOnClick([this] { m_expandedScopes.clear(); });

        auto& stats = root.emplace<Card>("profiler_stats");
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "Frame: %.2f ms   FPS: %.1f", lastMs, fps);
        stats.emplace<Label>(buffer, fps >= 55.0f ? TextTone::Success
                               : fps >= 30.0f ? TextTone::Warning
                                              : TextTone::Danger);
        auto& graph = stats.emplace<Canvas>("profiler_frame_graph");
        graph.setHeight(84.0f);
        graph.setOnDraw([this](UIContext&, ImDrawList* drawList, const ImVec2& origin,
                               const ImVec2& size) {
            drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                    IM_COL32(18, 20, 26, 255), 4.0f);
            f32 maxVal = 33.3f;
            for (f32 sample : m_frameTimes) maxVal = std::max(maxVal, sample);
            maxVal = std::min(maxVal * 1.15f, 200.0f);
            const float budgetY =
                origin.y + size.y * (1.0f - std::min(16.7f / maxVal, 1.0f));
            drawList->AddLine(ImVec2(origin.x, budgetY),
                              ImVec2(origin.x + size.x, budgetY),
                              IM_COL32(120, 200, 140, 130), 1.0f);

            ImVec2 previous;
            bool havePrevious = false;
            for (int i = 0; i < 120; ++i) {
                const f32 sample = m_frameTimes[(m_frameIdx + 1 + static_cast<u32>(i)) % 120];
                const float x = origin.x + size.x * static_cast<float>(i) / 119.0f;
                const float y = origin.y + size.y -
                                std::min(sample / maxVal, 1.0f) * size.y;
                const ImVec2 point(x, y);
                if (havePrevious) {
                    drawList->AddLine(previous, point, IM_COL32(225, 130, 170, 220), 1.5f);
                }
                previous = point;
                havePrevious = true;
            }
            char label[64];
            std::snprintf(label, sizeof(label), "max %.1f ms", maxVal);
            drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f),
                              IM_COL32(170, 175, 190, 220), label);
        });
        stats.emplace<Label>("Budget: 16.7 ms (60 FPS)", TextTone::Muted);

        const Debug::SystemMetrics sys = Debug::sampleSystemMetrics();
        auto& hardware = root.emplace<Card>("profiler_hardware");
        hardware.emplace<Label>("Hardware", TextTone::Accent).setHeading(true);
        std::snprintf(buffer, sizeof(buffer), "%.1f%%", sys.processCpuPercent);
        hardware.emplace<KeyValue>("CPU (process)", buffer);
        if (sys.gpuAvailable && sys.gpuBusyPercent >= 0.0f) {
            std::snprintf(buffer, sizeof(buffer), "%.1f%%", sys.gpuBusyPercent);
            hardware.emplace<KeyValue>("GPU", buffer);
        } else {
            hardware.emplace<KeyValue>("GPU", "n/a");
        }
        if (sys.ramTotalMiB > 0.0f) {
            std::snprintf(buffer, sizeof(buffer), "%.0f / %.0f MiB", sys.ramUsedMiB,
                          sys.ramTotalMiB);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.0f MiB", sys.ramUsedMiB);
        }
        hardware.emplace<KeyValue>("RAM", buffer);
        if (sys.vramTotalMiB > 0.0f) {
            std::snprintf(buffer, sizeof(buffer), "%.0f / %.0f MiB", sys.vramUsedMiB,
                          sys.vramTotalMiB);
            hardware.emplace<KeyValue>("VRAM", buffer);
        }

        auto& scopeSection = root.emplace<Elements::Section>("Scope tree");
        scopeSection.emplace<Elements::Paragraph>(
            "Hierarchical call stack - self ms = exclusive, total ms = inclusive. "
            "Instrument with CF_PROFILE_SCOPE.");
        scopeSection.emplace<Checkbox>("Hide empty scopes", &m_hideEmptyScopes);

        const Vector<Debug::Profiler::ScopeStats>& scopes = m_cachedScopes;
        usize sampledScopes = 0;
        for (usize i = 1; i < scopes.size(); ++i) {
            if (scopes[i].callCount > 0) ++sampledScopes;
        }

        if (sampledScopes == 0) {
            scopeSection.emplace<Label>(
                "No scope samples yet. Instrument hot paths with CF_PROFILE_SCOPE.",
                TextTone::Muted)
                .setWrap(true);
        } else {
            auto& table = scopeSection.emplace<DataTable>("profiler_scopes");
            table.addColumn("Scope", 0.0f, true);
            table.addColumn("self ms", 70.0f, false);
            table.addColumn("total ms", 72.0f, false);
            table.addColumn("% frame", 64.0f, false);
            table.addColumn("% parent", 68.0f, false);
            table.addColumn("max ms", 64.0f, false);
            table.addColumn("calls", 54.0f, false);
            table.setHeight(300.0f);
            table.setSelectable(false);
            table.setOnHeaderClick([this](int column) {
                if (m_sortColumn == column) {
                    m_sortAscending = !m_sortAscending;
                } else {
                    m_sortColumn = column;
                    m_sortAscending = (column == 0);
                }
            });
            const f64 frameBudgetMs = lastMs > 0.0f ? static_cast<f64>(lastMs) : 16.7;
            buildScopeRows(table, scopes, 0u, 0, frameBudgetMs, 0.0);
        }

        ui.draw(root);
    }

private:
    bool hasVisibleChildren(const Vector<Debug::Profiler::ScopeStats>& scopes,
                            u32 nodeIndex) const {
        if (nodeIndex >= scopes.size()) return false;
        for (u32 child = scopes[nodeIndex].firstChildIndex;
             child != Debug::Profiler::kInvalidNode && child < scopes.size();
             child = scopes[child].nextSiblingIndex) {
            if (!m_hideEmptyScopes || scopes[child].callCount > 0) return true;
        }
        return false;
    }

    std::vector<u32> sortedChildren(const Vector<Debug::Profiler::ScopeStats>& scopes,
                                    u32 parentIndex) const {
        std::vector<u32> children;
        if (parentIndex >= scopes.size()) return children;
        for (u32 child = scopes[parentIndex].firstChildIndex;
             child != Debug::Profiler::kInvalidNode && child < scopes.size();
             child = scopes[child].nextSiblingIndex) {
            if (!m_hideEmptyScopes || scopes[child].callCount > 0) children.push_back(child);
        }

        auto metric = [this](const Debug::Profiler::ScopeStats& scope) -> f64 {
            switch (m_sortColumn) {
                case 1: return scope.avgSelfMs;
                case 2: return scope.avgMs;
                case 5: return scope.maxMs;
                case 6: return static_cast<f64>(scope.callCount);
                default: return scope.avgSelfMs;
            }
        };

        std::sort(children.begin(), children.end(), [&](u32 a, u32 b) {
            if (m_sortColumn == 0) {
                const char* an = scopes[a].name ? scopes[a].name : "";
                const char* bn = scopes[b].name ? scopes[b].name : "";
                const int cmp = std::strcmp(an, bn);
                return m_sortAscending ? cmp < 0 : cmp > 0;
            }
            const f64 va = metric(scopes[a]);
            const f64 vb = metric(scopes[b]);
            if (va != vb) return m_sortAscending ? va < vb : va > vb;
            return scopes[a].avgMs > scopes[b].avgMs;
        });
        return children;
    }

    void buildScopeRows(Caffeine::UIKit::Elements::DataTable& table,
                        const Vector<Debug::Profiler::ScopeStats>& scopes,
                        u32 parentIndex, int depth, f64 frameBudgetMs,
                        f64 parentTotalMs) {
        for (u32 nodeIndex : sortedChildren(scopes, parentIndex)) {
            const Debug::Profiler::ScopeStats& scope = scopes[nodeIndex];
            const bool expandable = hasVisibleChildren(scopes, nodeIndex);
            const bool expanded = expandable && m_expandedScopes.contains(nodeIndex);
            const f64 pctFrame =
                frameBudgetMs > 0.0 ? (scope.avgSelfMs / frameBudgetMs) * 100.0 : 0.0;
            const f64 pctParent =
                parentTotalMs > 0.0
                    ? (scope.avgSelfMs / std::max(parentTotalMs, 0.001)) * 100.0
                    : pctFrame;

            char self[32];
            char total[32];
            char frame[32];
            char parent[32];
            char maximum[32];
            char calls[32];
            std::snprintf(self, sizeof(self), "%.3f", static_cast<f32>(scope.avgSelfMs));
            std::snprintf(total, sizeof(total), "%.3f", static_cast<f32>(scope.avgMs));
            std::snprintf(frame, sizeof(frame), "%.1f%%", static_cast<f32>(pctFrame));
            std::snprintf(parent, sizeof(parent), "%.1f%%", static_cast<f32>(pctParent));
            std::snprintf(maximum, sizeof(maximum), "%.3f", static_cast<f32>(scope.maxMs));
            std::snprintf(calls, sizeof(calls), "%llu",
                          static_cast<unsigned long long>(scope.callCount));

            auto& row = table.addRow({scope.name ? scope.name : "?", self, total, frame,
                                      parent, maximum, calls});
            row.setIndent(depth);
            row.setExpandable(expandable);
            row.setExpanded(expanded);
            row.setOnToggle([this, nodeIndex](bool open) {
                if (open) {
                    m_expandedScopes.insert(nodeIndex);
                } else {
                    m_expandedScopes.erase(nodeIndex);
                }
            });

            if (expanded) {
                buildScopeRows(table, scopes, nodeIndex, depth + 1, frameBudgetMs,
                               scope.avgMs);
            }
        }
    }
#endif

private:
    bool m_open   = true;
    bool m_paused = false;
    bool m_hideEmptyScopes = true;
    bool m_forceScopeRefresh = true;
    bool m_expandAllRequested = false;
    int m_sortColumn = 1;
    bool m_sortAscending = false;
    std::set<u32> m_expandedScopes;
    Vector<Debug::Profiler::ScopeStats> m_cachedScopes;
    std::array<f32, 120> m_frameTimes {};
    u32 m_frameIdx = 0;
};

}  // namespace Caffeine::Editor
