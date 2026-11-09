#pragma once

/**
 * Caffeine UI — standard widget vocabulary.
 *
 * These helpers are the building blocks used by Doppio panels and by plugins.
 * They are header-only so a plugin can render its own UI without linking the
 * editor's private libraries.
 */

#include "caffeine/ui/Icons.hpp"
#include "caffeine/ui/Theme.hpp"

#include <imgui.h>

#include <cfloat>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>

namespace Caffeine::UIKit {

// ── Layout helpers ─────────────────────────────────────────────────────────

inline void SeparatorSpaced() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

inline void SectionLabel(const char* label) {
    ImGui::Spacing();
    ImGui::TextColored(tokens().textMuted, "%s", label);
    ImGui::Separator();
}

/// Collapsible section with a consistent header. Returns true while open.
inline bool Section(const char* label, bool defaultOpen = true,
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None) {
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    return ImGui::CollapsingHeader(label, flags);
}

inline void HelpMarker(const char* text) {
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline void Kbd(const char* shortcut) {
    ImGui::TextDisabled("%s", shortcut);
}

inline void TextMuted(const char* text) {
    ImGui::TextColored(tokens().textMuted, "%s", text ? text : "");
}

inline void TextMutedF(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().textMuted);
    ImGui::TextV(format, args);
    ImGui::PopStyleColor();
    va_end(args);
}

/// Muted, word-wrapped paragraph used for explanatory copy in panels.
inline void Paragraph(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().textMuted);
    ImGui::TextWrapped("%s", text ? text : "");
    ImGui::PopStyleColor();
}

inline void Bullet(const char* text) {
    ImGui::BulletText("%s", text ? text : "");
}

inline void SameLine(float spacing = -1.0f) {
    if (spacing < 0.0f) {
        ImGui::SameLine();
    } else {
        ImGui::SameLine(0.0f, spacing);
    }
}

inline void TextF(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::TextV(format, args);
    va_end(args);
}

inline bool InputTextLine(const char* label, char* buffer, std::size_t size,
                          ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, buffer, size, flags);
}

inline bool InputTextMultiline(const char* id, char* buffer, std::size_t size,
                               const ImVec2& sizeHint = ImVec2(0, 0),
                               ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextMultiline(id, buffer, size, sizeHint, flags);
}

inline bool Combo(const char* label, int* current, const char* const items[], int count) {
    return ImGui::Combo(label, current, items, count);
}

inline bool SelectableRow(const char* label, bool selected = false,
                          ImGuiSelectableFlags flags = 0) {
    return ImGui::Selectable(label, selected, flags);
}

inline bool MiniButton(const char* label) { return ImGui::SmallButton(label); }

inline bool BeginList(const char* id, const ImVec2& size, bool border = true) {
    return ImGui::BeginChild(id, size, border);
}

inline void EndList() { ImGui::EndChild(); }

inline void PushId(int id) { ImGui::PushID(id); }
inline void PopId() { ImGui::PopID(); }

inline bool BeginTabs(const char* id) { return ImGui::BeginTabBar(id); }
inline bool BeginTab(const char* label) { return ImGui::BeginTabItem(label); }
inline void EndTab() { ImGui::EndTabItem(); }
inline void EndTabs() { ImGui::EndTabBar(); }

inline bool Hovered() { return ImGui::IsItemHovered(); }
inline void Tooltip(const char* text) { ImGui::SetTooltip("%s", text ? text : ""); }

inline void TextSuccess(const char* text) {
    ImGui::TextColored(tokens().success, "%s", text ? text : "");
}

inline void TextWarning(const char* text) {
    ImGui::TextColored(tokens().warning, "%s", text ? text : "");
}

inline void TextDanger(const char* text) {
    ImGui::TextColored(tokens().danger, "%s", text ? text : "");
}

// ── Cards / toolbars ───────────────────────────────────────────────────────

inline bool BeginCard(const char* id, const ImVec2& size = ImVec2(0, 0)) {
    return ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar);
}

inline void EndCard() { ImGui::EndChild(); }

inline bool BeginToolbar(const char* id) {
    const float height = tokens().toolbarHeight * uiScale();
    return ImGui::BeginChild(id, ImVec2(0.0f, height), false,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

inline void EndToolbar() { ImGui::EndChild(); }

// ── Buttons ────────────────────────────────────────────────────────────────

inline bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    const ThemeTokens& t = tokens();
    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, t.onAccent);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

inline bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    const ThemeTokens& t = tokens();
    ImGui::PushStyleColor(ImGuiCol_Button, t.danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.danger);
    ImGui::PushStyleColor(ImGuiCol_Text, t.onAccent);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

inline bool ToolbarButton(const char* label, const char* tooltip = nullptr,
                          bool selected = false, const ImVec2& size = ImVec2(0, 0)) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, tokens().accentMuted);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tokens().accent);
    }
    const bool pressed = ImGui::Button(label, size);
    if (selected) ImGui::PopStyleColor(2);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

