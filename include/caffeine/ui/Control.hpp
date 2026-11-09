#pragma once

/**
 * Caffeine UI — interactive controls.
 *
 * Every control is an Element with a stable id, an optional size, a callback and
 * per-frame state. Controls are intentionally thin wrappers over the ImGui
 * primitives in Widgets.hpp: the composition lives in the element tree, not in
 * duplicated drawing code.
 */

#include "caffeine/ui/Element.hpp"
#include "caffeine/ui/Widgets.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Caffeine::UIKit::Elements {

enum class ButtonVariant {
    Default,
    Primary,
    Danger,
    Toolbar,
    Segmented,
    Ghost,
    IconText,
};

class Button : public Element {
public:
    explicit Button(std::string label = {}, ButtonVariant variant = ButtonVariant::Default)
        : m_label(std::move(label)), m_variant(variant) {
        if (!m_label.empty()) setId("Button:" + m_label);
    }

    const char* type() const override { return "Button"; }

    Button& setLabel(std::string label) {
        m_label = std::move(label);
        return *this;
    }
    Button& setVariant(ButtonVariant variant) {
        m_variant = variant;
        return *this;
    }
    Button& setIcon(std::string icon) {
        m_icon = std::move(icon);
        return *this;
    }
    Button& setSelected(bool selected) {
        m_selected = selected;
        return *this;
    }
    Button& setOnClick(std::function<void()> callback) {
        m_onClick = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    std::string m_icon;
    ButtonVariant m_variant = ButtonVariant::Default;
    bool m_selected = false;
    std::function<void()> m_onClick;
};

class IconButton : public Element {
public:
    explicit IconButton(std::string icon, std::string tooltip = {})
        : m_icon(std::move(icon)), m_tooltipText(std::move(tooltip)) {
        setId("IconButton:" + m_icon);
    }

    const char* type() const override { return "IconButton"; }

    IconButton& setIcon(std::string icon) {
        m_icon = std::move(icon);
        return *this;
    }
    IconButton& setSelected(bool selected) {
        m_selected = selected;
        return *this;
    }
    IconButton& setOnClick(std::function<void()> callback) {
        m_onClick = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_icon;
    std::string m_tooltipText;
    bool m_selected = false;
    std::function<void()> m_onClick;
};

class Checkbox : public Element {
public:
    explicit Checkbox(std::string label, bool* value)
        : m_label(std::move(label)), m_value(value) {
        if (!m_label.empty()) setId("Checkbox:" + m_label);
    }

    const char* type() const override { return "Checkbox"; }

    Checkbox& setOnChange(std::function<void(bool)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    bool* m_value = nullptr;
    std::function<void(bool)> m_onChange;
};

class SliderFloat : public Element {
public:
    SliderFloat(std::string label, float* value, float min, float max,
                std::string format = "%.3f")
        : m_label(std::move(label)),
          m_value(value),
          m_min(min),
          m_max(max),
          m_format(std::move(format)) {
        if (!m_label.empty()) setId("SliderFloat:" + m_label);
    }

    const char* type() const override { return "SliderFloat"; }

    SliderFloat& setOnChange(std::function<void(float)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    float* m_value = nullptr;
    float m_min = 0.0f;
    float m_max = 1.0f;
    std::string m_format;
    std::function<void(float)> m_onChange;
};

class SliderInt : public Element {
public:
    SliderInt(std::string label, int* value, int min, int max)
        : m_label(std::move(label)), m_value(value), m_min(min), m_max(max) {
        if (!m_label.empty()) setId("SliderInt:" + m_label);
    }

    const char* type() const override { return "SliderInt"; }

    SliderInt& setOnChange(std::function<void(int)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    int* m_value = nullptr;
    int m_min = 0;
    int m_max = 100;
    std::function<void(int)> m_onChange;
};

class DragFloat : public Element {
public:
    DragFloat(std::string label, float* value, float speed = 0.1f, float min = 0.0f,
              float max = 0.0f, std::string format = "%.3f")
        : m_label(std::move(label)),
          m_value(value),
          m_speed(speed),
          m_min(min),
          m_max(max),
          m_format(std::move(format)) {
        if (!m_label.empty()) setId("DragFloat:" + m_label);
    }

