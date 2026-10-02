#pragma once
#include "editor/EditorIcons.hpp"
#include "editor/EditorContext.hpp"
#include "editor/DragDropSystem.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include <string>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <cctype>

#ifdef CF_HAS_IMGUI
#include <imgui.h>

namespace Caffeine::Editor::Widgets {

inline void AxisLabels2() {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float colW = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX());
    ImGui::Text("X");
    ImGui::SameLine(ImGui::GetCursorPosX() + colW + spacing);
    ImGui::Text("Y");
    ImGui::PopStyleColor();
}

inline void AxisLabels3() {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float colW = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("X");
    ImGui::SameLine(0.0f, colW - ImGui::CalcTextSize("X").x + spacing);
    ImGui::Text("Y");
    ImGui::SameLine(0.0f, colW - ImGui::CalcTextSize("Y").x + spacing);
    ImGui::Text("Z");
    ImGui::PopStyleColor();
}

inline bool DragVec3(const char* label, Vec3& v, float speed = 0.1f,
                     float lo = -1e9f, float hi = 1e9f) {
    ImGui::TextUnformatted(label);
    AxisLabels3();
    ImGui::PushID(label);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float colW = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    bool changed = false;
    float tmp[3] = {v.x, v.y, v.z};

    ImGui::PushItemWidth(colW);
    if (ImGui::DragFloat("##x", &tmp[0], speed, lo, hi, "%.3f")) changed = true;
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::DragFloat("##y", &tmp[1], speed, lo, hi, "%.3f")) changed = true;
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::DragFloat("##z", &tmp[2], speed, lo, hi, "%.3f")) changed = true;
    ImGui::PopItemWidth();
    ImGui::PopID();

    if (changed) {
        v.x = tmp[0];
        v.y = tmp[1];
        v.z = tmp[2];
    }
    return changed;
}

inline bool DragVec2(const char* label, Vec2& v, float speed = 0.1f,
                     float lo = -1e9f, float hi = 1e9f) {
    ImGui::TextUnformatted(label);
    AxisLabels2();
    ImGui::PushID(label);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float colW = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    bool changed = false;
    float tmp[2] = {v.x, v.y};

    ImGui::PushItemWidth(colW);
    if (ImGui::DragFloat("##x", &tmp[0], speed, lo, hi, "%.3f")) changed = true;
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::DragFloat("##y", &tmp[1], speed, lo, hi, "%.3f")) changed = true;
    ImGui::PopItemWidth();
    ImGui::PopID();

    if (changed) {
        v.x = tmp[0];
        v.y = tmp[1];
    }
    return changed;
}

inline bool InputText(const char* label, std::string& str) {
    char buf[512];
    strncpy(buf, str.c_str(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText(label, buf, sizeof(buf))) {
        str = buf;
        return true;
    }
    return false;
}

inline std::string toLowerExt(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline bool extensionMatchesFilter(const std::filesystem::path& path, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    const std::string ext = toLowerExt(path.extension().string());
    std::string filterStr(filter);
    size_t start = 0;
    while (start < filterStr.size()) {
        const size_t sep = filterStr.find(';', start);
        std::string token = filterStr.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        token = toLowerExt(token);
        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
        while (!token.empty() && token.back() == ' ') token.pop_back();
        if (!token.empty() && token.front() != '.') token.insert(token.begin(), '.');
        if (!token.empty() && ext == token) return true;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return false;
}

inline std::string pathRelativeToProject(const EditorContext& ctx, const std::filesystem::path& absolute) {
    if (!ctx.projectRootPath.empty()) {
        std::error_code ec;
        const auto rel = std::filesystem::relative(absolute, ctx.projectRootPath, ec);
        if (!ec && !rel.empty() && rel.generic_string().rfind("..") != 0) {
            return rel.generic_string();
        }
    }
    return absolute.string();
}

/// Leaves room for ImGui's right-side label so the control cannot consume the whole row.
inline void setWidthForLabel(const char* label) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float labelW = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
    const float controlW = avail - labelW;
    ImGui::SetNextItemWidth(controlW > 48.0f ? controlW : std::max(48.0f, avail * 0.5f));
}

inline bool AssetField(EditorContext& ctx, const char* label, std::string& path, const char* filter) {
    bool changed = false;
    const std::string display = path.empty() ? "(none)" : std::filesystem::path(path).filename().string();
    char dispBuf[256];
    strncpy(dispBuf, display.c_str(), sizeof(dispBuf));
    dispBuf[sizeof(dispBuf) - 1] = '\0';

    ImGui::PushID(label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemInnerSpacing.x;
    const float browseW = ImGui::CalcTextSize("Browse").x + style.FramePadding.x * 2.0f + 8.0f;
    const float clearW = path.empty() ? 0.0f : 22.0f;
    const float buttons = browseW + (clearW > 0.0f ? spacing + clearW : 0.0f);

    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(std::max(48.0f, ImGui::GetContentRegionAvail().x - buttons - spacing));

    ImGui::InputText("##path", dispBuf, sizeof(dispBuf), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const AssetDropPayload* drop = DragDropManager::AcceptAssetDrop()) {
            const std::filesystem::path dropped(drop->path);
            if (extensionMatchesFilter(dropped, filter)) {
                path = pathRelativeToProject(ctx, dropped);
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button("Browse", ImVec2(browseW, 0.0f))) {
        ctx.browse.requestProjectAsset(&path, filter, label);
    }
    if (!path.empty()) {
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("X", ImVec2(clearW, 0.0f))) {
            path.clear();
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

inline bool ComponentHeader(const char* label, bool& enabled, bool& outRemove,
                            const char* iconName = nullptr) {
    outRemove = false;
    ImGui::PushID(label);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
    bool open = ImGui::CollapsingHeader("##hdr", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleVar();

    ImGui::SameLine();
    ImGui::Checkbox("##en", &enabled);
    ImGui::SameLine();

    if (iconName && EditorIcons::hasIcon(iconName)) {
        EditorIcons::image(iconName, ImGui::GetFontSize());
        ImGui::SameLine();
    }

    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f);
    if (ImGui::SmallButton("...")) {
        ImGui::OpenPopup("##cmenu");
    }
    if (ImGui::BeginPopup("##cmenu")) {
        if (ImGui::MenuItem("Remove Component")) {
            outRemove = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return open;
}

} // namespace Caffeine::Editor::Widgets
#endif // CF_HAS_IMGUI
