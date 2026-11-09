#pragma once

/**
 * Caffeine UI — containers and layout.
 *
 * Containers are Elements that own children. The main hierarchy is:
 *
 *   Panel  (docked window)
 *     Card (raised surface / child window)
 *       Row / Column / Grid / Toolbar / Section / ScrollArea
 *         control and display elements
 *
 * Layout is intentionally simple and predictable: vertical stacks, horizontal
 * rows with optional grow columns, two-column property grids, cards that auto
 * size to their content and collapsible sections. Complex layout belongs in a
 * container, never in a leaf control.
 */

#include "caffeine/ui/Element.hpp"
#include "caffeine/ui/Widgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Caffeine::UIKit::Elements {

/// Generic container. `Box` is a friendly alias for the common case.
class Container : public Element {
public:
    explicit Container(Axis axis = Axis::Vertical) : m_axis(axis) {}

    Container& setAxis(Axis axis) {
        m_axis = axis;
        return *this;
    }
    Axis axis() const { return m_axis; }

    Container& setGap(float gap) {
        m_gap = gap;
        return *this;
    }
    float gap() const { return m_gap; }

    Container& setScroll(bool scroll) {
        m_scroll = scroll;
        return *this;
    }

    /// Adds a child. Empty ids are auto-assigned as "<Type>#<index>".
    Element& add(std::unique_ptr<Element> child);
    Element& add(Element& child) = delete;  // ownership is explicit
    void clear() { m_children.clear(); }

    template <typename T, typename... Args>
    T& emplace(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *child;
        add(std::move(child));
        return reference;
    }

    const char* type() const override { return "Container"; }
    bool isContainer() const override { return true; }
    std::vector<Element*> children() const override;

    void draw(UIContext& ctx) override;

protected:
    /// Begin/end the container surface. beginBody must always be paired with
    /// endBody, even when it returns false (ImGui child/table rules).
    virtual bool beginBody(UIContext& ctx) {
        (void)ctx;
        return true;
    }
    virtual void endBody(UIContext& ctx) { (void)ctx; }

    virtual void drawChildren(UIContext& ctx);

    std::vector<std::unique_ptr<Element>> m_children;
    Axis m_axis = Axis::Vertical;
    float m_gap = -1.0f;
    bool m_scroll = false;
};

/// Friendly name for a plain vertical/horizontal box.
class Box : public Container {
public:
    explicit Box(Axis axis = Axis::Vertical) : Container(axis) {}
    const char* type() const override { return "Box"; }
};

class Column : public Container {
public:
    explicit Column(std::string id = {}) : Container(Axis::Vertical) {
        if (!id.empty()) setId(std::move(id));
    }
    const char* type() const override { return "Column"; }

protected:
    void drawChildren(UIContext& ctx) override {
        const float available = ImGui::GetContentRegionAvail().x;
        const float logicalWidth = available / std::max(ctx.scale(), 0.001f);
        for (auto& child : m_children) {
            if (!child || !child->visible()) continue;
            if (child->grow() && child->size().width <= 0.0f) {
                child->setComputedWidth(logicalWidth);
            }
        }
        Container::drawChildren(ctx);
    }
};

class Row : public Container {
public:
    explicit Row(std::string id = {}) : Container(Axis::Horizontal) {
        if (!id.empty()) setId(std::move(id));
    }
    const char* type() const override { return "Row"; }

protected:
    void drawChildren(UIContext& ctx) override {
        const float scale = std::max(ctx.scale(), 0.001f);
        const float available = ImGui::GetContentRegionAvail().x;
        const float gap = (m_gap >= 0.0f ? m_gap : ctx.theme().spacingX) * scale;

        float fixed = 0.0f;
        int visible = 0;
        int growCount = 0;
        for (auto& child : m_children) {
            if (!child || !child->visible()) continue;
            ++visible;
            if (child->size().width > 0.0f) {
                fixed += child->size().width * scale;
            } else if (child->grow()) {
                ++growCount;
            }
        }
        const float gaps = static_cast<float>(std::max(visible - 1, 0)) * gap;
        const float remaining = std::max(0.0f, available - fixed - gaps);
        const float share = growCount > 0 ? remaining / static_cast<float>(growCount) : 0.0f;
        for (auto& child : m_children) {
            if (!child || !child->visible()) continue;
            if (child->size().width > 0.0f) {
                child->setComputedWidth(0.0f);
            } else if (child->grow()) {
                child->setComputedWidth(share / scale);
            }
        }
        Container::drawChildren(ctx);
    }
};

/// Raised surface used to group related controls.
class Card : public Container {
public:
    explicit Card(std::string id = {}) : Container(Axis::Vertical) {
        if (!id.empty()) setId(std::move(id));
    }

    const char* type() const override { return "Card"; }

    Card& setTitle(std::string title) {
        m_title = std::move(title);
        return *this;
    }
    const std::string& title() const { return m_title; }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;

private:
    std::string m_title;
};