    const char* type() const override { return "DragFloat"; }

    DragFloat& setOnChange(std::function<void(float)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    float* m_value = nullptr;
    float m_speed = 0.1f;
    float m_min = 0.0f;
    float m_max = 0.0f;
    std::string m_format;
    std::function<void(float)> m_onChange;
};

class DragInt : public Element {
public:
    DragInt(std::string label, int* value, float speed = 1.0f, int min = 0, int max = 0,
            std::string format = "%d")
        : m_label(std::move(label)),
          m_value(value),
          m_speed(speed),
          m_min(min),
          m_max(max),
          m_format(std::move(format)) {
        if (!m_label.empty()) setId("DragInt:" + m_label);
    }

    const char* type() const override { return "DragInt"; }

    DragInt& setOnChange(std::function<void(int)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    int* m_value = nullptr;
    float m_speed = 1.0f;
    int m_min = 0;
    int m_max = 0;
    std::string m_format;
    std::function<void(int)> m_onChange;
};

class DragVec2 : public Element {
public:
    DragVec2(std::string label, Vec2* value, float speed = 0.01f, float min = 0.0f,
             float max = 0.0f, std::string format = "%.2f")
        : m_label(std::move(label)), m_value(value), m_speed(speed), m_min(min), m_max(max),
          m_format(std::move(format)) {}

    const char* type() const override { return "DragVec2"; }

    DragVec2& setOnChange(std::function<void()> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext&) override;

private:
    std::string m_label;
    Vec2* m_value = nullptr;
    float m_speed = 0.01f;
    float m_min = 0.0f;
    float m_max = 0.0f;
    std::string m_format{"%.2f"};
    std::function<void()> m_onChange;
};

class DragVec3 : public Element {
public:
    DragVec3(std::string label, Vec3* value, float speed = 0.01f, float min = 0.0f,
             float max = 0.0f, std::string format = "%.2f")
        : m_label(std::move(label)), m_value(value), m_speed(speed), m_min(min), m_max(max),
          m_format(std::move(format)) {}

    const char* type() const override { return "DragVec3"; }

    DragVec3& setOnChange(std::function<void()> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext&) override;

private:
    std::string m_label;
    Vec3* m_value = nullptr;
    float m_speed = 0.01f;
    float m_min = 0.0f;
    float m_max = 0.0f;
    std::string m_format{"%.2f"};
    std::function<void()> m_onChange;
};

class Combo : public Element {
public:
    Combo(std::string label, int* value, std::vector<std::string> items)
        : m_label(std::move(label)), m_value(value), m_items(std::move(items)) {
        if (!m_label.empty()) setId("Combo:" + m_label);
    }

    const char* type() const override { return "Combo"; }

    Combo& setItems(std::vector<std::string> items) {
        m_items = std::move(items);
        return *this;
    }
    Combo& setOnChange(std::function<void(int)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    int* m_value = nullptr;
    std::vector<std::string> m_items;
    std::function<void(int)> m_onChange;
};

class TextInput : public Element {
public:
    explicit TextInput(std::string& value, std::size_t capacity = 512)
        : m_string(&value), m_capacity(capacity) {}
    TextInput(char* buffer, std::size_t size) : m_buffer(buffer), m_capacity(size) {}

    const char* type() const override { return "TextInput"; }

