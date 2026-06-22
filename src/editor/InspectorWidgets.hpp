#pragma once
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include <string>
#include <filesystem>
#include <functional>

#ifdef CF_HAS_IMGUI
#include <imgui.h>

namespace Caffeine::Editor::Widgets {

inline bool DragVec3(const char* label, Vec3& v, float speed = 0.1f,
                     float lo = -1e9f, float hi = 1e9f) {
    float tmp[3] = { v.x, v.y, v.z };
    if (ImGui::DragFloat3(label, tmp, speed, lo, hi)) {
        v.x = tmp[0]; v.y = tmp[1]; v.z = tmp[2];
        return true;
    }
    return false;
}

inline bool DragVec2(const char* label, Vec2& v, float speed = 0.1f,
                     float lo = -1e9f, float hi = 1e9f) {
    float tmp[2] = { v.x, v.y };
    if (ImGui::DragFloat2(label, tmp, speed, lo, hi)) {
        v.x = tmp[0]; v.y = tmp[1];
        return true;
    }
    return false;
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

inline bool AssetField(const char* label, std::string& path,
                       const char* filter,
                       const std::filesystem::path& projectRoot) {
    bool changed = false;
    std::string display = path.empty() ? "(none)" : std::filesystem::path(path).filename().string();
    char dispBuf[256];
    strncpy(dispBuf, display.c_str(), sizeof(dispBuf));
    dispBuf[sizeof(dispBuf) - 1] = '\0';
    ImGui::InputText(label, dispBuf, sizeof(dispBuf), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    std::string btnId = std::string("...##") + label;
    if (ImGui::Button(btnId.c_str(), ImVec2(28, 0))) {
        ImGui::OpenPopup(label);
    }
    if (ImGui::BeginPopup(label)) {
        ImGui::Text("Select asset (%s)", filter);
        ImGui::Separator();
        static char search[128] = {};
        ImGui::InputText("##search", search, sizeof(search));
        if (std::filesystem::exists(projectRoot)) {
            std::string filterStr(filter);
            for (auto& entry : std::filesystem::recursive_directory_iterator(
                     projectRoot, std::filesystem::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                if (filterStr.find(ext) == std::string::npos) continue;
                std::string fname = entry.path().filename().string();
                if (search[0] != '\0' && fname.find(search) == std::string::npos) continue;
                if (ImGui::Selectable(fname.c_str())) {
                    path = entry.path().string();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            ImGui::TextDisabled("No project root set");
        }
        ImGui::EndPopup();
    }
    return changed;
}

inline bool ComponentHeader(const char* label, bool& enabled, bool& outRemove) {
    outRemove = false;
    ImGui::PushID(label);
    bool open = ImGui::CollapsingHeader("##hdr", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    ImGui::Checkbox("##en", &enabled);
    ImGui::SameLine();
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