/// Toggle-style button for segmented controls (gizmo mode, view mode, brush).
/// Selected segments use the accent fill so the active choice is unambiguous.
inline bool SegmentedButton(const char* label, bool selected,
                            const ImVec2& size = ImVec2(0, 0),
                            const char* tooltip = nullptr) {
    const ThemeTokens& t = tokens();
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, t.onAccent);
    }
    const bool pressed = ImGui::Button(label, size);
    if (selected) ImGui::PopStyleColor(4);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

inline bool Toggle(const char* label, bool* value, const char* tooltip = nullptr) {
    const bool changed = ImGui::Checkbox(label, value);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return changed;
}

inline bool IconTextButton(const char* iconName, const char* label,
                           const char* tooltip = nullptr, bool selected = false) {
    char padded[256];
    std::snprintf(padded, sizeof(padded), "   %s", label ? label : "");
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, tokens().accentMuted);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tokens().accent);
    }
    const bool pressed = ImGui::Button(padded);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float iconSize = ImGui::GetFontSize();
    const float pad = ImGui::GetStyle().FramePadding.x;
    if (hasIcon(iconName)) {
        const ImVec2 saved = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(
            ImVec2(min.x + pad, (min.y + max.y) * 0.5f - iconSize * 0.5f));
        icon(iconName, iconSize);
        ImGui::SetCursorScreenPos(saved);
    }
    if (selected) ImGui::PopStyleColor(2);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

// ── Feedback ───────────────────────────────────────────────────────────────

inline void Badge(const char* text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().onAccent);
    ImGui::SmallButton(text);
    ImGui::PopStyleColor(4);
}

inline void StatusDot(const ImVec4& color) {
    const float r = ImGui::GetFontSize() * 0.28f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r,
        ImGui::ColorConvertFloat4ToU32(color));
    ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, 2.0f);
}

inline void EmptyState(const char* title, const char* subtitle = nullptr,
                       const char* iconName = nullptr) {
    const float avail = ImGui::GetContentRegionAvail().y;
    ImGui::Dummy(ImVec2(0.0f, avail * 0.22f));
    const float width = ImGui::GetContentRegionAvail().x;
    auto centred = [width](float itemWidth) {
        if (itemWidth < width) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - itemWidth) * 0.5f);
        }
    };

    if (iconName && hasIcon(iconName)) {
        const float size = 32.0f * uiScale();
        centred(size);
        icon(iconName, size, ImGui::ColorConvertFloat4ToU32(tokens().textMuted));
    }
    const float titleWidth = ImGui::CalcTextSize(title).x;
    centred(titleWidth);
    ImGui::TextColored(tokens().textMuted, "%s", title);
    if (subtitle && subtitle[0]) {
        const float subWidth = ImGui::CalcTextSize(subtitle).x;
        if (subWidth < width) {
            centred(subWidth);
            ImGui::TextDisabled("%s", subtitle);
        } else {
            // Narrow panel: wrap instead of clipping the text on the left.
            ImGui::PushStyleColor(ImGuiCol_Text, tokens().textDisabled);
            ImGui::TextWrapped("%s", subtitle);
            ImGui::PopStyleColor();
        }
    }
}

// ── Search / inputs ────────────────────────────────────────────────────────