    TextInput& setHint(std::string hint) {
        m_hint = std::move(hint);
        return *this;
    }
    /// Request keyboard focus for the next draw (used by search dialogs).
    TextInput& focusNextFrame() {
        m_focusNext = true;
        return *this;
    }
    TextInput& setOnChange(std::function<void(const std::string&)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string* m_string = nullptr;
    char* m_buffer = nullptr;
    std::size_t m_capacity = 512;
    std::string m_hint;
    bool m_focusNext = false;
    std::function<void(const std::string&)> m_onChange;
};

class SearchField : public TextInput {
public:
    explicit SearchField(std::string& value, std::string hint = "Search...")
        : TextInput(value) {
        setHint(std::move(hint));
    }
    SearchField(char* buffer, std::size_t size, std::string hint = "Search...")
        : TextInput(buffer, size) {
        setHint(std::move(hint));
    }
    const char* type() const override { return "SearchField"; }
};

/// Read-only asset path with Browse/Clear buttons and a drag-drop target.
class PathField : public Element {
public:
    explicit PathField(std::string& path, std::string payload = "ASSET_PATH")
        : m_path(&path), m_payload(std::move(payload)) {}

    const char* type() const override { return "PathField"; }

    PathField& setOnBrowse(std::function<void()> callback) {
        m_onBrowse = std::move(callback);
        return *this;
    }
    PathField& setOnDrop(std::function<void(const char*)> callback) {
        m_onDrop = std::move(callback);
        return *this;
    }
    PathField& setOnChange(std::function<void()> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext&) override;

private:
    std::string* m_path = nullptr;
    std::string m_payload;
    std::function<void()> m_onBrowse;
    std::function<void(const char*)> m_onDrop;
    std::function<void()> m_onChange;
};

/// Button-sized area that accepts a drag payload (default: ASSET_PATH).
class DropZone : public Element {
public:
    explicit DropZone(std::string label, std::string payload = "ASSET_PATH")
        : m_label(std::move(label)), m_payload(std::move(payload)) {}

    const char* type() const override { return "DropZone"; }

    DropZone& setOnDrop(std::function<void(const char*)> callback) {
        m_onDrop = std::move(callback);
        return *this;
    }
    DropZone& setPayload(std::string payload) {
        m_payload = std::move(payload);
        return *this;
    }

    void draw(UIContext&) override;

private:
    std::string m_label;
    std::string m_payload;
    std::function<void(const char*)> m_onDrop;
};

class MultilineText : public Element {
public:
    MultilineText(char* buffer, std::size_t size,
                  ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput)
        : m_buffer(buffer), m_capacity(size), m_flags(flags) {}

    const char* type() const override { return "MultilineText"; }

    MultilineText& setFlags(ImGuiInputTextFlags flags) {
        m_flags = flags;
        return *this;
    }

    void draw(UIContext&) override;

private:
    char* m_buffer = nullptr;
    std::size_t m_capacity = 0;
    ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_AllowTabInput;
};

class ImageButton : public Element {
public:
    ImageButton(ImTextureRef texture, ImVec2 size, ImVec2 uv0 = ImVec2(0, 0),
                ImVec2 uv1 = ImVec2(1, 1), ImVec4 background = ImVec4(0, 0, 0, 0))
        : m_texture(texture), m_logicalSize(size), m_uv0(uv0), m_uv1(uv1),
          m_background(background) {}

    const char* type() const override { return "ImageButton"; }

    ImageButton& setOnClick(std::function<void()> callback) {
        m_onClick = std::move(callback);
        return *this;
    }

    void draw(UIContext&) override;

private:
    ImTextureRef m_texture;
    ImVec2 m_logicalSize{0.0f, 0.0f};
    ImVec2 m_uv0{0.0f, 0.0f};
    ImVec2 m_uv1{1.0f, 1.0f};
    ImVec4 m_background{0.0f, 0.0f, 0.0f, 0.0f};
    std::function<void()> m_onClick;
};

class ColorPicker : public Element {
public:
    explicit ColorPicker(std::string label, float* color,
                         ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs,
                         int components = 4)
        : m_label(std::move(label)), m_color(color), m_flags(flags),
          m_components(components) {
        if (!m_label.empty()) setId("ColorPicker:" + m_label);
    }

