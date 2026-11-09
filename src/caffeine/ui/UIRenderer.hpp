#pragma once

#include "ui/UIComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/SceneComponents.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::UI {

#ifdef CF_HAS_IMGUI

inline ImU32 toImColor(const UIColor& c) {
    return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f),
                    static_cast<int>(c.b * 255.0f), static_cast<int>(c.a * 255.0f));
}

inline void drawWidgets(ECS::World& world, ImDrawList* dl, ImVec2 screenOrigin, ImVec2 panelSize) {
    if (!dl || panelSize.x < 1.0f || panelSize.y < 1.0f) return;

    ECS::ComponentQuery q;
    q.with<UIWidget>();
    world.forEach<UIWidget>(q, [&](ECS::Entity e, UIWidget& w) {
        if (!w.visible || w.type == UIWidgetType::Canvas) return;
        if (!w.computedRect.isValid()) return;

        const f32 refW = 1280.0f;
        const f32 refH = 720.0f;
        const f32 sx = panelSize.x / refW;
        const f32 sy = panelSize.y / refH;

        const ImVec2 p0(screenOrigin.x + w.computedRect.position.x * sx,
                        screenOrigin.y + w.computedRect.position.y * sy);
        const ImVec2 p1(p0.x + w.computedRect.size.x * sx, p0.y + w.computedRect.size.y * sy);
        if (p1.x < screenOrigin.x || p1.y < screenOrigin.y) return;

        const ImU32 bg = toImColor(w.style.backgroundColor);
        const ImU32 border = toImColor(w.style.borderColor);
        const ImU32 textCol = toImColor(w.style.textColor);

        switch (w.type) {
            case UIWidgetType::Panel:
                dl->AddRectFilled(p0, p1, bg, w.style.borderRadius);
                dl->AddRect(p0, p1, border, w.style.borderRadius, 0, w.style.borderWidth);
                break;
            case UIWidgetType::Button: {
                const UIButton* btn = world.get<UIButton>(e);
                ImU32 col = bg;
                if (btn) {
                    if (btn->isPressed) col = toImColor(btn->pressedColor);
                    else if (btn->isHovered) col = toImColor(btn->hoverColor);
                    else col = toImColor(btn->idleColor);
                }
                dl->AddRectFilled(p0, p1, col, w.style.borderRadius);
                dl->AddRect(p0, p1, border, w.style.borderRadius, 0, w.style.borderWidth);
                if (btn && !btn->labelText.empty()) {
                    dl->AddText(p0, textCol, btn->labelText.cStr());
                }
                break;
            }
            case UIWidgetType::Label:
                if (const UILabel* lbl = world.get<UILabel>(e)) {
                    dl->AddText(p0, textCol, lbl->text.cStr());
                }
                break;
            case UIWidgetType::ProgressBar: {
                dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 36, 220), w.style.borderRadius);
                if (const UIProgressBar* pb = world.get<UIProgressBar>(e)) {
                    const f32 range = std::max(0.0001f, pb->maxValue - pb->minValue);
                    const f32 t = std::clamp((pb->currentValue - pb->minValue) / range, 0.0f, 1.0f);
                    ImVec2 fillEnd(p0.x + (p1.x - p0.x) * t, p1.y);
                    dl->AddRectFilled(p0, fillEnd, toImColor(pb->fillColor), w.style.borderRadius);
                    if (pb->showText) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.0f", pb->currentValue);
                        dl->AddText(p0, textCol, buf);
                    }
                }
                dl->AddRect(p0, p1, border, w.style.borderRadius, 0, 1.0f);
                break;
            }
            case UIWidgetType::Slider: {
                dl->AddRectFilled(p0, p1, bg, w.style.borderRadius);
                if (const UISlider* sl = world.get<UISlider>(e)) {
                    const f32 range = std::max(0.0001f, sl->maxValue - sl->minValue);
                    const f32 t = std::clamp((sl->currentValue - sl->minValue) / range, 0.0f, 1.0f);
                    const f32 knobX = p0.x + (p1.x - p0.x) * t;
                    dl->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(knobX, p1.y),
                                      IM_COL32(80, 140, 220, 230), w.style.borderRadius);
                }
                dl->AddRect(p0, p1, border, w.style.borderRadius, 0, 1.0f);
                break;
            }
            case UIWidgetType::Checkbox: {
                const UICheckbox* cb = world.get<UICheckbox>(e);
                dl->AddRectFilled(p0, p1, bg, 4.0f);
                if (cb && cb->checked) {
                    dl->AddRectFilled(ImVec2(p0.x + 4, p0.y + 4), ImVec2(p1.x - 4, p1.y - 4),
                                      toImColor(cb->checkedColor), 2.0f);
                }
                dl->AddRect(p0, p1, border, 4.0f, 0, 1.0f);
                break;
            }
            default:
                break;
        }
    });
}

#endif

}  // namespace Caffeine::UI
