#pragma once

/**
 * Caffeine UI — composable element model.
 *
 * Everything in the editor UI is an Element: buttons, sliders, labels, cards,
 * panels, canvases and nodes. Elements can contain other elements, so a panel is
 * just a tree of small pieces. The model is immediate-mode underneath (each
 * element draws with ImGui every frame) but the tree is retained, which gives
 * stable ids, easy debugging and a simple plugin API.
 *
 * Namespace: Caffeine::UIKit::Elements
 *   Elements::Panel
 *     └─ Elements::Card
 *          ├─ Elements::Row
 *          │    ├─ Elements::Button
 *          │    └─ Elements::Label
 *          └─ Elements::PropertyGrid
 *
 * The legacy free functions in Widgets.hpp remain available. New code should
 * compose elements; Widgets.hpp is the low-level vocabulary the elements use.
 */

#include "caffeine/ui/Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Caffeine::UIKit {
class NodeCanvas;
}

namespace Caffeine::UIKit::Elements {

class Element;
class UIContext;

/// Logical padding/margin in UI pixels (multiplied by uiScale() at draw time).
struct Insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    Insets() = default;
    explicit Insets(float all) : left(all), top(all), right(all), bottom(all) {}
    Insets(float horizontal, float vertical)
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
    Insets(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
};

/// Preferred size. Zero means "auto"; grow() lets a Row share leftover space.
struct Size {
    float width = 0.0f;
    float height = 0.0f;
    float minWidth = 0.0f;
    float maxWidth = 0.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
};

/// Optional visual overrides. Unset values fall back to the active theme.
struct Style {
    std::optional<ImVec4> background;
    std::optional<ImVec4> border;
    std::optional<ImVec4> text;
    float rounding = -1.0f;
    float borderThickness = -1.0f;
    float opacity = 1.0f;
};

enum class Align { Start, Center, End, Stretch };
enum class Axis { Vertical, Horizontal };

/// Per-frame state recorded by UIContext so hosts can react after draw().
struct ElementState {
    bool hovered = false;
    bool active = false;
    bool pressed = false;
    bool changed = false;
    bool focused = false;
    bool enabled = true;
    bool visible = true;
    int depth = 0;
    std::string path;
    ImVec2 min{0.0f, 0.0f};
    ImVec2 max{0.0f, 0.0f};
};

/// One row in the debug snapshot (see UIContext::debugDump()).
struct DebugEntry {
    std::string path;
    std::string type;
    std::string id;
    int depth = 0;
    bool visible = true;
    bool enabled = true;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    ImVec2 min{0.0f, 0.0f};
    ImVec2 max{0.0f, 0.0f};
};

/// Owns the draw traversal, id stack and debug snapshot.
///
/// One context can draw many trees; it is cheap and stateless between frames.
/// It does not create an ImGui context: the host must be inside NewFrame().
class UIContext {
public:
    UIContext() = default;
    ~UIContext() = default;

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;

    bool available() const { return ImGui::GetCurrentContext() != nullptr; }

    /// Clears the frame snapshot and draws the whole tree.
    void draw(Element& root);

    /// Draws one element (normally called by containers). Handles ids, disabled
    /// state and the debug snapshot.
    void drawElement(Element& element);

    const ThemeTokens& theme() const { return UIKit::theme(); }
    float scale() const { return uiScale(); }

    void pushId(const std::string& id);
    void popId();
    std::string path() const;

    void setNodeCanvas(Caffeine::UIKit::NodeCanvas* canvas) { m_nodeCanvas = canvas; }
    Caffeine::UIKit::NodeCanvas* nodeCanvas() const { return m_nodeCanvas; }

    void setDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
    bool debugEnabled() const { return m_debugEnabled; }

    const std::vector<DebugEntry>& debugEntries() const { return m_debugEntries; }
    const std::vector<DebugEntry>& previousDebugEntries() const {
        return m_previousDebugEntries;
    }

    /// Human-readable tree with ids, visibility and bounds.
    std::string debugDump(bool previous = false) const;

    /// Draws hit rectangles + type names. Pass ImGui::GetForegroundDrawList().
    void drawDebugOverlay(ImDrawList* drawList, bool previous = false) const;

private:
    friend class Element;

    std::vector<std::string> m_idStack;
    std::vector<DebugEntry> m_debugEntries;
    std::vector<DebugEntry> m_previousDebugEntries;
    Caffeine::UIKit::NodeCanvas* m_nodeCanvas = nullptr;
    int m_depth = 0;
    bool m_debugEnabled = true;
};

/// Base object. Subclasses implement type() and draw().
class Element {
public:
    virtual ~Element() = default;