    const char* type() const override { return "ColorPicker"; }
    ColorPicker& setOnChange(std::function<void()> callback) {
        m_onChange = std::move(callback);
        return *this;
    }
    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    float* m_color = nullptr;
    ImGuiColorEditFlags m_flags = ImGuiColorEditFlags_NoInputs;
    int m_components = 4;
    std::function<void()> m_onChange;
};

/// Selectable row for lists and simple trees.
class ListItem : public Element {
public:
    explicit ListItem(std::string label, bool selected = false)
        : m_label(std::move(label)), m_selected(selected) {
        if (!m_label.empty()) setId("ListItem:" + m_label);
    }

    const char* type() const override { return "ListItem"; }

    ListItem& setLabel(std::string label) {
        m_label = std::move(label);
        return *this;
    }
    ListItem& setSelected(bool selected) {
        m_selected = selected;
        return *this;
    }
    ListItem& setOnClick(std::function<void()> callback) {
        m_onClick = std::move(callback);
        return *this;
    }
    ListItem& setOnDoubleClick(std::function<void()> callback) {
        m_onDoubleClick = std::move(callback);
        return *this;
    }
    /// Called inside an active ImGui drag-drop source. Use
    /// ImGui::SetDragDropPayload() from the callback.
    ListItem& setOnDrag(std::function<void()> callback) {
        m_onDrag = std::move(callback);
        return *this;
    }
    /// Scroll the list so this row is visible (used by keyboard navigation).
    ListItem& setScrollHere(bool scroll = true) {
        m_scrollHere = scroll;
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    bool m_selected = false;
    std::function<void()> m_onClick;
    std::function<void()> m_onDoubleClick;
    std::function<void()> m_onDrag;
    bool m_scrollHere = false;
};

class SegmentedControl : public Element {
public:
    SegmentedControl(std::string id, int* selected, std::vector<std::string> options)
        : m_value(selected), m_options(std::move(options)) {
        setId(std::move(id));
    }

    const char* type() const override { return "SegmentedControl"; }

    SegmentedControl& setOnChange(std::function<void(int)> callback) {
        m_onChange = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    int* m_value = nullptr;
    std::vector<std::string> m_options;
    std::function<void(int)> m_onChange;
};

// ── Inline implementation ──────────────────────────────────────────────────

inline void Button::draw(UIContext& ctx) {
    const ImVec2 size = controlSize(*this);
    const char* label = m_label.c_str();
    const char* tooltip = m_tooltip.empty() ? nullptr : m_tooltip.c_str();

    bool pressed = false;
    switch (m_variant) {
        case ButtonVariant::Primary:
            pressed = PrimaryButton(label, size);
            break;
        case ButtonVariant::Danger:
            pressed = DangerButton(label, size);
            break;
        case ButtonVariant::Toolbar:
            pressed = ToolbarButton(label, tooltip, m_selected, size);
            break;
        case ButtonVariant::Segmented:
            pressed = SegmentedButton(label, m_selected, size, tooltip);
            break;
        case ButtonVariant::IconText:
            pressed = IconTextButton(m_icon.c_str(), label, tooltip, m_selected);
            break;
        case ButtonVariant::Ghost: {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ctx.theme().surfaceOverlay);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ctx.theme().surfaceRaised);
            pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(3);
            break;
        }
        case ButtonVariant::Default:
        default:
            pressed = ImGui::Button(label, size);
            break;
    }

