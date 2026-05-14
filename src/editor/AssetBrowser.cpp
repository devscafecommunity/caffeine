#include "editor/AssetBrowser.hpp"

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

// ── Init / Refresh ───────────────────────────────────────────────

void AssetBrowser::init(const char* rootPath) {
    m_rootPath = rootPath;
    m_currentDir = rootPath;
    refreshDirectory();
}

void AssetBrowser::refreshDirectory() {
    m_entries.clear();
    if (!std::filesystem::exists(m_currentDir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(m_currentDir)) {
        if (entry.is_directory()) {
            Entry e;
            e.path = entry.path();
            e.type = AssetType::Unknown;
            strncpy(e.label, entry.path().filename().string().c_str(), sizeof(e.label) - 1);
            e.label[sizeof(e.label) - 1] = '\0';
            m_entries.push_back(std::move(e));
        } else {
            auto ext = entry.path().extension().string();
            AssetType type = AssetType::Unknown;

            if (ext == ".caf") {
                type = AssetType::Scene;
            } else if (ext == ".png" || ext == ".jpg" || ext == ".tga") {
                type = AssetType::Texture;
            } else if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
                type = AssetType::Audio;
            } else if (ext == ".obj") {
                type = AssetType::Mesh;
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

// ── Icon helper ──────────────────────────────────────────────────

const char* AssetBrowser::iconForType(AssetType type) {
    switch (type) {
        case AssetType::Texture: return "[T]";
        case AssetType::Audio:   return "[A]";
        case AssetType::Mesh:    return "[M]";
        case AssetType::Scene:   return "[S]";
        default:                 return "[ ]";
    }
}

// ── Main render ──────────────────────────────────────────────────

void AssetBrowser::render(EditorContext& ctx) {
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

        int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / 100.0f));
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

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                m_selectedEntry = static_cast<int>(i);
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (isDir) {
                    m_currentDir = entry.path;
                    refreshDirectory();
                    ImGui::EndGroup();
                    break;
                }
            }

            if (!isDir && ImGui::BeginDragDropSource()) {
                std::string pathStr = entry.path.string();
                ImGui::SetDragDropPayload("ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
                ImGui::Text("%s", entry.label);
                ImGui::EndDragDropSource();
            }

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
}

// ── Drag-drop target ─────────────────────────────────────────────

std::optional<std::filesystem::path> AssetBrowser::getDroppedAsset() const {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            std::filesystem::path path(static_cast<const char*>(payload->Data));
            ImGui::EndDragDropTarget();
            return path;
        }
        ImGui::EndDragDropTarget();
    }
    return {};
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