    Element& setId(std::string id) {
        m_id = std::move(id);
        return *this;
    }
    const std::string& id() const { return m_id; }

    Element& setVisible(bool visible) {
        m_visible = visible;
        return *this;
    }
    bool visible() const { return m_visible; }

    Element& setEnabled(bool enabled) {
        m_enabled = enabled;
        return *this;
    }
    bool enabled() const { return m_enabled; }

    Element& setTooltip(std::string tooltip) {
        m_tooltip = std::move(tooltip);
        return *this;
    }
    const std::string& tooltip() const { return m_tooltip; }

    Element& setSize(const Size& size) {
        m_size = size;
        return *this;
    }
    const Size& size() const { return m_size; }
    Element& setWidth(float width) {
        m_size.width = width;
        return *this;
    }
    Element& setHeight(float height) {
        m_size.height = height;
        return *this;
    }
    Element& setMinWidth(float width) {
        m_size.minWidth = width;
        return *this;
    }
    Element& setMaxWidth(float width) {
        m_size.maxWidth = width;
        return *this;
    }
    Element& setGrow(bool grow = true) {
        m_grow = grow;
        return *this;
    }
    bool grow() const { return m_grow; }

    Element& setAlign(Align align) {
        m_align = align;
        return *this;
    }
    Align align() const { return m_align; }

    Element& setMargin(const Insets& margin) {
        m_margin = margin;
        return *this;
    }
    const Insets& margin() const { return m_margin; }

    Element& setPadding(const Insets& padding) {
        m_padding = padding;
        m_hasPadding = true;
        return *this;
    }
    const Insets& padding() const { return m_padding; }
    bool hasPadding() const { return m_hasPadding; }

    Element& setStyle(const Style& style) {
        m_style = style;
        return *this;
    }
    const Style& style() const { return m_style; }

    /// Set by the parent layout before draw; 0 means auto.
    void setComputedWidth(float width) { m_computedWidth = width; }
    float computedWidth() const { return m_computedWidth; }
    /// Logical width to use for controls: explicit width, else parent width.
    float resolvedWidth() const {
        return m_size.width > 0.0f ? m_size.width : m_computedWidth;
    }

    virtual const char* type() const = 0;
    virtual void draw(UIContext& ctx) = 0;

    virtual bool isContainer() const { return false; }
    virtual std::vector<Element*> children() const { return {}; }

    /// Searches this subtree by id (depth-first).
    virtual Element* find(const std::string& id);
    /// Appends a readable tree description (used by tests and the debug panel).
    virtual void describe(std::string& out, int depth = 0) const;

    const ElementState& state() const { return m_state; }
    bool hovered() const { return m_state.hovered; }
    bool pressed() const { return m_state.pressed; }
    bool changed() const { return m_state.changed; }
    bool focused() const { return m_state.focused; }

protected:
    friend class UIContext;

    void setHovered(bool value) { m_state.hovered = value; }
    void setActive(bool value) { m_state.active = value; }
    void setPressed(bool value) { m_state.pressed = value; }
    void setChanged(bool value) { m_state.changed = value; }
    void setFocused(bool value) { m_state.focused = value; }

