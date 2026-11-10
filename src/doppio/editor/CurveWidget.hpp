#pragma once

#ifdef CF_HAS_IMGUI

#include "animation/SkinLibrary.hpp"
#include "caffeine/ui/UI.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

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
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    constexpr int kModeCount = static_cast<int>(std::size(kCurveModes));
    int modeIndex = 0;
    for (int i = 0; i < kModeCount; ++i) {
        if (kCurveModes[i] == curve.mode) {
            modeIndex = i;
            break;
        }
    }
    std::vector<std::string> names;
    names.reserve(kModeCount);
    for (Animation::KeyInterp mode : kCurveModes) {
        names.emplace_back(Animation::keyInterpName(mode));
    }

    bool changed = false;
    Column root(std::string("curve_mode_") + label);
    auto& combo = root.emplace<Elements::Combo>(label, &modeIndex, names);
    combo.setWidth(width);
    combo.setOnChange([&curve, &changed](int index) {
        if (index >= 0 && index < kModeCount) {
            applyKeyInterp(curve, kCurveModes[index]);
            changed = true;
        }
    });

    UIContext ctx;
    ctx.draw(root);
    return changed;
}

/// Bezier editor with draggable handles. Dragging a handle switches the curve to Custom.
inline bool editKeyCurve(const char* id, Animation::KeyCurve& curve, f32 width, f32 height = 120.0f) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    bool changed = false;
    Column root(std::string("curve_edit_") + id);

    const Animation::KeyCurve shape =
        curve.mode == Animation::KeyInterp::Custom
            ? curve
            : curve.mode == Animation::KeyInterp::Linear ? Animation::KeyCurve{}
                                                         : Animation::keyCurvePreset(curve.mode);

    auto& editor = root.emplace<CurveEditor>("curve_editor");
    editor.setWidth(std::max(width, 80.0f));
    editor.setHeight(height);
    editor.setCurve(shape.x1, shape.y1, shape.x2, shape.y2);
    editor.setShowHandles(curve.mode != Animation::KeyInterp::Constant);
    editor.setSampler([&curve](float u) { return Animation::evaluateKeyCurve(curve, u); });
    editor.setOnChange([&curve, &changed](float x1, float y1, float x2, float y2) {
        if (curve.mode != Animation::KeyInterp::Custom) {
            applyKeyInterp(curve, Animation::KeyInterp::Custom);
        }
        curve.x1 = x1;
        curve.y1 = y1;
        curve.x2 = x2;
        curve.y2 = y2;
        changed = true;
    });

    constexpr int kModeCount = static_cast<int>(std::size(kCurveModes));
    int modeIndex = 0;
    for (int i = 0; i < kModeCount; ++i) {
        if (kCurveModes[i] == curve.mode) {
            modeIndex = i;
            break;
        }
    }
    std::vector<std::string> names;
    names.reserve(kModeCount);
    for (Animation::KeyInterp mode : kCurveModes) {
        names.emplace_back(Animation::keyInterpName(mode));
    }
    auto& mode = root.emplace<Elements::Combo>("Curve", &modeIndex, names);
    mode.setWidth(std::max(width - 60.0f, 80.0f));
    mode.setOnChange([&curve, &changed](int index) {
        if (index >= 0 && index < kModeCount) {
            applyKeyInterp(curve, kCurveModes[index]);
            changed = true;
        }
    });

    struct Preset {
        const char* label;
        Animation::KeyInterp mode;
    };
    static const Preset kPresets[] = {{"Linear", Animation::KeyInterp::Linear},
                                      {"In", Animation::KeyInterp::EaseIn},
                                      {"Out", Animation::KeyInterp::EaseOut},
                                      {"In Out", Animation::KeyInterp::EaseInOut},
                                      {"Step", Animation::KeyInterp::Constant}};
    auto& presets = root.emplace<Row>("curve_presets");
    for (const Preset& preset : kPresets) {
        auto& button = presets.emplace<Button>(preset.label, ButtonVariant::Segmented);
        button.setGrow(true);
        button.setSelected(curve.mode == preset.mode);
        button.setOnClick([&curve, &changed, mode = preset.mode] {
            applyKeyInterp(curve, mode);
            changed = true;
        });
    }

    UIContext ctx;
    ctx.draw(root);
    return changed;
}

}  // namespace Caffeine::Editor

#endif
