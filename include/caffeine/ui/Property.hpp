#pragma once

/**
 * Caffeine UI — property grids.
 *
 * A PropertyGrid is a two-column table (label | editor). PropertyRow owns one
 * value element, so the same grid works for floats, ints, bools, combos, colors,
 * text and any custom element.
 *
 * The typed rows are sugar:
 *   grid.addRow<FloatProperty>("Wind", wind, 0.0f, 60.0f);
 */

#include "caffeine/ui/Container.hpp"
#include "caffeine/ui/Control.hpp"
#include "caffeine/ui/Display.hpp"

#include <imgui.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Caffeine::UIKit::Elements {

class PropertyRow : public Element {
public:
    PropertyRow(std::string label, std::unique_ptr<Element> value,
                std::string tooltip = {})
        : m_label(std::move(label)),
          m_value(std::move(value)),
          m_tooltip(std::move(tooltip)) {
        if (!m_label.empty()) setId("Property:" + m_label);
        if (m_value && m_value->id().empty()) m_value->setId("value");
    }

    const char* type() const override { return "PropertyRow"; }

    const std::string& label() const { return m_label; }
    Element* value() { return m_value.get(); }
    const Element* value() const { return m_value.get(); }

    PropertyRow& setHint(std::string hint) {
        m_tooltip = std::move(hint);
        return *this;
    }

    std::vector<Element*> children() const override {
        return m_value ? std::vector<Element*>{m_value.get()} : std::vector<Element*>{};
    }

    void draw(UIContext& ctx) override;

private:
    std::string m_label;
    std::unique_ptr<Element> m_value;
    std::string m_tooltip;
};

class PropertyGrid : public Container {
public:
    explicit PropertyGrid(std::string id = {}) : Container(Axis::Vertical) {
        if (!id.empty()) setId(std::move(id));
    }

    const char* type() const override { return "PropertyGrid"; }

    PropertyGrid& setLabelWidth(float width) {
        m_labelWidth = width;
        return *this;
    }

    PropertyRow& row(std::string label, std::unique_ptr<Element> value,
                     std::string tooltip = {}) {
        auto child = std::make_unique<PropertyRow>(std::move(label), std::move(value),
                                                   std::move(tooltip));
        PropertyRow& reference = *child;
        add(std::move(child));
        return reference;
    }

    template <typename T, typename... Args>
    T& typedRow(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *child;
        add(std::move(child));
        return reference;
    }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;
    void drawChildren(UIContext& ctx) override;

private:
    float m_labelWidth = 140.0f;
    bool m_open = false;
};

/// Float editor row. `slider` chooses SliderFloat over DragFloat.
class FloatProperty : public PropertyRow {
public:
    FloatProperty(std::string label, float& value, float min, float max,
                  float speed = 0.1f, std::string format = "%.3f",
                  bool slider = false)
        : PropertyRow(std::move(label), makeControl(value, min, max, speed, format, slider)) {}

    const char* type() const override { return "FloatProperty"; }

    FloatProperty& setOnChange(std::function<void(float)> callback) {
        if (auto* slider = dynamic_cast<SliderFloat*>(value())) {
            slider->setOnChange(std::move(callback));
        } else if (auto* drag = dynamic_cast<DragFloat*>(value())) {
            drag->setOnChange(std::move(callback));
        }
        return *this;
    }

private:
    static std::unique_ptr<Element> makeControl(float& value, float min, float max,
                                                float speed, const std::string& format,
                                                bool slider) {
        if (slider) {
            return std::make_unique<SliderFloat>("", &value, min, max, format);
        }
        return std::make_unique<DragFloat>("", &value, speed, min, max, format);
    }
};

class Vec2Property : public PropertyRow {
public:
    Vec2Property(std::string label, Vec2& value, float speed = 0.01f, float min = 0.0f,
                 float max = 0.0f, std::string format = "%.2f")
        : PropertyRow(std::move(label),
                      std::make_unique<DragVec2>("", &value, speed, min, max,
                                                 std::move(format))) {}

    const char* type() const override { return "Vec2Property"; }

