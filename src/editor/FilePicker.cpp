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
#include <unordered_map>
#include <unordered_set>
#include <cstring>

namespace Caffeine::Editor {

namespace {
std::unordered_set<std::string> g_filePickerCloseEvents;
}

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

bool FilePicker::consumeCloseEvent(const std::string& title) {
    auto it = g_filePickerCloseEvents.find(title);
    if (it == g_filePickerCloseEvents.end()) {
        return false;
    }
    g_filePickerCloseEvents.erase(it);
    return true;
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
    struct State {
        std::filesystem::path currentPath;
        std::vector<std::filesystem::path> entries;
        std::string searchFilter;
        bool isOpen;
    };
    
    static std::unordered_map<std::string, State> states;
    
    auto it = states.find(title);
     if (it == states.end()) {
         states[title] = {
             defaultPath.empty() ? std::filesystem::current_path() : defaultPath,
             {},
             "",
             true
         };
         it = states.find(title);
     }
    
    State& state = it->second;
    std::optional<std::filesystem::path> result;

    if (!state.isOpen) {
        states.erase(title);
        g_filePickerCloseEvents.insert(title);
        return std::nullopt;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);

    bool windowOpen = ImGui::Begin(title.c_str(), &state.isOpen, ImGuiWindowFlags_NoMove);
    
    if (windowOpen) {
        ImGui::Text("Current: %s", state.currentPath.c_str());

        if (ImGui::Button("Go Up##browser", ImVec2(80, 0))) {
            if (state.currentPath.has_parent_path()) {
                state.currentPath = state.currentPath.parent_path();
            }
        }

        char filterBuf[256];
        strcpy(filterBuf, state.searchFilter.c_str());
        if (ImGui::InputTextWithHint("##search", "Filter...", filterBuf, sizeof(filterBuf))) {
            state.searchFilter = filterBuf;
        }

        ImGui::Separator();

        if (ImGui::BeginChild("browser_list", ImVec2(0, 300), true)) {
            try {
                state.entries.clear();
                for (const auto& entry : std::filesystem::directory_iterator(state.currentPath)) {
                    std::string name = entry.path().filename().string();

                    if (!state.searchFilter.empty()) {
                        if (name.find(state.searchFilter) == std::string::npos) {
                            continue;
                        }
                    }

                    state.entries.push_back(entry.path());
                }

                std::sort(state.entries.begin(), state.entries.end(), [](const auto& a, const auto& b) {
                    bool aIsDir = std::filesystem::is_directory(a);
                    bool bIsDir = std::filesystem::is_directory(b);
                    if (aIsDir != bIsDir) return aIsDir;
                    return a.filename() < b.filename();
                });

                for (size_t i = 0; i < state.entries.size(); ++i) {
                    const auto& entry = state.entries[i];
                    std::string name = entry.filename().string();
                    bool isDir = std::filesystem::is_directory(entry);

                    std::string displayName = isDir ? "[DIR] " + name : name;

                    if (ImGui::Selectable(displayName.c_str())) {
                        if (isDir) {
                            state.currentPath = entry;
                        } else if (mode != Mode::PickFolder) {
                            result = entry;
                            state.isOpen = false;
                        }
                    }

                    if (isDir && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        state.currentPath = entry;
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
                result = state.currentPath;
                state.isOpen = false;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            state.isOpen = false;
        }
    }
    
    ImGui::End();

    return result;
#else
    return std::nullopt;
#endif
}

} // namespace Caffeine::Editor

