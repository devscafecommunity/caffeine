#include "editor/FilePicker.hpp"

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

namespace Caffeine::Editor {

std::optional<std::filesystem::path> FilePicker::pickPath(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
#ifdef CF_HAS_IMGUI
    return pickPathImGui(mode, title, defaultPath);
#endif

    return std::nullopt;
}

std::optional<std::filesystem::path> FilePicker::pickPathNative(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
    return std::nullopt;
}

std::optional<std::filesystem::path> FilePicker::pickPathImGui(
    Mode mode,
    const std::string& title,
    const std::filesystem::path& defaultPath
) {
#ifdef CF_HAS_IMGUI
    static std::filesystem::path currentPath;
    static std::vector<std::filesystem::path> entries;
    static bool initialized = false;
    static char searchFilter[256] = {0};
    static bool windowOpen = false;

    if (!initialized) {
        currentPath = defaultPath.empty() ? std::filesystem::current_path() : defaultPath;
        initialized = true;
        windowOpen = true;
    }

    if (!windowOpen) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> result;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);

    if (ImGui::Begin(title.c_str(), &windowOpen, ImGuiWindowFlags_Modal)) {
        ImGui::Text("Current: %s", currentPath.c_str());

        if (ImGui::Button("Go Up##browser", ImVec2(80, 0))) {
            if (currentPath.has_parent_path()) {
                currentPath = currentPath.parent_path();
            }
        }

        ImGui::InputTextWithHint("##search", "Filter...", searchFilter, sizeof(searchFilter));

        ImGui::Separator();

        if (ImGui::BeginChild("browser_list", ImVec2(0, 300), true)) {
            try {
                entries.clear();
                for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                    std::string name = entry.path().filename().string();

                    if (strlen(searchFilter) > 0) {
                        if (name.find(searchFilter) == std::string::npos) {
                            continue;
                        }
                    }

                    entries.push_back(entry.path());
                }

                std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                    bool aIsDir = std::filesystem::is_directory(a);
                    bool bIsDir = std::filesystem::is_directory(b);
                    if (aIsDir != bIsDir) return aIsDir;
                    return a.filename() < b.filename();
                });

                for (size_t i = 0; i < entries.size(); ++i) {
                    const auto& entry = entries[i];
                    std::string name = entry.filename().string();
                    bool isDir = std::filesystem::is_directory(entry);

                    std::string displayName = isDir ? "[DIR] " + name : name;

                    if (ImGui::Selectable(displayName.c_str())) {
                        if (isDir) {
                            currentPath = entry;
                        } else if (mode != Mode::PickFolder) {
                            result = entry;
                            windowOpen = false;
                            initialized = false;
                        }
                    }

                    if (isDir && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        currentPath = entry;
                    }
                }
            } catch (const std::exception& e) {
                ImGui::TextDisabled("Error: %s", e.what());
            }

            ImGui::EndChild();
        }

        ImGui::Separator();

        if (mode == Mode::PickFolder) {
            if (ImGui::Button("Select This Folder", ImVec2(150, 0))) {
                result = currentPath;
                windowOpen = false;
                initialized = false;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            windowOpen = false;
            initialized = false;
        }

        ImGui::End();
    }

    return result;
#else
    return std::nullopt;
#endif
}

} // namespace Caffeine::Editor