    Vec2Property& setOnChange(std::function<void()> callback) {
        if (auto* drag = dynamic_cast<DragVec2*>(value())) {
            drag->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class Vec3Property : public PropertyRow {
public:
    Vec3Property(std::string label, Vec3& value, float speed = 0.01f, float min = 0.0f,
                 float max = 0.0f, std::string format = "%.2f")
        : PropertyRow(std::move(label),
                      std::make_unique<DragVec3>("", &value, speed, min, max,
                                                 std::move(format))) {}

    const char* type() const override { return "Vec3Property"; }

    Vec3Property& setOnChange(std::function<void()> callback) {
        if (auto* drag = dynamic_cast<DragVec3*>(value())) {
            drag->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class IntProperty : public PropertyRow {
public:
    IntProperty(std::string label, int& value, int min, int max, float speed = 1.0f,
                std::string format = "%d", bool slider = false)
        : PropertyRow(std::move(label), makeControl(value, min, max, speed, format, slider)) {}

    const char* type() const override { return "IntProperty"; }

    IntProperty& setOnChange(std::function<void(int)> callback) {
        if (auto* slider = dynamic_cast<SliderInt*>(value())) {
            slider->setOnChange(std::move(callback));
        } else if (auto* drag = dynamic_cast<DragInt*>(value())) {
            drag->setOnChange(std::move(callback));
        }
        return *this;
    }

private:
    static std::unique_ptr<Element> makeControl(int& value, int min, int max, float speed,
                                                const std::string& format, bool slider) {
        if (slider) return std::make_unique<SliderInt>("", &value, min, max);
        return std::make_unique<DragInt>("", &value, speed, min, max, format);
    }
};

class BoolProperty : public PropertyRow {
public:
    BoolProperty(std::string label, bool& value)
        : PropertyRow(std::move(label), std::make_unique<Checkbox>("", &value)) {}

    const char* type() const override { return "BoolProperty"; }

    BoolProperty& setOnChange(std::function<void(bool)> callback) {
        if (auto* check = dynamic_cast<Checkbox*>(value())) {
            check->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class ChoiceProperty : public PropertyRow {
public:
    ChoiceProperty(std::string label, int& value, std::vector<std::string> items)
        : PropertyRow(std::move(label),
                      std::make_unique<Combo>("", &value, std::move(items))) {}

    const char* type() const override { return "ChoiceProperty"; }

    ChoiceProperty& setOnChange(std::function<void(int)> callback) {
        if (auto* combo = dynamic_cast<Combo*>(value())) {
            combo->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class ColorFieldProperty : public PropertyRow {
public:
    ColorFieldProperty(std::string label, float* color,
                       ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs)
        : PropertyRow(std::move(label),
                      std::make_unique<ColorPicker>("", color, flags)) {}

    const char* type() const override { return "ColorFieldProperty"; }

    ColorFieldProperty& setOnChange(std::function<void()> callback) {
        if (auto* picker = dynamic_cast<ColorPicker*>(value())) {
            picker->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class Color3FieldProperty : public PropertyRow {
public:
    Color3FieldProperty(std::string label, float* color,
                        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs)
        : PropertyRow(std::move(label),
                      std::make_unique<ColorPicker>("", color, flags, 3)) {}

    const char* type() const override { return "Color3FieldProperty"; }

    Color3FieldProperty& setOnChange(std::function<void()> callback) {
        if (auto* picker = dynamic_cast<ColorPicker*>(value())) {
            picker->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class TextFieldProperty : public PropertyRow {
public:
    TextFieldProperty(std::string label, std::string& value,
                      std::size_t capacity = 512)
        : PropertyRow(std::move(label),
                      std::make_unique<TextInput>(value, capacity)) {}

    const char* type() const override { return "TextFieldProperty"; }

    TextFieldProperty& setOnChange(std::function<void(const std::string&)> callback) {
        if (auto* input = dynamic_cast<TextInput*>(value())) {
            input->setOnChange(std::move(callback));
        }
        return *this;
    }
};

class BufferProperty : public PropertyRow {
public:
    BufferProperty(std::string label, char* buffer, std::size_t size)
        : PropertyRow(std::move(label),
                      std::make_unique<TextInput>(buffer, size)) {}

    const char* type() const override { return "BufferProperty"; }
};

class PathProperty : public PropertyRow {
public:
    PathProperty(std::string label, std::string& value)
        : PropertyRow(std::move(label), std::make_unique<PathField>(value)) {}

    const char* type() const override { return "PathProperty"; }

    PathProperty& setOnBrowse(std::function<void()> callback) {
        if (auto* field = dynamic_cast<PathField*>(value())) {
            field->setOnBrowse(std::move(callback));
        }
        return *this;
    }
    PathProperty& setOnDrop(std::function<void(const char*)> callback) {
        if (auto* field = dynamic_cast<PathField*>(value())) {
            field->setOnDrop(std::move(callback));
        }
        return *this;
    }
    PathProperty& setOnChange(std::function<void()> callback) {
        if (auto* field = dynamic_cast<PathField*>(value())) {
            field->setOnChange(std::move(callback));
        }
        return *this;
    }
};

// ── Inline implementation ──────────────────────────────────────────────────

inline void PropertyRow::draw(UIContext& ctx) {
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(m_label.c_str());
    if (!m_tooltip.empty()) UIKit::HelpMarker(m_tooltip.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(m_label.c_str());
    if (m_value) {
        ctx.drawElement(*m_value);
        setChanged(m_value->changed());
        setHovered(m_value->hovered());
        setFocused(m_value->focused());
    }
    ImGui::PopID();
}

inline bool PropertyGrid::beginBody(UIContext& ctx) {
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_NoPadOuterX |
                                  ImGuiTableFlags_NoBordersInBody;
    m_open = ImGui::BeginTable("##property_grid", 2, flags);
    if (m_open) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed,
                                m_labelWidth * std::max(ctx.scale(), 0.001f));
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    }
    return m_open;
}

inline void PropertyGrid::endBody(UIContext&) {
    // ImGui requires EndTable() even when BeginTable() returned false.
    ImGui::EndTable();
}

inline void PropertyGrid::drawChildren(UIContext& ctx) {
    for (auto& child : m_children) {
        if (!child || !child->visible()) continue;
        ImGui::TableNextRow();
        ctx.drawElement(*child);
    }
}

}  // namespace Caffeine::UIKit::Elements
