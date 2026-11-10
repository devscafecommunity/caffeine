#pragma once

/**
 * Caffeine UI — data tables.
 *
 * DataTable owns the ImGui table, header, selection and row hitbox. DataRow
 * owns the cell strings and optional tree indentation/expand arrow, so hosts
 * can rebuild rows each frame from their own data model.
 */

#include "caffeine/ui/Container.hpp"
#include "caffeine/ui/Display.hpp"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Caffeine::UIKit::Elements {

struct DataColumn {
    std::string title;
    float width = 0.0f;
    bool stretch = true;
};

class DataRow : public Element {
public:
    explicit DataRow(std::vector<std::string> cells)
        : m_cells(std::move(cells)) {}

    const char* type() const override { return "DataRow"; }
    const std::vector<std::string>& cells() const { return m_cells; }

    DataRow& setIndent(int levels) {
        m_indent = std::max(levels, 0);
        return *this;
    }
    DataRow& setExpandable(bool expandable) {
        m_expandable = expandable;
        return *this;
    }
    DataRow& setExpanded(bool expanded) {
        m_expanded = expanded;
        return *this;
    }
    DataRow& setIcon(std::string icon) {
        m_icon = std::move(icon);
        return *this;
    }
    DataRow& setTone(TextTone tone) {
        m_tone = tone;
        return *this;
    }
    DataRow& setOnToggle(std::function<void(bool)> callback) {
        m_onToggle = std::move(callback);
        return *this;
    }
    DataRow& setPayload(std::string payload) {
        m_payload = std::move(payload);
        return *this;
    }
    DataRow& setOnDrag(std::function<void()> callback) {
        m_onDrag = std::move(callback);
        return *this;
    }
    DataRow& setOnDrop(std::function<void(const char*)> callback) {
        m_onDrop = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override {
        const float scale = std::max(ctx.scale(), 0.001f);
        const ImVec4 color = textToneColor(m_tone, ctx.theme());
        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(color);

        for (usize i = 0; i < m_cells.size(); ++i) {
            ImGui::TableSetColumnIndex(static_cast<int>(i));
            if (i == 0) {
                if (m_indent > 0) {
                    ImGui::Dummy(ImVec2(static_cast<float>(m_indent) * 14.0f * scale, 0.0f));
                    ImGui::SameLine(0.0f, 4.0f * scale);
                }
                if (m_expandable) {
                    ImGui::PushID("expand");
                    if (ImGui::ArrowButton("##toggle",
                                           m_expanded ? ImGuiDir_Down : ImGuiDir_Right)) {
                        if (m_onToggle) m_onToggle(!m_expanded);
                    }
                    ImGui::PopID();
                    ImGui::SameLine(0.0f, 4.0f * scale);
                } else if (m_indent > 0) {
                    ImGui::Dummy(ImVec2(18.0f * scale, 0.0f));
                    ImGui::SameLine(0.0f, 4.0f * scale);
                }
                if (!m_icon.empty() && UIKit::hasIcon(m_icon.c_str())) {
                    UIKit::icon(m_icon.c_str(), ImGui::GetFontSize());
                    ImGui::SameLine(0.0f, 4.0f * scale);
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::TextUnformatted(m_cells[i].c_str());
            ImGui::PopStyleColor();
        }
    }

private:
    friend class DataTable;

    std::vector<std::string> m_cells;
    std::string m_icon;
    std::string m_payload{"ASSET_PATH"};
    TextTone m_tone = TextTone::Normal;
    int m_indent = 0;
    bool m_expandable = false;
    bool m_expanded = false;
    std::function<void(bool)> m_onToggle;
    std::function<void()> m_onDrag;
    std::function<void(const char*)> m_onDrop;
};

class DataTable : public Container {
public:
    explicit DataTable(std::string id = {}) : Container(Axis::Vertical) {
        if (!id.empty()) setId(std::move(id));
    }

    const char* type() const override { return "DataTable"; }

    DataTable& addColumn(std::string title, float width = 0.0f, bool stretch = true) {
        m_columns.push_back({std::move(title), width, stretch});
        return *this;
    }
    DataTable& setHeight(float height) {
        m_height = height;
        return *this;
    }
    DataTable& setRowHeight(float height) {
        m_rowHeight = height;
        return *this;
    }
    DataTable& setSelectable(bool selectable = true) {
        m_selectable = selectable;
        return *this;
    }
    DataTable& setSelectedRow(int index) {
        m_selectedRow = index;
        return *this;
    }
    int selectedRow() const { return m_selectedRow; }

    DataTable& setOnRowSelected(std::function<void(int)> callback) {
        m_onRowSelected = std::move(callback);
        return *this;
    }
    DataTable& setOnRowActivated(std::function<void(int)> callback) {
        m_onRowActivated = std::move(callback);
        return *this;
    }
    DataTable& setOnHeaderClick(std::function<void(int)> callback) {
        m_onHeaderClick = std::move(callback);
        return *this;
    }

    DataRow& addRow(std::vector<std::string> cells) {
        auto row = std::make_unique<DataRow>(std::move(cells));
        DataRow& reference = *row;
        add(std::move(row));
        return reference;
    }

    void selectRow(int index) {
        if (!m_selectable) return;
        m_selectedRow = index;
        if (m_onRowSelected) m_onRowSelected(index);
    }

protected:
    bool beginBody(UIContext& ctx) override {
        m_tableOpen = false;
        if (m_columns.empty()) return false;
        const float scale = std::max(ctx.scale(), 0.001f);
        const float height = m_height > 0.0f ? m_height * scale : 0.0f;
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_ScrollX |
                                      ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable(id().c_str(), static_cast<int>(m_columns.size()),
                               flags, ImVec2(0.0f, height))) {
            m_tableOpen = false;
            return false;
        }
        m_tableOpen = true;
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const DataColumn& column : m_columns) {
            const ImGuiTableColumnFlags columnFlags =
                column.width > 0.0f ? ImGuiTableColumnFlags_WidthFixed
                                    : (column.stretch ? ImGuiTableColumnFlags_WidthStretch
                                                      : ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(column.title.c_str(), columnFlags,
                                    column.width * scale);
        }
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int i = 0; i < static_cast<int>(m_columns.size()); ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::TableHeader(m_columns[static_cast<usize>(i)].title.c_str());
            if (m_onHeaderClick && ImGui::IsItemClicked()) m_onHeaderClick(i);
        }
        return true;
    }

    void endBody(UIContext&) override {
        if (m_tableOpen) {
            ImGui::EndTable();
            m_tableOpen = false;
        }
    }

    void drawChildren(UIContext& ctx) override {
        const float scale = std::max(ctx.scale(), 0.001f);
        const ImVec2 rowSize(0.0f, m_rowHeight > 0.0f ? m_rowHeight * scale : 0.0f);
        int index = 0;
        for (auto& child : m_children) {
            if (!child || !child->visible()) continue;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowSize.y);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(index);
            const bool selected = m_selectable && index == m_selectedRow;
            if (ImGui::Selectable("##data_row", selected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap,
                                  rowSize)) {
                selectRow(index);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                m_onRowActivated) {
                m_onRowActivated(index);
            }
            if (auto* row = dynamic_cast<DataRow*>(child.get())) {
                if (row->m_onDrag &&
                    ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    row->m_onDrag();
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(row->m_payload.c_str())) {
                        const char* value = static_cast<const char*>(payload->Data);
                        if (value && row->m_onDrop) row->m_onDrop(value);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ctx.drawElement(*child);
            ImGui::PopID();
            ++index;
        }
    }

private:
    std::vector<DataColumn> m_columns;
    float m_height = 0.0f;
    float m_rowHeight = 0.0f;
    bool m_selectable = true;
    bool m_tableOpen = false;
    int m_selectedRow = -1;
    std::function<void(int)> m_onRowSelected;
    std::function<void(int)> m_onRowActivated;
    std::function<void(int)> m_onHeaderClick;
};

}  // namespace Caffeine::UIKit::Elements