/// Top-level docked window. A Panel is the root of a panel tree.
class Panel : public Container {
public:
    explicit Panel(std::string title, std::string subtitle = {}, std::string icon = {},
                   bool* open = nullptr)
        : Container(Axis::Vertical),
          m_title(std::move(title)),
          m_subtitle(std::move(subtitle)),
          m_icon(std::move(icon)),
          m_open(open) {
        setId(m_title);
    }

    const char* type() const override { return "Panel"; }
    const std::string& title() const { return m_title; }
    Panel& setSubtitle(std::string subtitle) {
        m_subtitle = std::move(subtitle);
        return *this;
    }
    Panel& setIcon(std::string icon) {
        m_icon = std::move(icon);
        return *this;
    }
    Panel& setOpenPtr(bool* open) {
        m_open = open;
        return *this;
    }
    Panel& setWindowFlags(ImGuiWindowFlags flags) {
        m_windowFlags = flags;
        return *this;
    }
    Panel& setInitialSize(ImVec2 size) {
        m_initialSize = size;
        return *this;
    }
    /// Called after ImGui::Begin succeeds and before the panel header/content.
    Panel& setOnBegin(std::function<void()> callback) {
        m_onBegin = std::move(callback);
        return *this;
    }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;

private:
    std::string m_title;
    std::string m_subtitle;
    std::string m_icon;
    bool* m_open = nullptr;
    ImGuiWindowFlags m_windowFlags = ImGuiWindowFlags_None;
    ImVec2 m_initialSize{0.0f, 0.0f};
    std::function<void()> m_onBegin;
};

/// Collapsible section with indented children.
class Section : public Container {
public:
    explicit Section(std::string title, bool defaultOpen = true)
        : Container(Axis::Vertical), m_title(std::move(title)), m_defaultOpen(defaultOpen) {
        setId("Section:" + m_title);
    }
    const char* type() const override { return "Section"; }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;

private:
    std::string m_title;
    bool m_defaultOpen = true;
    bool m_open = false;
};

/// Horizontal strip with the standard toolbar height.
class Toolbar : public Row {
public:
    explicit Toolbar(std::string id = {}) : Row(std::move(id)) { setGap(4.0f); }
    const char* type() const override { return "Toolbar"; }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;
};

/// Vertical scrollable region.
class ScrollArea : public Column {
public:
    explicit ScrollArea(std::string id = {}) : Column(std::move(id)) {}
    const char* type() const override { return "ScrollArea"; }

    ScrollArea& setScrollToBottom(bool scroll = true) {
        m_scrollToBottom = scroll;
        return *this;
    }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;

private:
    bool m_scrollToBottom = false;
};

/// Scrollable list container. Add ListItem children.
class ListBox : public ScrollArea {
public:
    explicit ListBox(std::string id = {}) : ScrollArea(std::move(id)) {}
    const char* type() const override { return "ListBox"; }
};

/// Table-backed grid. Children fill the cells left to right.
class Grid : public Container {
public:
    explicit Grid(int columns = 2) : Container(Axis::Vertical), m_columns(columns) {}

    const char* type() const override { return "Grid"; }
    Grid& setColumns(int columns) {
        m_columns = std::max(columns, 1);
        return *this;
    }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;
    void drawChildren(UIContext& ctx) override;

private:
    int m_columns = 2;
};

/// Tab bar. Add TabPage children.
class TabPage : public Container {
public:
    explicit TabPage(std::string title) : Container(Axis::Vertical), m_title(std::move(title)) {
        setId("Tab:" + m_title);
    }
    const char* type() const override { return "TabPage"; }
    const std::string& title() const { return m_title; }
    void draw(UIContext& ctx) override { drawChildren(ctx); }

private:
    std::string m_title;
};

class TabBar : public Container {
public:
    explicit TabBar(std::string id = {}) : Container(Axis::Vertical) {
        setId(std::move(id));
    }
    const char* type() const override { return "TabBar"; }

protected:
    bool beginBody(UIContext& ctx) override;
    void endBody(UIContext& ctx) override;
    void drawChildren(UIContext& ctx) override;
};

// ── Inline implementation ──────────────────────────────────────────────────

inline Element& Container::add(std::unique_ptr<Element> child) {
    if (!child) return *this;
    if (child->id().empty()) {
        child->setId(std::string(child->type()) + "#" + std::to_string(m_children.size()));
    }
    Element& reference = *child;
    m_children.push_back(std::move(child));
    return reference;
}

inline std::vector<Element*> Container::children() const {
    std::vector<Element*> result;
    result.reserve(m_children.size());
    for (const auto& child : m_children) result.push_back(child.get());
    return result;
}

inline void Container::draw(UIContext& ctx) {
    const bool open = beginBody(ctx);
    if (open) drawChildren(ctx);
    endBody(ctx);
}

