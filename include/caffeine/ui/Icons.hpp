#pragma once

/**
 * Caffeine UI — semantic icon resolution.
 *
 * The editor installs an icon provider backed by its SVG/PNG atlas. Plugins call
 * the helpers below and stay independent from Doppio's asset layout. Everything
 * is inline so plugin libraries do not need a separate UI link step.
 */

#include <imgui.h>

#include <functional>
#include <string>

namespace Caffeine::UIKit {

struct IconProvider {
    std::function<bool(const std::string&)> exists;
    std::function<ImTextureRef(const std::string&)> texture;
};

IconProvider& iconProviderRef();

void setIconProvider(IconProvider provider);
void clearIconProvider();

bool hasIcon(const char* name);
ImTextureRef iconTexture(const char* name);

inline bool icon(const char* name, float size = 16.0f, ImU32 tint = IM_COL32_WHITE) {
    const ImTextureRef texture = iconTexture(name);
    if (texture._TexData == nullptr && texture._TexID == ImTextureID_Invalid) return false;
    const ImVec4 tintCol = ImGui::ColorConvertU32ToFloat4(tint);
    ImGui::Image(texture, ImVec2(size, size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                 tintCol, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    return true;
}

inline bool iconButton(const char* id, const char* name, const char* tooltip = nullptr,
                       float size = 0.0f, bool selected = false) {
    const float side = size > 0.0f ? size : ImGui::GetFrameHeight();
    ImGui::PushID(id);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }
    const bool pressed = ImGui::Button("##icon_button", ImVec2(side, side));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImTextureRef texture = iconTexture(name);
    if (texture._TexData != nullptr || texture._TexID != ImTextureID_Invalid) {
        const float inner = side * 0.68f;
        const ImVec2 p0((min.x + max.x - inner) * 0.5f, (min.y + max.y - inner) * 0.5f);
        ImGui::GetWindowDrawList()->AddImage(
            texture, p0, ImVec2(p0.x + inner, p0.y + inner), ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f), IM_COL32_WHITE);
    } else if (name && name[0]) {
        const char fallback[2] = {name[0], '\0'};
        const ImVec2 textSize = ImGui::CalcTextSize(fallback);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2((min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f),
            IM_COL32(220, 220, 220, 255), fallback);
    }
    if (selected) ImGui::PopStyleColor();
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return pressed;
}

inline void iconLabel(const char* name, const char* label, float size = 16.0f) {
    if (name && hasIcon(name)) {
        icon(name, size);
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(label);
}

}  // namespace Caffeine::UIKit