inline bool SearchField(const char* id, char* buffer, std::size_t size,
                        const char* hint = "Search...", float width = -1.0f) {
    ImGui::PushItemWidth(width);
    const bool changed = ImGui::InputTextWithHint(id, hint, buffer, size,
                                                  ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::PopItemWidth();
    return changed;
}

// ── Property grid ──────────────────────────────────────────────────────────

/// True while a BeginPropertyGrid table is active. Property helpers use this
/// guard so a misplaced call cannot crash ImGui's table stack.
inline bool& propertyGridActive() {
    static bool active = false;
    return active;
}

inline bool BeginPropertyGrid(const char* id, float labelWidth = 140.0f) {
    if (propertyGridActive()) return false;
    if (!ImGui::BeginTable(id, 2,
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoBordersInBody)) {
        return false;
    }
    propertyGridActive() = true;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed,
                            labelWidth * uiScale());
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

inline void EndPropertyGrid() {
    if (!propertyGridActive()) return;
    propertyGridActive() = false;
    ImGui::EndTable();
}

inline bool PropertyRow(const char* label, const std::function<bool()>& draw,
                        const char* tooltip = nullptr) {
    if (!propertyGridActive()) return false;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip) HelpMarker(tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    const bool changed = draw();
    ImGui::PopID();
    return changed;
}

inline bool PropertyRow(const char* label, const char* value) {
    if (!propertyGridActive()) return false;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", value ? value : "");
    return false;
}

inline bool DragFloatProperty(const char* label, float* value, float speed = 0.1f,
                              float min = 0.0f, float max = 0.0f,
                              const char* format = "%.3f") {
    return PropertyRow(label, [&] {
        return ImGui::DragFloat("##value", value, speed, min, max, format);
    });
}

inline bool DragIntProperty(const char* label, int* value, float speed = 1.0f,
                            int min = 0, int max = 0, const char* format = "%d") {
    return PropertyRow(label, [&] {
        return ImGui::DragInt("##value", value, speed, min, max, format);
    });
}

inline bool DragInt2Property(const char* label, int value[2], float speed = 1.0f,
                             int min = 0, int max = 0) {
    return PropertyRow(label, [&] {
        return ImGui::DragInt2("##value", value, speed, min, max);
    });
}

inline bool DragFloat2Property(const char* label, float value[2], float speed = 0.1f,
                               float min = 0.0f, float max = 0.0f,
                               const char* format = "%.3f") {
    return PropertyRow(label, [&] {
        return ImGui::DragFloat2("##value", value, speed, min, max, format);
    });
}

inline bool CheckboxProperty(const char* label, bool* value) {
    return PropertyRow(label, [&] { return ImGui::Checkbox("##value", value); });
}

inline bool ComboProperty(const char* label, int* current,
                          const char* const items[], int count) {
    return PropertyRow(label, [&] {
        return ImGui::Combo("##value", current, items, count);
    });
}

inline bool SliderFloatProperty(const char* label, float* value, float min, float max,
                                const char* format = "%.3f") {
    return PropertyRow(label, [&] {
        return ImGui::SliderFloat("##value", value, min, max, format);
    });
}

inline bool SliderIntProperty(const char* label, int* value, int min, int max,
                              const char* format = "%d") {
    return PropertyRow(label, [&] {
        return ImGui::SliderInt("##value", value, min, max, format);
    });
}

inline bool InputTextProperty(const char* label, char* buffer, std::size_t size,
                              ImGuiInputTextFlags flags = 0) {
    return PropertyRow(label, [&] {
        return ImGui::InputText("##value", buffer, size, flags);
    });
}

inline bool InputTextMultilineProperty(const char* label, char* buffer, std::size_t size,
                                       const ImVec2& sizeHint = ImVec2(0, 0),
                                       ImGuiInputTextFlags flags = 0) {
    if (!propertyGridActive()) return false;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
    const bool changed = ImGui::InputTextMultiline("##value", buffer, size, sizeHint, flags);
    ImGui::PopID();
    return changed;
}

inline bool InputTextProperty(const char* label, std::string& value,
                              std::size_t capacity = 512) {
    return PropertyRow(label, [&] {
        char buffer[512];
        const std::size_t n = capacity < sizeof(buffer) ? capacity : sizeof(buffer);
        std::snprintf(buffer, n, "%s", value.c_str());
        if (ImGui::InputText("##value", buffer, n)) {
            value = buffer;
            return true;
        }
        return false;
    });
}

inline bool ColorProperty(const char* label, float color[4],
                          ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs) {
    return PropertyRow(label, [&] {
        return ImGui::ColorEdit4("##value", color, flags);
    });
}

// ── Vector editors ─────────────────────────────────────────────────────────

inline bool DragVec3(const char* id, float* value, float speed = 0.1f,
                     float min = -FLT_MAX, float max = FLT_MAX,
                     const char* format = "%.3f") {
    const float spacing = ImGui::GetStyle().ItemSpacing.x * uiScale();
    const float width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    const ImVec4 axis[3] = {ImVec4(0.95f, 0.35f, 0.40f, 1.0f),
                            ImVec4(0.45f, 0.90f, 0.50f, 1.0f),
                            ImVec4(0.45f, 0.65f, 1.00f, 1.0f)};
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushItemWidth(width);
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(axis[i].x * 0.18f,
                                                       axis[i].y * 0.18f,
                                                       axis[i].z * 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, axis[i]);
        const char* labels[3] = {"##x", "##y", "##z"};
        if (ImGui::DragFloat(labels[i], &value[i], speed, min, max, format)) changed = true;
        ImGui::PopStyleColor(2);
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

inline bool DragVec2(const char* id, float* value, float speed = 0.1f,
                     float min = -FLT_MAX, float max = FLT_MAX,
                     const char* format = "%.3f") {
    const float spacing = ImGui::GetStyle().ItemSpacing.x * uiScale();
    const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushItemWidth(width);
    if (ImGui::DragFloat("##x", &value[0], speed, min, max, format)) changed = true;
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::DragFloat("##y", &value[1], speed, min, max, format)) changed = true;
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

// ── Panel chrome ───────────────────────────────────────────────────────────

/// Begin a dockable panel with sane minimum sizes. Prevents the 44-pixel
/// collapsed windows ImGui otherwise persists in imgui.ini.
inline bool BeginPanel(const char* title, bool* open = nullptr,
                       ImGuiWindowFlags flags = ImGuiWindowFlags_None) {
    const float minWidth = 240.0f * uiScale();
    const float minHeight = 120.0f * uiScale();
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, minHeight),
                                        ImVec2(FLT_MAX, FLT_MAX));
    return ImGui::Begin(title, open, flags);
}

inline void EndPanel() { ImGui::End(); }

/// Standard panel intro: title, optional subtitle and a separator.
inline void PanelHeader(const char* title, const char* subtitle = nullptr,
                        const char* iconName = nullptr) {
    if (iconName && hasIcon(iconName)) {
        icon(iconName, ImGui::GetFontSize() * 1.15f);
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(title);
    if (subtitle && subtitle[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", subtitle);
    }
    ImGui::Separator();
}

}  // namespace Caffeine::UIKit
