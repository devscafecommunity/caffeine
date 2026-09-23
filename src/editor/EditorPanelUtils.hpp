#pragma once

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI

inline ImGuiID detachedPanelClassId() {
    static const ImGuiID id = ImHashStr("CaffeineDetachedPanel");
    return id;
}

inline void editorPanelApplyDetach(bool detached, ImVec2 detachedSize = ImVec2(0, 0)) {
    if (!detached) return;

    ImGuiWindowClass windowClass;
    windowClass.ClassId = detachedPanelClassId();
    windowClass.DockingAllowUnclassed = true;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);

    if (detachedSize.x > 0.0f && detachedSize.y > 0.0f) {
        ImGui::SetNextWindowSize(detachedSize, ImGuiCond_FirstUseEver);
        const ImGuiViewport* mainVp = ImGui::GetMainViewport();
        if (mainVp) {
            ImGui::SetNextWindowPos(
                ImVec2(mainVp->Pos.x + 48.0f, mainVp->Pos.y + 48.0f),
                ImGuiCond_FirstUseEver);
        }
    }
}

// Small button on the tab bar (right side) to pop the panel into its own OS window.
inline void editorPanelDetachTabButton(bool& detached) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window) return;

    const float tabH = ImGui::GetFrameHeight();
    const float btnW = tabH + 2.0f;
    const float btnH = tabH - 2.0f;

    float x = window->Pos.x + window->SizeFull.x - btnW - 4.0f;
    float y = window->Pos.y + 2.0f;
    if (window->DockIsActive && window->DockNode) {
        x = window->Pos.x + window->SizeFull.x - btnW - 4.0f;
        y = window->Pos.y + 2.0f;
    }

    ImGui::PushID("##panel_detach_tab");
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
    const char* label = detached ? "Dock" : "Pop";
    if (ImGui::Button(label, ImVec2(btnW, btnH))) {
        detached = !detached;
    }
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(detached ? "Dock back into main window"
                                   : "Pop out to separate window (drag to another monitor)");
    }
    ImGui::PopID();
}

// Skip GPU work for collapsed/hidden/throttled dock panels; still safe to blit the last target.
inline bool editorPanelWorthGpuRender(ImVec2 origin, ImVec2 size, u32 frameInterval = 1) {
    if (size.x < 32.0f || size.y < 32.0f) return false;
    if (ImGui::IsWindowCollapsed()) return false;
    const ImVec2 max(origin.x + size.x, origin.y + size.y);
    if (!ImGui::IsRectVisible(origin, max)) return false;
    if (frameInterval > 1u && (ImGui::GetFrameCount() % frameInterval) != 0u) return false;
    return true;
}

#endif

}  // namespace Caffeine::Editor
