#include "editor/EditorSpinner.hpp"
#include "editor/EditorIcons.hpp"

#include <cmath>

namespace Caffeine::Editor {

void EditorSpinner::draw(const char* id, ImVec2 size, ImU32 color) {
#ifdef CF_HAS_IMGUI
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    const f32 radius = std::min(size.x, size.y) * 0.38f;
    const f32 t = static_cast<f32>(ImGui::GetTime());
    const f32 a0 = t * 5.5f;
    const f32 a1 = a0 + 4.2f;
    dl->PathClear();
    dl->PathArcTo(center, radius, a0, a1, 24);
    dl->PathStroke(color, 0, 2.5f);
    ImGui::InvisibleButton(id, size);
#endif
}

void EditorSpinner::drawSvg(const char* id, ImVec2 size, ImU32 tint) {
#ifdef CF_HAS_IMGUI
    ImTextureRef tex = EditorIcons::get("spinners/90-ring");
    if (tex._TexData == nullptr && tex._TexID == ImTextureID_Invalid) {
        draw(id, size, tint);
        return;
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    const f32 angle = static_cast<f32>(ImGui::GetTime()) * 4.0f;
    const f32 hw = size.x * 0.5f;
    const f32 hh = size.y * 0.5f;
    const f32 c = std::cos(angle);
    const f32 s = std::sin(angle);

    auto rot = [&](f32 lx, f32 ly) -> ImVec2 {
        return ImVec2(center.x + lx * c - ly * s, center.y + lx * s + ly * c);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImageQuad(tex,
                     rot(-hw, -hh), rot(hw, -hh), rot(hw, hh), rot(-hw, hh),
                     ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), tint);
    ImGui::InvisibleButton(id, size);
#endif
}

} // namespace Caffeine::Editor
