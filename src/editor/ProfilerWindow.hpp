#pragma once
#include "core/Types.hpp"
#include "debug/Profiler.hpp"
#include "containers/Vector.hpp"
#include <algorithm>
#include <array>

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
            }

            ImGui::Text("Frame: %.2f ms   FPS: %.1f", lastMs, fps);

            f32 maxVal = 33.3f;
            for (const f32 sample : m_frameTimes) {
                maxVal = std::max(maxVal, sample);
            }
            maxVal = std::min(maxVal * 1.15f, 200.0f);

            ImGui::PlotLines("Frame time (ms)", m_frameTimes.data(), 120, 0,
                             nullptr, 0.0f, maxVal, ImVec2(-1, 80));
            ImGui::TextDisabled("Budget: 16.7 ms (60 FPS)");

            Vector<Debug::Profiler::ScopeStats> scopes;
            profiler.report(scopes);
            if (scopes.empty()) {
                ImGui::TextDisabled("No scope samples yet. Instrument hot paths with CF_PROFILE_SCOPE.");
            } else if (ImGui::BeginTable("scopes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Scope");
                ImGui::TableSetupColumn("avg ms");
                ImGui::TableSetupColumn("max ms");
                ImGui::TableSetupColumn("total ms");
                ImGui::TableSetupColumn("calls");
                ImGui::TableHeadersRow();
                for (usize i = 0; i < scopes.size(); ++i) {
                    const auto& s = scopes[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s.name ? s.name : "");
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", static_cast<f32>(s.avgMs));
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", static_cast<f32>(s.maxMs));
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", static_cast<f32>(s.totalMs));
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%llu", static_cast<unsigned long long>(s.callCount));
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
#endif

private:
    bool m_open   = true;
    bool m_paused = false;
    std::array<f32, 120> m_frameTimes {};
    u32 m_frameIdx = 0;
};

}  // namespace Caffeine::Editor
