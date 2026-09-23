#pragma once
#include "core/Types.hpp"
#include "debug/Profiler.hpp"
#include "debug/SystemMetrics.hpp"
#include "containers/Vector.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
        if (ImGui::Begin("Profiler", &m_open)) {
            const f32 lastMs = lastFrameTime();
            const f32 fps = lastMs > 0.0f ? 1000.0f / lastMs : 0.0f;

            if (ImGui::Button(m_paused ? "Resume" : "Pause")) {
                m_paused = !m_paused;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Stats")) {
                Debug::Profiler::instance().reset();
                m_forceScopeRefresh = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(m_expandAll ? "Collapse All" : "Expand All")) {
                m_expandAll = !m_expandAll;
            }

            ImGui::Text("Frame: %.2f ms   FPS: %.1f", lastMs, fps);

            const Debug::SystemMetrics sys = Debug::sampleSystemMetrics();
            ImGui::SeparatorText("Hardware");
            ImGui::Text("CPU (process):  %.1f%%", sys.processCpuPercent);
            if (sys.gpuAvailable && sys.gpuBusyPercent >= 0.0f) {
                ImGui::Text("GPU:            %.1f%%", sys.gpuBusyPercent);
            } else {
                ImGui::TextDisabled("GPU:            n/a");
            }
            if (sys.ramTotalMiB > 0.0f) {
                ImGui::Text("RAM:            %.0f / %.0f MiB", sys.ramUsedMiB, sys.ramTotalMiB);
            } else {
                ImGui::Text("RAM:            %.0f MiB", sys.ramUsedMiB);
            }
            if (sys.vramTotalMiB > 0.0f) {
                ImGui::Text("VRAM:           %.0f / %.0f MiB", sys.vramUsedMiB, sys.vramTotalMiB);
            }

            f32 maxVal = 33.3f;
            for (const f32 sample : m_frameTimes) {
                maxVal = std::max(maxVal, sample);
            }
            maxVal = std::min(maxVal * 1.15f, 200.0f);

            ImGui::PlotLines("Frame time (ms)", m_frameTimes.data(), 120, 0,
                             nullptr, 0.0f, maxVal, ImVec2(-1, 80));
            ImGui::TextDisabled("Budget: 16.7 ms (60 FPS)");

            if (m_forceScopeRefresh || (ImGui::GetFrameCount() % 15u) == 0u) {
                profiler.report(m_cachedScopes);
                m_forceScopeRefresh = false;
            }

            const Vector<Debug::Profiler::ScopeStats>& scopes = m_cachedScopes;
            usize sampledScopes = 0;
            for (usize i = 1; i < scopes.size(); ++i) {
                if (scopes[i].callCount > 0) {
                    sampledScopes++;
                }
            }

            ImGui::SeparatorText("Scope tree");
            ImGui::TextDisabled(
                "Hierarchical call stack — self ms = exclusive, total ms = inclusive. "
                "Instrument with CF_PROFILE_SCOPE.");
            ImGui::Checkbox("Sort by self time", &m_sortBySelf);
            ImGui::SameLine();
            ImGui::Checkbox("Hide empty scopes", &m_hideEmptyScopes);

            if (sampledScopes == 0) {
                ImGui::TextDisabled("No scope samples yet. Instrument hot paths with CF_PROFILE_SCOPE.");
            } else if (ImGui::BeginTable("scope_tree", 7,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                              | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                          ImVec2(-1.0f, 260.0f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Scope");
                ImGui::TableSetupColumn("self ms");
                ImGui::TableSetupColumn("total ms");
                ImGui::TableSetupColumn("% frame");
                ImGui::TableSetupColumn("% parent");
                ImGui::TableSetupColumn("max ms");
                ImGui::TableSetupColumn("calls");
                ImGui::TableHeadersRow();

                const f64 frameBudgetMs = (lastMs > 0.0f) ? static_cast<f64>(lastMs) : 16.7;
                renderScopeChildren(scopes, 0u, frameBudgetMs, 0.0);

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

private:
    static void appendScopePath(const Vector<Debug::Profiler::ScopeStats>& scopes, u32 nodeIndex,
                                char* out, usize outCap) {
        if (nodeIndex == Debug::Profiler::kInvalidNode || nodeIndex >= scopes.size() || outCap == 0) {
            if (outCap > 0) out[0] = '\0';
            return;
        }

        const auto& node = scopes[nodeIndex];
        if (node.parentIndex != Debug::Profiler::kInvalidNode && node.parentIndex != 0u
            && node.parentIndex < scopes.size()) {
            appendScopePath(scopes, node.parentIndex, out, outCap);
            const usize len = std::strlen(out);
            if (len + 3 < outCap) {
                std::snprintf(out + len, outCap - len, " > ");
            }
        }

        const usize len = std::strlen(out);
        if (node.name && len + 1 < outCap) {
            std::snprintf(out + len, outCap - len, "%s", node.name);
        }
    }

    void renderScopeChildren(const Vector<Debug::Profiler::ScopeStats>& scopes, u32 parentIndex,
                             f64 frameBudgetMs, f64 parentTotalMs) {
        Vector<u32> children;
        u32 child = scopes[parentIndex].firstChildIndex;
        while (child != Debug::Profiler::kInvalidNode && child < scopes.size()) {
            if (!m_hideEmptyScopes || scopes[child].callCount > 0) {
                children.pushBack(child);
            }
            child = scopes[child].nextSiblingIndex;
        }

        std::sort(children.begin(), children.end(), [&](u32 a, u32 b) {
            const auto& sa = scopes[a];
            const auto& sb = scopes[b];
            if (m_sortBySelf) {
                if (sa.selfMs != sb.selfMs) return sa.selfMs > sb.selfMs;
                return sa.totalMs > sb.totalMs;
            }
            if (sa.totalMs != sb.totalMs) return sa.totalMs > sb.totalMs;
            return sa.selfMs > sb.selfMs;
        });

        for (u32 nodeIndex : children) {
            renderScopeNode(scopes, nodeIndex, frameBudgetMs, parentTotalMs);
        }
    }

    void renderScopeNode(const Vector<Debug::Profiler::ScopeStats>& scopes, u32 nodeIndex,
                         f64 frameBudgetMs, f64 parentTotalMs) {
        const auto& scope = scopes[nodeIndex];
        bool hasChildren = false;
        for (u32 c = scope.firstChildIndex; c != Debug::Profiler::kInvalidNode && c < scopes.size();
             c = scopes[c].nextSiblingIndex) {
            if (!m_hideEmptyScopes || scopes[c].callCount > 0) {
                hasChildren = true;
                break;
            }
        }

        const f64 avgSelf = scope.avgSelfMs;
        const f64 avgTotal = scope.avgMs;
        const f64 pctFrame = (frameBudgetMs > 0.0) ? (avgSelf / frameBudgetMs) * 100.0 : 0.0;
        const f64 parentDenom = std::max(parentTotalMs, 0.001);
        const f64 pctParent = (parentTotalMs > 0.0) ? (avgSelf / parentDenom) * 100.0 : pctFrame;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        } else if (m_expandAll) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        const bool open = ImGui::TreeNodeEx(
            (void*)(static_cast<intptr_t>(nodeIndex + 1u)),
            flags,
            "%s",
            scope.name ? scope.name : "?");
        if (ImGui::IsItemHovered()) {
            char path[512];
            path[0] = '\0';
            appendScopePath(scopes, nodeIndex, path, sizeof(path));
            ImGui::SetTooltip("%s\navg self: %.3f ms   avg total: %.3f ms", path, avgSelf, avgTotal);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", static_cast<f32>(avgSelf));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.3f", static_cast<f32>(avgTotal));
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.1f%%", static_cast<f32>(pctFrame));
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.1f%%", static_cast<f32>(pctParent));
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.3f", static_cast<f32>(scope.maxMs));
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%llu", static_cast<unsigned long long>(scope.callCount));

        if (hasChildren && open) {
            renderScopeChildren(scopes, nodeIndex, frameBudgetMs, avgTotal);
            ImGui::TreePop();
        }
    }
#endif

private:
    bool m_open   = true;
    bool m_paused = false;
    bool m_expandAll = false;
    bool m_sortBySelf = true;
    bool m_hideEmptyScopes = true;
    bool m_forceScopeRefresh = true;
    Vector<Debug::Profiler::ScopeStats> m_cachedScopes;
    std::array<f32, 120> m_frameTimes {};
    u32 m_frameIdx = 0;
};

}  // namespace Caffeine::Editor