inline void Container::drawChildren(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    const float gap = (m_gap >= 0.0f ? m_gap
                                     : (m_axis == Axis::Horizontal ? ctx.theme().spacingX
                                                                   : ctx.theme().spacingY)) *
                      scale;
    bool first = true;
    for (auto& child : m_children) {
        if (!child || !child->visible()) continue;

        if (!first) {
            if (m_axis == Axis::Horizontal) {
                ImGui::SameLine(0.0f, gap);
            } else {
                ImGui::Dummy(ImVec2(0.0f, gap));
            }
        }
        first = false;

        if (m_axis == Axis::Vertical && child->margin().top > 0.0f) {
            ImGui::Dummy(ImVec2(0.0f, child->margin().top * scale));
        } else if (m_axis == Axis::Horizontal && child->margin().left > 0.0f) {
            ImGui::SameLine(0.0f, child->margin().left * scale);
        }

        const float width = child->resolvedWidth();
        if (width > 0.0f) ImGui::SetNextItemWidth(width * scale);
        ctx.drawElement(*child);

        if (m_axis == Axis::Vertical && child->margin().bottom > 0.0f) {
            ImGui::Dummy(ImVec2(0.0f, child->margin().bottom * scale));
        }
    }
}

inline bool Card::beginBody(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    const float width = resolvedWidth() * scale;
    const float height = size().height > 0.0f ? size().height * scale : 0.0f;

    const ImVec4 background =
        style().background.value_or(ctx.theme().surfaceRaised);
    const ImVec4 border = style().border.value_or(ctx.theme().border);
    const float rounding = style().rounding >= 0.0f ? style().rounding : ctx.theme().rounding;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ctx.theme().paddingX * scale, ctx.theme().paddingY * scale));

    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (height <= 0.0f) flags |= ImGuiChildFlags_AutoResizeY;
    const bool open = ImGui::BeginChild("##card_body", ImVec2(width, height), flags);
    if (open && !m_title.empty()) {
        ImGui::TextColored(ctx.theme().textMuted, "%s", m_title.c_str());
        ImGui::Separator();
    }
    return open;
}

inline void Card::endBody(UIContext&) {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

inline bool Panel::beginBody(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f * scale, 120.0f * scale),
                                        ImVec2(FLT_MAX, FLT_MAX));
    if (m_initialSize.x > 0.0f && m_initialSize.y > 0.0f) {
        ImGui::SetNextWindowSize(
            ImVec2(m_initialSize.x * scale, m_initialSize.y * scale),
            ImGuiCond_FirstUseEver);
    }
    const bool open = ImGui::Begin(m_title.c_str(), m_open, m_windowFlags);
    if (open) {
        if (m_onBegin) m_onBegin();
        UIKit::PanelHeader(m_title.c_str(),
                           m_subtitle.empty() ? nullptr : m_subtitle.c_str(),
                           m_icon.empty() ? nullptr : m_icon.c_str());
    }
    return open;
}

inline void Panel::endBody(UIContext&) { ImGui::End(); }

inline bool Section::beginBody(UIContext& ctx) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    m_open = ImGui::CollapsingHeader(m_title.c_str(), flags);
    if (m_open) ImGui::Indent(ctx.theme().indentWidth * std::max(ctx.scale(), 0.001f));
    return m_open;
}

inline void Section::endBody(UIContext& ctx) {
    if (m_open) ImGui::Unindent(ctx.theme().indentWidth * std::max(ctx.scale(), 0.001f));
}

inline bool Toolbar::beginBody(UIContext& ctx) {
    const float height = ctx.theme().toolbarHeight * std::max(ctx.scale(), 0.001f);
    return ImGui::BeginChild("##toolbar_body", ImVec2(0.0f, height), ImGuiChildFlags_None,
                             ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse);
}

inline void Toolbar::endBody(UIContext&) { ImGui::EndChild(); }

inline bool ScrollArea::beginBody(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    const float width = resolvedWidth() * scale;
    const float height = size().height > 0.0f ? size().height * scale : 0.0f;
    return ImGui::BeginChild("##scroll_body", ImVec2(width, height),
                             ImGuiChildFlags_Borders);
}

inline void ScrollArea::endBody(UIContext&) {
    if (m_scrollToBottom) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

inline bool Grid::beginBody(UIContext&) {
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("##grid_body", m_columns, flags)) return false;
    for (int i = 0; i < m_columns; ++i) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
    }
    return true;
}

inline void Grid::endBody(UIContext&) { ImGui::EndTable(); }

inline void Grid::drawChildren(UIContext& ctx) {
    for (auto& child : m_children) {
        if (!child || !child->visible()) continue;
        ImGui::TableNextColumn();
        ctx.drawElement(*child);
    }
}

inline bool TabBar::beginBody(UIContext&) { return ImGui::BeginTabBar("##tab_bar"); }

inline void TabBar::endBody(UIContext&) { ImGui::EndTabBar(); }

inline void TabBar::drawChildren(UIContext& ctx) {
    for (auto& child : m_children) {
        if (!child || !child->visible()) continue;
        auto* page = dynamic_cast<TabPage*>(child.get());
        if (!page) continue;
        if (ImGui::BeginTabItem(page->title().c_str())) {
            ctx.drawElement(*page);
            ImGui::EndTabItem();
        }
    }
}

}  // namespace Caffeine::UIKit::Elements
