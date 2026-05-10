#pragma once
#include "core/Types.hpp"
#include "assets/AssetManager.hpp"
#include "assets/AssetTypes.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"

#include <vector>
#include <filesystem>
#include <cstring>
#include <optional>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class AssetBrowser {
public:
    struct Entry {
        std::filesystem::path     path;
        Assets::AssetType         type;
        char                      label[64];
    };

    AssetBrowser() = default;

    void init(const char* rootPath) {
        m_rootPath = rootPath;
        m_currentDir = rootPath;
        refreshDirectory();
    }

    void refreshDirectory() {
        m_entries.clear();
        if (!std::filesystem::exists(m_currentDir)) return;

        for (const auto& entry : std::filesystem::directory_iterator(m_currentDir)) {
            if (entry.is_directory()) {
                Entry e;
                e.path = entry.path();
                e.type = Assets::AssetType::Unknown;
                strncpy(e.label, entry.path().filename().string().c_str(), sizeof(e.label) - 1);
                e.label[sizeof(e.label) - 1] = '\0';
                m_entries.push_back(std::move(e));
            } else {
                auto ext = entry.path().extension().string();
                Assets::AssetType type = Assets::AssetType::Unknown;
                if (ext == ".caf" || ext == ".png" || ext == ".jpg") {
                    type = Assets::AssetType::Texture;
                } else if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
                    type = Assets::AssetType::Audio;
                } else if (ext == ".obj") {
                    type = Assets::AssetType::Mesh;
                }

                Entry e;
                e.path = entry.path();
                e.type = type;
                strncpy(e.label, entry.path().filename().string().c_str(), sizeof(e.label) - 1);
                e.label[sizeof(e.label) - 1] = '\0';
                m_entries.push_back(std::move(e));
            }
        }
    }

    const char* iconForType(Assets::AssetType type) {
        switch (type) {
            case Assets::AssetType::Texture: return "[T]";
            case Assets::AssetType::Audio:   return "[A]";
            case Assets::AssetType::Mesh:    return "[M]";
            case Assets::AssetType::Scene:   return "[S]";
            default:                         return "[ ]";
        }
    }

    void render(EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
        if (!m_open) return;
        if (ImGui::Begin("Asset Browser", &m_open)) {

            // Breadcrumb + back button
            if (ImGui::Button("<- Back") && m_currentDir.has_parent_path()) {
                m_currentDir = m_currentDir.parent_path();
                refreshDirectory();
            }
            ImGui::SameLine();
            std::string dirName = m_currentDir.filename().string();
            if (dirName.empty()) dirName = m_rootPath;
            ImGui::TextUnformatted(dirName.c_str());
            ImGui::Separator();

            ImGui::BeginChild("asset_grid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

            int cols = ImMax(1, static_cast<int>(ImGui::GetContentRegionAvail().x / 100.0f));
            ImGui::Columns(cols, nullptr, false);

            for (usize i = 0; i < m_entries.size(); ++i) {
                auto& entry = m_entries[i];

                bool isDir = std::filesystem::is_directory(entry.path);

                ImGui::BeginGroup();
                if (isDir) {
                    ImGui::Text("[dir]");
                } else {
                    ImGui::Text("%s", iconForType(entry.type));
                }
                ImGui::TextUnformatted(entry.label);

                // Click to select
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    m_selectedEntry = static_cast<int>(i);
                }

                // Double-click to navigate (folder) or select asset
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (isDir) {
                        m_currentDir = entry.path;
                        refreshDirectory();
                        ImGui::EndGroup();
                        break; // columns are broken after directory change
                    }
                }

                // Drag-drop source for assets
                if (!isDir && ImGui::BeginDragDropSource()) {
                    std::string pathStr = entry.path.string();
                    ImGui::SetDragDropPayload("ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
                    ImGui::Text("%s", entry.label);
                    ImGui::EndDragDropSource();
                }

                // Selection highlight
                if (m_selectedEntry == static_cast<int>(i) && ImGui::IsItemHovered()) {
                    ImGui::GetWindowDrawList()->AddRect(
                        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        IM_COL32(255, 255, 0, 100));
                }

                ImGui::EndGroup();
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::EndChild();
        }
        ImGui::End();
#endif
    }

    std::optional<std::filesystem::path> getDroppedAsset() const {
#ifdef CF_HAS_IMGUI
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                std::filesystem::path path(static_cast<const char*>(payload->Data));
                ImGui::EndDragDropTarget();
                return path;
            }
            ImGui::EndDragDropTarget();
        }
#endif
        return {};
    }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
    bool m_open = true;
    std::string m_rootPath = "assets";
    std::filesystem::path m_currentDir;
    std::vector<Entry> m_entries;
    int m_selectedEntry = -1;
};

} // namespace Caffeine::Editor
