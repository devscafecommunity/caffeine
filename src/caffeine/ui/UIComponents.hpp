#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "containers/FixedString.hpp"
#include "ecs/Entity.hpp"

#include <functional>

namespace Caffeine::UI {

using namespace Caffeine;

struct UIColor {
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    f32 a = 1.0f;

    static UIColor white()       { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static UIColor black()       { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static UIColor transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    static UIColor red()         { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    static UIColor green()       { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    static UIColor blue()        { return {0.0f, 0.0f, 1.0f, 1.0f}; }

    bool operator==(const UIColor& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

struct UIRect {
    Vec2 position = {0.0f, 0.0f};
    Vec2 size     = {0.0f, 0.0f};

    bool contains(Vec2 point) const {
        return point.x >= position.x && point.x <= position.x + size.x &&
               point.y >= position.y && point.y <= position.y + size.y;
    }

    bool isValid() const { return size.x > 0.0f && size.y > 0.0f; }
};

/*
 * Layout math: anchorMin/Max are fractions of parent size; offsetMin/Max are
 * pixel deltas from those anchor corners. Fixed widget at (px,py) size (w,h):
 *   anchorMin=anchorMax={0,0}, offsetMin={px,py}, offsetMax={px+w,py+h}.
 */
struct RectTransform {
    Vec2 anchorMin = {0.0f, 0.0f};
    Vec2 anchorMax = {0.0f, 0.0f};
    Vec2 offsetMin = {0.0f, 0.0f};
    Vec2 offsetMax = {0.0f, 0.0f};
};

// ============================================================================
// UIStyle — visual appearance of a widget.
// ============================================================================
struct UIStyle {
    UIColor backgroundColor = {0.1f, 0.1f, 0.1f, 0.9f};
    UIColor textColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    UIColor borderColor     = {0.3f, 0.3f, 0.3f, 1.0f};
    f32     borderWidth     = 1.0f;
    f32     borderRadius    = 4.0f;
    f32     fontSize        = 16.0f;
    Vec2    textAlignment   = {0.5f, 0.5f};
};

// ============================================================================
// UIWidgetType — discriminator for the widget category.
// ============================================================================
enum class UIWidgetType : u8 {
    Canvas,
    Panel,
    Button,
    Label,
    ProgressBar,
    Checkbox,
    Slider
};

static constexpr u32 kUIInvalidParent = 0xFFFFFFFFu;

// ============================================================================
// UIWidget — base ECS component present on every UI entity.
//
//  computedRect is filled each frame by UISystem::layoutWidgets().
// ============================================================================
struct UIWidget {
    UIWidgetType  type         = UIWidgetType::Panel;
    u32           parentId     = kUIInvalidParent;
    bool          visible      = true;
    bool          interactable = true;
    i32           siblingOrder = 0;
    UIStyle       style;
    RectTransform transform;
    UIRect        computedRect;

    std::function<void(ECS::Entity)>      onClick;
    std::function<void(ECS::Entity)>      onHoverEnter;
    std::function<void(ECS::Entity)>      onHoverExit;
    std::function<void(ECS::Entity, f32)> onValueChanged;
};

// ── Widget-specific components ──────────────────────────────────────────────

struct UIButton {
    FixedString<64> labelText;
    UIColor         idleColor    = {0.2f,  0.2f,  0.2f,  1.0f};
    UIColor         hoverColor   = {0.35f, 0.35f, 0.35f, 1.0f};
    UIColor         pressedColor = {0.1f,  0.1f,  0.1f,  1.0f};
    bool            isHovered    = false;
    bool            isPressed    = false;
};

struct UILabel {
    FixedString<256> text;
    bool             wordWrap = false;
};

struct UIProgressBar {
    f32     minValue     = 0.0f;
    f32     maxValue     = 100.0f;
    f32     currentValue = 50.0f;
    bool    showText     = false;
    UIColor fillColor    = {0.2f, 0.8f, 0.2f, 1.0f};
};

struct UISlider {
    f32  minValue     = 0.0f;
    f32  maxValue     = 1.0f;
    f32  currentValue = 0.5f;
    bool snapToInt    = false;
};

struct UICheckbox {
    bool    checked      = false;
    UIColor checkedColor = {0.2f, 0.6f, 1.0f, 1.0f};
};

}  // namespace Caffeine::UI
