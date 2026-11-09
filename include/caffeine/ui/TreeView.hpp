#pragma once

/**
 * Caffeine UI — tree views.
 *
 * TreeView is a scrollable container of TreeItem elements. A TreeItem is a
 * branch when it owns children (or setBranch(true)), otherwise it behaves as a
 * selectable leaf. Selection, tooltips, context menus, drag payloads and drop
 * targets are first-class properties, so Hierarchy/Asset Browser/Dryad Nodes can
 * share the same behaviour.
 */

#include "caffeine/ui/Container.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <utility>

namespace Caffeine::UIKit::Elements {

class TreeView : public ScrollArea {
public:
    explicit TreeView(std::string id = {}) : ScrollArea(std::move(id)) {}
    const char* type() const override { return "TreeView"; }

protected:
    void drawChildren(UIContext& ctx) override {
        for (auto& child : m_children) {
            if (child && child->visible()) ctx.drawElement(*child);
        }
    }
};

class TreeItem : public Container {
public:
    explicit TreeItem(std::string label, bool defaultOpen = false)
        : Container(Axis::Vertical), m_label(std::move(label)), m_defaultOpen(defaultOpen) {
        if (!m_label.empty()) setId("TreeItem:" + m_label);
    }

    const char* type() const override { return "TreeItem"; }

    TreeItem& setLabel(std::string label) {
        m_label = std::move(label);
        return *this;
    }
    TreeItem& setSelected(bool selected) {
        m_selected = selected;
        return *this;
    }
    TreeItem& setDefaultOpen(bool open) {
        m_defaultOpen = open;
        return *this;
    }
    /// Treat an item without children as a branch (e.g. an empty category).
    TreeItem& setBranch(bool branch) {
        m_forceBranch = branch;
        return *this;
    }
    TreeItem& setOnClick(std::function<void()> callback) {
        m_onClick = std::move(callback);
        return *this;
    }
    TreeItem& setOnDoubleClick(std::function<void()> callback) {
        m_onDoubleClick = std::move(callback);
        return *this;
    }
    /// Called inside an active context-menu popup; add ImGui::MenuItem here.
    TreeItem& setOnContextMenu(std::function<void()> callback) {
        m_onContextMenu = std::move(callback);
        return *this;
    }
    /// Called inside an active drag source. Use ImGui::SetDragDropPayload().
    TreeItem& setOnDrag(std::function<void()> callback) {
        m_onDrag = std::move(callback);
        return *this;
    }
    /// Convenience drag source: payload type + null-terminated string data.
    TreeItem& setDragPayload(std::string type, std::string data,
                             std::string preview = {}) {
        m_dragType = std::move(type);
        m_dragData = std::move(data);
        m_dragPreview = std::move(preview);
        return *this;
    }
    TreeItem& setAcceptDrop(std::string payload,
                            std::function<void(const char*)> callback) {
        m_dropPayload = std::move(payload);
        m_onDrop = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    void drawItemBehaviour();
    void drawDragDrop();

    std::string m_label;
    bool m_selected = false;
    bool m_defaultOpen = false;
    bool m_forceBranch = false;
    std::function<void()> m_onClick;
    std::function<void()> m_onDoubleClick;
    std::function<void()> m_onContextMenu;
    std::function<void()> m_onDrag;
    std::string m_dragType;
    std::string m_dragData;
    std::string m_dragPreview;
    std::string m_dropPayload;
    std::function<void(const char*)> m_onDrop;
};

inline void TreeItem::drawDragDrop() {
    if (!m_dragType.empty() &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const std::string& data = m_dragData;
        ImGui::SetDragDropPayload(m_dragType.c_str(), data.c_str(), data.size() + 1);
        if (!m_dragPreview.empty()) ImGui::TextUnformatted(m_dragPreview.c_str());
        ImGui::EndDragDropSource();
    } else if (m_onDrag &&
               ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        m_onDrag();
        ImGui::EndDragDropSource();
    }

    if (!m_dropPayload.empty() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(m_dropPayload.c_str())) {
            const char* value = static_cast<const char*>(payload->Data);
            if (value && m_onDrop) m_onDrop(value);
        }
        ImGui::EndDragDropTarget();
    }
}

inline void TreeItem::drawItemBehaviour() {
    if (!tooltip().empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip().c_str());
    }
    if (m_onContextMenu && ImGui::BeginPopupContextItem()) {
        m_onContextMenu();
        ImGui::EndPopup();
    }
    drawDragDrop();
}

inline void TreeItem::draw(UIContext& ctx) {
    const bool branch = m_forceBranch || !m_children.empty();
    if (branch) {
        if (m_defaultOpen) ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (m_forceBranch) flags |= ImGuiTreeNodeFlags_OpenOnArrow;
        const bool open = ImGui::TreeNodeEx("##tree_node", flags, "%s", m_label.c_str());
        setHovered(ImGui::IsItemHovered());
        setFocused(ImGui::IsItemFocused());
        if (ImGui::IsItemClicked()) {
            setPressed(true);
            if (m_onClick) m_onClick();
        }
        drawItemBehaviour();
        if (open) {
            for (auto& child : m_children) {
                if (child && child->visible()) ctx.drawElement(*child);
            }
            ImGui::TreePop();
        }
        return;
    }

    const bool pressed =
        ImGui::Selectable(m_label.c_str(), m_selected,
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
    drawItemBehaviour();
}

}  // namespace Caffeine::UIKit::Elements
