#pragma once

#ifdef CF_HAS_IMGUI

#include "animation/SkinLibrary.hpp"

#include <imgui.h>

#include <algorithm>

namespace Caffeine::Editor {

inline const Animation::KeyInterp kCurveModes[] = {
    Animation::KeyInterp::Linear,    Animation::KeyInterp::EaseIn,   Animation::KeyInterp::EaseOut,
    Animation::KeyInterp::EaseInOut, Animation::KeyInterp::Constant, Animation::KeyInterp::Custom,
};

inline void applyKeyInterp(Animation::KeyCurve& curve, Animation::KeyInterp mode) {
    if (mode == Animation::KeyInterp::Custom) {
        if (curve.mode != Animation::KeyInterp::Custom) {
            curve = (curve.mode == Animation::KeyInterp::Linear || curve.mode == Animation::KeyInterp::Constant)
                        ? Animation::KeyCurve{}
                        : Animation::keyCurvePreset(curve.mode);
        }
        curve.mode = Animation::KeyInterp::Custom;
        return;
    }
    curve = Animation::keyCurvePreset(mode);
}

/// Curve shape inside [min, max], 0..1 on both axes.
inline void drawCurveGlyph(ImDrawList* dl, ImVec2 min, ImVec2 max, const Animation::KeyCurve& curve, ImU32 color,
                           f32 thickness = 1.5f) {
    ImVec2 points[17];
    for (int i = 0; i < 17; ++i) {
        const f32 u = static_cast<f32>(i) / 16.0f;
        const f32 v = std::clamp(Animation::evaluateKeyCurve(curve, u), -0.2f, 1.2f);
        points[i] = ImVec2(min.x + (max.x - min.x) * u, max.y - (max.y - min.y) * v);
    }
    dl->AddPolyline(points, 17, color, 0, thickness);
}

/// Combo with the interpolation presets. Returns true when the mode changed.
inline bool curveModeCombo(const char* label, Animation::KeyCurve& curve, f32 width = 130.0f) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, Animation::keyInterpName(curve.mode))) {
        for (Animation::KeyInterp mode : kCurveModes) {
            if (ImGui::Selectable(Animation::keyInterpName(mode), curve.mode == mode)) {
                applyKeyInterp(curve, mode);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// Bezier editor with draggable handles. Dragging a handle switches the curve to Custom.
inline bool editKeyCurve(const char* id, Animation::KeyCurve& curve, f32 width, f32 height = 120.0f) {
    ImGui::PushID(id);
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 size(std::max(width, 80.0f), height);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 pad = 10.0f;
    const f32 vMin = -0.25f;
    const f32 vMax = 1.25f;
    auto toScreen = [&](f32 u, f32 v) {
        return ImVec2(pos.x + pad + u * (size.x - pad * 2.0f),
                      pos.y + size.y - pad - ((v - vMin) / (vMax - vMin)) * (size.y - pad * 2.0f));
    };
    ImGui::InvisibleButton("##graph", size);
    const bool active = ImGui::IsItemActive();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID handleKey = ImGui::GetID("##handle");
    int handle = storage->GetInt(handleKey, 0);

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(26, 28, 34, 255), 4.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(60, 64, 74, 255), 4.0f);
    dl->AddLine(toScreen(0.0f, 0.0f), toScreen(1.0f, 0.0f), IM_COL32(70, 74, 86, 255));
    dl->AddLine(toScreen(0.0f, 1.0f), toScreen(1.0f, 1.0f), IM_COL32(70, 74, 86, 255));
    dl->AddLine(toScreen(0.0f, 0.0f), toScreen(1.0f, 1.0f), IM_COL32(70, 74, 86, 140));

    const bool hasHandles = curve.mode != Animation::KeyInterp::Constant;
    const Animation::KeyCurve shape = curve.mode == Animation::KeyInterp::Custom   ? curve
                                      : curve.mode == Animation::KeyInterp::Linear ? Animation::KeyCurve{}
                                                                                   : Animation::keyCurvePreset(curve.mode);
    const ImVec2 h1 = toScreen(shape.x1, shape.y1);
    const ImVec2 h2 = toScreen(shape.x2, shape.y2);
    if (hasHandles) {
        dl->AddLine(toScreen(0.0f, 0.0f), h1, IM_COL32(120, 180, 255, 200), 1.2f);
        dl->AddLine(toScreen(1.0f, 1.0f), h2, IM_COL32(120, 180, 255, 200), 1.2f);
    }
    ImVec2 points[49];
    for (int s = 0; s < 49; ++s) {
        const f32 u = static_cast<f32>(s) / 48.0f;
        points[s] = toScreen(u, Animation::evaluateKeyCurve(curve, u));
    }
    dl->AddPolyline(points, 49, IM_COL32(255, 210, 60, 255), 0, 2.0f);
    if (hasHandles) {
        dl->AddCircleFilled(h1, handle == 1 ? 6.0f : 5.0f, IM_COL32(160, 205, 255, 255), 16);
        dl->AddCircleFilled(h2, handle == 2 ? 6.0f : 5.0f, IM_COL32(160, 205, 255, 255), 16);
    }
    if (ImGui::IsItemClicked() && hasHandles) {
        auto dist2 = [&](ImVec2 p) {
            const f32 dx = io.MousePos.x - p.x;
            const f32 dy = io.MousePos.y - p.y;
            return dx * dx + dy * dy;
        };
        const f32 d1 = dist2(h1);
        const f32 d2 = dist2(h2);
        handle = std::min(d1, d2) < 196.0f ? (d1 <= d2 ? 1 : 2) : 0;
    }
    if (!active) handle = 0;
    bool changed = false;
    if (active && handle != 0) {
        if (curve.mode != Animation::KeyInterp::Custom) applyKeyInterp(curve, Animation::KeyInterp::Custom);
        const f32 u = std::clamp((io.MousePos.x - pos.x - pad) / (size.x - pad * 2.0f), 0.0f, 1.0f);
        const f32 v = std::clamp(vMin + ((pos.y + size.y - pad - io.MousePos.y) / (size.y - pad * 2.0f)) * (vMax - vMin),
                                 vMin, vMax);
        if (handle == 1) {
            curve.x1 = u;
            curve.y1 = v;
        } else {
            curve.x2 = u;
            curve.y2 = v;
        }
        changed = true;
    }
    storage->SetInt(handleKey, handle);

    changed = curveModeCombo("Curve", curve, size.x - 60.0f) || changed;
    const struct {
        const char* label;
        Animation::KeyInterp mode;
    } presets[] = {{"Linear", Animation::KeyInterp::Linear},
                   {"In", Animation::KeyInterp::EaseIn},
                   {"Out", Animation::KeyInterp::EaseOut},
                   {"In Out", Animation::KeyInterp::EaseInOut},
                   {"Step", Animation::KeyInterp::Constant}};
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(presets[i].label)) {
            applyKeyInterp(curve, presets[i].mode);
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

}  // namespace Caffeine::Editor

#endif