    ElementState m_state;
    std::string m_id;
    std::string m_tooltip;
    Size m_size;
    Insets m_margin;
    Insets m_padding;
    Style m_style;
    bool m_visible = true;
    bool m_enabled = true;
    bool m_grow = false;
    bool m_hasPadding = false;
    Align m_align = Align::Stretch;
    float m_computedWidth = 0.0f;
};

/// Resolves an element's logical size to ImGui pixels. Shared by controls and
/// display elements.
inline ImVec2 controlSize(const Element& element, float fallbackHeight = 0.0f) {
    const float scale = std::max(uiScale(), 0.001f);
    const float width = element.resolvedWidth();
    const float height = element.size().height;
    return ImVec2(width > 0.0f ? width * scale : 0.0f,
                  height > 0.0f ? height * scale : fallbackHeight);
}

// ── Inline implementation ──────────────────────────────────────────────────

inline Element* Element::find(const std::string& target) {
    if (m_id == target) return this;
    for (Element* child : children()) {
        if (!child) continue;
        if (Element* found = child->find(target)) return found;
    }
    return nullptr;
}

inline void Element::describe(std::string& out, int depth) const {
    out.append(static_cast<std::size_t>(std::max(depth, 0)) * 2, ' ');
    out += type();
    if (!m_id.empty()) {
        out += " #";
        out += m_id;
    }
    if (!m_visible) out += " (hidden)";
    if (!m_enabled) out += " (disabled)";
    out += '\n';
    for (Element* child : children()) {
        if (child) child->describe(out, depth + 1);
    }
}

inline void UIContext::pushId(const std::string& id) {
    const std::string scope = id.empty() ? std::string("element") : id;
    m_idStack.push_back(scope);
    ImGui::PushID(scope.c_str());
}

inline void UIContext::popId() {
    ImGui::PopID();
    if (!m_idStack.empty()) m_idStack.pop_back();
}

inline std::string UIContext::path() const {
    std::string result;
    for (std::size_t i = 0; i < m_idStack.size(); ++i) {
        if (i > 0) result += '/';
        result += m_idStack[i];
    }
    return result;
}

inline void UIContext::draw(Element& root) {
    if (!available()) return;

    m_previousDebugEntries = m_debugEntries;
    m_debugEntries.clear();
    m_idStack.clear();
    m_depth = 0;
    m_nodeCanvas = nullptr;

    drawElement(root);
    m_idStack.clear();
}

inline void UIContext::drawElement(Element& element) {
    if (!element.visible()) return;

    const std::string elementId =
        element.id().empty() ? std::string(element.type()) : element.id();
    pushId(elementId);
    ++m_depth;

    element.m_state = ElementState{};
    element.m_state.path = path();
    element.m_state.enabled = element.enabled();
    element.m_state.visible = true;

    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (element.enabled()) {
        element.draw(*this);
    } else {
        ImGui::BeginDisabled();
        element.draw(*this);
        ImGui::EndDisabled();
    }
    const ImVec2 end = ImGui::GetCursorScreenPos();

    element.m_state.min = start;
    element.m_state.max = end;
    element.m_state.depth = m_depth - 1;

    // Leaf controls set hovered/pressed while drawing. Containers get a
    // best-effort fallback from the last submitted item.
    if (!element.isContainer()) {
        element.m_state.hovered = element.m_state.hovered || ImGui::IsItemHovered();
        element.m_state.focused = element.m_state.focused || ImGui::IsItemFocused();
    }

    if (m_debugEnabled) {
        DebugEntry entry;
        entry.path = element.m_state.path;
        entry.type = element.type();
        entry.id = elementId;
        entry.depth = m_depth - 1;
        entry.visible = element.visible();
        entry.enabled = element.enabled();
        entry.hovered = element.m_state.hovered;
        entry.pressed = element.m_state.pressed;
        entry.focused = element.m_state.focused;
        entry.min = start;
        entry.max = end;
        m_debugEntries.push_back(std::move(entry));
    }

    --m_depth;
    popId();
}

inline std::string UIContext::debugDump(bool previous) const {
    const std::vector<DebugEntry>& entries =
        previous ? m_previousDebugEntries : m_debugEntries;
    std::string out;
    for (const DebugEntry& entry : entries) {
        out.append(static_cast<std::size_t>(std::max(entry.depth, 0)) * 2, ' ');
        out += entry.type;
        out += " #";
        out += entry.id;
        if (!entry.visible) out += " hidden";
        if (!entry.enabled) out += " disabled";
        if (entry.hovered) out += " hovered";
        if (entry.pressed) out += " pressed";
        if (entry.focused) out += " focused";
        out += " [";
        out += std::to_string(static_cast<int>(entry.max.x - entry.min.x));
        out += "x";
        out += std::to_string(static_cast<int>(entry.max.y - entry.min.y));
        out += "]\n";
    }
    return out;
}

inline void UIContext::drawDebugOverlay(ImDrawList* drawList, bool previous) const {
    if (!drawList) return;
    const std::vector<DebugEntry>& entries =
        previous ? m_previousDebugEntries : m_debugEntries;
    for (const DebugEntry& entry : entries) {
        const ImU32 color = entry.hovered ? IM_COL32(255, 205, 80, 230)
                                          : IM_COL32(90, 175, 255, 120);
        drawList->AddRect(entry.min, entry.max, color);
        drawList->AddText(ImVec2(entry.min.x + 2.0f, entry.min.y + 1.0f), color,
                          entry.type.c_str());
    }
}

}  // namespace Caffeine::UIKit::Elements
