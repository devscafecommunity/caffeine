#pragma once

/**
 * Caffeine UI — asset tile.
 *
 * AssetCard is the reusable thumbnail/icon tile used by asset grids, creator
 * palettes and pickers. It owns selection, click/double-click, drag source,
 * drop target and context-menu callbacks; the host keeps the asset semantics.
 */

#include "caffeine/ui/Element.hpp"
#include "caffeine/ui/Icons.hpp"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace Caffeine::UIKit::Elements {

class AssetCard : public Element {
public:
    explicit AssetCard(std::string title, std::string subtitle = {},
                       std::string icon = {})
        : m_title(std::move(title)), m_subtitle(std::move(subtitle)),
          m_icon(std::move(icon)) {
        if (!m_title.empty()) setId("AssetCard:" + m_title);
    }

    const char* type() const override { return "AssetCard"; }

    const std::string& title() const { return m_title; }

    AssetCard& setTitle(std::string title) {
        m_title = std::move(title);
        return *this;
    }
    AssetCard& setSubtitle(std::string subtitle) {
        m_subtitle = std::move(subtitle);
        return *this;
    }
    AssetCard& setIcon(std::string icon) {
        m_icon = std::move(icon);
        return *this;
    }
    AssetCard& setThumbnail(ImTextureRef texture, ImVec2 uv0 = ImVec2(0.0f, 0.0f),
                            ImVec2 uv1 = ImVec2(1.0f, 1.0f)) {
        m_texture = texture;
        m_uv0 = uv0;
        m_uv1 = uv1;
        m_hasThumbnail = true;
        return *this;
    }
    AssetCard& clearThumbnail() {
        m_hasThumbnail = false;
        return *this;
    }
    AssetCard& setSelected(bool selected) {
        m_selected = selected;
        return *this;
    }
    AssetCard& setTileSize(ImVec2 size) {
        m_tileSize = size;
        return *this;
    }
    AssetCard& setPayload(std::string payload) {
        m_payload = std::move(payload);
        return *this;
    }
    AssetCard& setOnClick(std::function<void()> callback) {
        m_onClick = std::move(callback);
        return *this;
    }
    AssetCard& setOnDoubleClick(std::function<void()> callback) {
        m_onDoubleClick = std::move(callback);
        return *this;
    }
    AssetCard& setOnRightClick(std::function<void()> callback) {
        m_onRightClick = std::move(callback);
        return *this;
    }
    AssetCard& setOnDrag(std::function<void()> callback) {
        m_onDrag = std::move(callback);
        return *this;
    }
    AssetCard& setOnDrop(std::function<void(const char*)> callback) {
        m_onDrop = std::move(callback);
        return *this;
    }
    AssetCard& setOnContextMenu(std::function<void()> callback) {
        m_onContextMenu = std::move(callback);
        return *this;
    }

    void draw(UIContext& ctx) override {
        const float scale = std::max(ctx.scale(), 0.001f);
        float width = resolvedWidth() * scale;
        float height = size().height * scale;
        if (width <= 0.0f) width = m_tileSize.x * scale;
        if (height <= 0.0f) height = m_tileSize.y * scale;
        width = std::max(width, 32.0f * scale);
        height = std::max(height, 32.0f * scale);

        ImGui::PushID(id().c_str());
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + width, p0.y + height);
        ImGui::InvisibleButton("##asset_card", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool doubleClicked =
            hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ThemeTokens& theme = ctx.theme();
        const ImU32 background = ImGui::ColorConvertFloat4ToU32(
            hovered ? theme.surfaceOverlay : theme.surfaceRaised);
        const ImU32 border = ImGui::ColorConvertFloat4ToU32(
            m_selected ? theme.accent : (hovered ? theme.borderStrong : theme.border));
        const float rounding = theme.rounding;
        drawList->AddRectFilled(p0, p1, background, rounding);
        drawList->AddRect(p0, p1, border, rounding, 0, m_selected ? 2.0f : 1.0f);

        const float padding = std::max(4.0f, 7.0f * scale);
        const float footer = (m_subtitle.empty() ? 22.0f : 36.0f) * scale;
        const ImVec2 thumb0(p0.x + padding, p0.y + padding);
        const ImVec2 thumb1(p1.x - padding, p1.y - footer);
        if (thumb1.x > thumb0.x && thumb1.y > thumb0.y) {
            if (m_hasThumbnail) {
                drawList->AddImage(m_texture, thumb0, thumb1, m_uv0, m_uv1);
            } else if (!m_icon.empty() && UIKit::hasIcon(m_icon.c_str())) {
                const ImTextureRef icon = UIKit::iconTexture(m_icon.c_str());
                const float side = std::min(32.0f * scale,
                                            std::min(thumb1.x - thumb0.x,
                                                     thumb1.y - thumb0.y));
                const ImVec2 iconPos((thumb0.x + thumb1.x - side) * 0.5f,
                                     (thumb0.y + thumb1.y - side) * 0.5f);
                drawList->AddImage(icon, iconPos, ImVec2(iconPos.x + side, iconPos.y + side));
            }
        }

        drawList->PushClipRect(p0, p1, true);
        const float fontSize = ImGui::GetFontSize();
        const ImVec2 textPos(p0.x + 8.0f * scale, p1.y - footer + 2.0f * scale);
        drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(theme.text),
                          m_title.c_str());
        if (!m_subtitle.empty()) {
            drawList->AddText(ImVec2(textPos.x, textPos.y + fontSize + 1.0f * scale),
                              ImGui::ColorConvertFloat4ToU32(theme.textMuted),
                              m_subtitle.c_str());
        }
        drawList->PopClipRect();

        if (m_onDrag &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            m_onDrag();
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(m_payload.c_str())) {
                const char* value = static_cast<const char*>(payload->Data);
                if (value && m_onDrop) m_onDrop(value);
            }
            ImGui::EndDragDropTarget();
        }
        if (m_onContextMenu && ImGui::BeginPopupContextItem("##asset_card_menu")) {
            m_onContextMenu();
            ImGui::EndPopup();
        }

        setPressed(clicked);
        setHovered(hovered);
        setFocused(ImGui::IsItemFocused());
        if (m_onRightClick && hovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_onRightClick();
        }
        if (m_onClick && clicked) m_onClick();
        if (m_onDoubleClick && doubleClicked) m_onDoubleClick();
        ImGui::PopID();
    }

private:
    std::string m_title;
    std::string m_subtitle;
    std::string m_icon;
    ImTextureRef m_texture;
    ImVec2 m_uv0{0.0f, 0.0f};
    ImVec2 m_uv1{1.0f, 1.0f};
    ImVec2 m_tileSize{128.0f, 116.0f};
    std::string m_payload{"ASSET_PATH"};
    bool m_hasThumbnail = false;
    bool m_selected = false;
    std::function<void()> m_onClick;
    std::function<void()> m_onDoubleClick;
    std::function<void()> m_onRightClick;
    std::function<void()> m_onDrag;
    std::function<void(const char*)> m_onDrop;
    std::function<void()> m_onContextMenu;
};

}  // namespace Caffeine::UIKit::Elements