    setPressed(pressed);
    setHovered(ImGui::IsItemHovered());
    setActive(ImGui::IsItemActive());
    setFocused(ImGui::IsItemFocused());
    if (pressed && m_onClick) m_onClick();
    if (tooltip && m_variant != ButtonVariant::Toolbar &&
        m_variant != ButtonVariant::Segmented && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
}

inline void IconButton::draw(UIContext&) {
    float side = 0.0f;
    if (size().width > 0.0f) side = size().width * uiScale();
    else if (size().height > 0.0f) side = size().height * uiScale();
    const bool pressed =
        UIKit::iconButton(id().c_str(), m_icon.c_str(),
                          m_tooltipText.empty() ? nullptr : m_tooltipText.c_str(), side,
                          m_selected);
    setPressed(pressed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (pressed && m_onClick) m_onClick();
}

inline void Checkbox::draw(UIContext&) {
    if (!m_value) return;
    const bool changed = ImGui::Checkbox(m_label.c_str(), m_value);
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange(*m_value);
}

inline void SliderFloat::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed = ImGui::SliderFloat(label, m_value, m_min, m_max, m_format.c_str());
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange(*m_value);
}

inline void SliderInt::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed = ImGui::SliderInt(label, m_value, m_min, m_max);
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange(*m_value);
}

inline void DragFloat::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed =
        ImGui::DragFloat(label, m_value, m_speed, m_min, m_max, m_format.c_str());
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange(*m_value);
}

inline void DragInt::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed =
        ImGui::DragInt(label, m_value, m_speed, m_min, m_max, m_format.c_str());
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange(*m_value);
}

inline void DragVec2::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed = ImGui::DragFloat2(label, &m_value->x, m_speed, m_min, m_max,
                                           m_format.c_str());
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange();
}

inline void DragVec3::draw(UIContext&) {
    if (!m_value) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed = ImGui::DragFloat3(label, &m_value->x, m_speed, m_min, m_max,
                                           m_format.c_str());
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange();
}

inline void Combo::draw(UIContext&) {
    if (!m_value || m_items.empty()) return;
    std::vector<const char*> items;
    items.reserve(m_items.size());
    for (const std::string& item : m_items) items.push_back(item.c_str());

    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed =
        ImGui::Combo(label, m_value, items.data(), static_cast<int>(items.size()));
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange(*m_value);
}

inline void TextInput::draw(UIContext&) {
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    if (m_focusNext) {
        ImGui::SetKeyboardFocusHere();
        m_focusNext = false;
    }

    if (m_string) {
        std::vector<char> buffer(std::max<std::size_t>(m_capacity, 2u), '\0');
        std::snprintf(buffer.data(), buffer.size(), "%s", m_string->c_str());
        bool changed = false;
        if (!m_hint.empty()) {
            changed = ImGui::InputTextWithHint("##value", m_hint.c_str(), buffer.data(),
                                               buffer.size());
        } else {
            changed = ImGui::InputText("##value", buffer.data(), buffer.size());
        }
        if (changed) {
            *m_string = buffer.data();
            setChanged(true);
            if (m_onChange) m_onChange(*m_string);
        }
    } else if (m_buffer && m_capacity > 0) {
        bool changed = false;
        if (!m_hint.empty()) {
            changed = ImGui::InputTextWithHint("##value", m_hint.c_str(), m_buffer,
                                               m_capacity);
        } else {
            changed = ImGui::InputText("##value", m_buffer, m_capacity);
        }
        if (changed) {
            setChanged(true);
            if (m_onChange) m_onChange(m_buffer);
        }
    }

    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
}

inline void PathField::draw(UIContext&) {
    if (!m_path) return;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float browseW = ImGui::CalcTextSize("Browse").x +
                          ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    const float clearW = m_path->empty() ? 0.0f : 22.0f;
    const float buttons = browseW + (clearW > 0.0f ? spacing + clearW : 0.0f);

    char display[256];
    const std::string name =
        m_path->empty() ? std::string("(none)")
                        : std::filesystem::path(*m_path).filename().string();
    std::snprintf(display, sizeof(display), "%s", name.c_str());

    ImGui::SetNextItemWidth(std::max(48.0f, ImGui::GetContentRegionAvail().x - buttons - spacing));
    ImGui::InputText("##pathfield", display, sizeof(display), ImGuiInputTextFlags_ReadOnly);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_payload.c_str())) {
            const char* value = static_cast<const char*>(payload->Data);
            if (value && value[0] != '\0') {
                if (m_onDrop) {
                    m_onDrop(value);
                } else {
                    *m_path = value;
                    if (m_onChange) m_onChange();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button("Browse", ImVec2(browseW, 0.0f)) && m_onBrowse) m_onBrowse();
    if (!m_path->empty()) {
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("X", ImVec2(clearW, 0.0f))) {
            m_path->clear();
            if (m_onChange) m_onChange();
        }
    }
}

inline void DropZone::draw(UIContext&) {
    const ImVec2 size = controlSize(*this);
    const bool pressed = ImGui::Button(
        m_label.c_str(), ImVec2(size.x > 0.0f ? size.x : -1.0f,
                               size.y > 0.0f ? size.y : 0.0f));
    setPressed(pressed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_payload.c_str())) {
            const char* value = static_cast<const char*>(payload->Data);
            if (value && m_onDrop) m_onDrop(value);
        }
        ImGui::EndDragDropTarget();
    }
}

inline void MultilineText::draw(UIContext&) {
    if (!m_buffer || m_capacity == 0) return;
    ImVec2 size = controlSize(*this);
    if (size.x <= 0.0f) size.x = -1.0f;
    if (size.y <= 0.0f) {
        size.y = std::max(ImGui::GetContentRegionAvail().y, 80.0f);
    }
    const bool changed = ImGui::InputTextMultiline("##value", m_buffer, m_capacity, size,
                                                   m_flags);
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
}

inline void ImageButton::draw(UIContext&) {
    const float scale = std::max(uiScale(), 0.001f);
    ImVec2 size = controlSize(*this);
    if (size.x <= 0.0f) size.x = m_logicalSize.x * scale;
    if (size.y <= 0.0f) size.y = m_logicalSize.y * scale;
    const bool pressed = ImGui::ImageButton("##image_button", m_texture, size, m_uv0,
                                            m_uv1, m_background);
    setPressed(pressed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (pressed && m_onClick) m_onClick();
}

inline void ColorPicker::draw(UIContext&) {
    if (!m_color) return;
    const ImVec2 size = controlSize(*this);
    if (size.x > 0.0f) ImGui::SetNextItemWidth(size.x);
    const char* label = m_label.empty() ? "##value" : m_label.c_str();
    const bool changed = m_components == 3
                             ? ImGui::ColorEdit3(label, m_color, m_flags)
                             : ImGui::ColorEdit4(label, m_color, m_flags);
    setChanged(changed);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    if (changed && m_onChange) m_onChange();
}

inline void ListItem::draw(UIContext&) {
    const bool pressed = ImGui::Selectable(
        m_label.c_str(), m_selected,
        ImGuiSelectableFlags_AllowDoubleClick);
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
    setPressed(ImGui::IsItemClicked());
    if (pressed) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_onDoubleClick) {
            m_onDoubleClick();
        } else if (m_onClick) {
            m_onClick();
        }
    }
    if (m_scrollHere) ImGui::SetScrollHereY(0.5f);
    if (m_onDrag && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        m_onDrag();
        ImGui::EndDragDropSource();
    }
    if (!tooltip().empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip().c_str());
    }
}

inline void SegmentedControl::draw(UIContext& ctx) {
    if (!m_value || m_options.empty()) return;

    const float scale = std::max(ctx.scale(), 0.001f);
    float totalWidth = resolvedWidth() * scale;
    if (totalWidth <= 0.0f) totalWidth = ImGui::GetContentRegionAvail().x;
    const float segmentWidth =
        totalWidth / static_cast<float>(m_options.size());

    for (std::size_t i = 0; i < m_options.size(); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushID(static_cast<int>(i));
        const bool selected = *m_value == static_cast<int>(i);
        if (SegmentedButton(m_options[i].c_str(), selected,
                            ImVec2(segmentWidth, size().height > 0.0f
                                                      ? size().height * scale
                                                      : 0.0f))) {
            *m_value = static_cast<int>(i);
            setChanged(true);
            if (m_onChange) m_onChange(*m_value);
        }
        ImGui::PopID();
    }
    setHovered(ImGui::IsItemHovered());
    setFocused(ImGui::IsItemFocused());
}

}  // namespace Caffeine::UIKit::Elements
