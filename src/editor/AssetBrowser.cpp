#include "editor/AssetBrowser.hpp"
#include "editor/CapLoader.hpp"

// ═════════════════════════════════════════════════════════════════════════════
// Data layer — always compiled (no ImGui dependency)
// ═════════════════════════════════════════════════════════════════════════════

namespace Caffeine::Editor {

// ── Init / Refresh ──────────────────────────────────────────────────────────

void AssetBrowser::init(const char* rootPath) {
    m_rootPath = rootPath ? rootPath : "assets";
    m_currentDir = m_rootPath;
    m_pathHistory.clear();
    refresh();
}

void AssetBrowser::refresh() {
    if (m_browseMode == BrowseMode::CapFile) {
        loadCapFile(m_currentCapPath);
        return;
    }

    m_entries.clear();
    if (!std::filesystem::exists(m_currentDir)) {
        applySearchFilter();
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_currentDir)) {
        Entry e;
        e.path        = entry.path();
        e.name        = entry.path().filename().string();
        e.isDirectory = entry.is_directory();

        if (!e.isDirectory) {
            const auto ext = entry.path().extension().string();
            if      (ext == ".caf")                  e.type = AssetType::Scene;
            else if (ext == ".png"  || ext == ".jpg"  || ext == ".jpeg" || ext == ".tga")  e.type = AssetType::Texture;
            else if (ext == ".wav"  || ext == ".ogg"  || ext == ".mp3")   e.type = AssetType::Audio;
            else if (ext == ".obj"  || ext == ".gltf" || ext == ".glb")   e.type = AssetType::Mesh;
            else if (ext == ".lua")                      e.type = AssetType::Unknown;
            else                                     e.type = AssetType::Unknown;

            std::error_code ec;
            e.fileSize = std::filesystem::file_size(entry.path(), ec);
        }

        m_entries.push_back(std::move(e));
    }

    applySearchFilter();
}

void AssetBrowser::loadCapFile(const std::filesystem::path& capPath) {
    m_entries.clear();
    m_currentCapPath = capPath;
    m_browseMode = BrowseMode::CapFile;
    
    auto assets = CapLoader::loadCap(capPath);
    
    for (const auto& asset : assets) {
        Entry entry{};
        entry.name = std::to_string(asset.hashID);
        
        switch (asset.type) {
            case Caffeine::Assets::CafAssetType::Texture:
                entry.type = AssetType::Texture;
                break;
            case Caffeine::Assets::CafAssetType::Audio:
                entry.type = AssetType::Audio;
                break;
            case Caffeine::Assets::CafAssetType::Mesh:
                entry.type = AssetType::Mesh;
                break;
            default:
                entry.type = AssetType::Unknown;
        }
        
        entry.path = capPath;
        entry.fileSize = asset.cafBlob.size();
        entry.isDirectory = false;
        m_entries.push_back(entry);
    }
    
    applySearchFilter();
}

// ── Search ──────────────────────────────────────────────────────────────────

void AssetBrowser::setSearchFilter(const char* filter) {
    m_searchFilter = filter ? filter : "";
    applySearchFilter();
}

const std::string& AssetBrowser::searchFilter() const {
    return m_searchFilter;
}

void AssetBrowser::applySearchFilter() {
    m_filteredEntries.clear();
    if (m_searchFilter.empty()) {
        m_filteredEntries = m_entries;
        return;
    }

    std::string lowerFilter = m_searchFilter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& e : m_entries) {
        std::string lowerName = e.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerName.find(lowerFilter) != std::string::npos) {
            m_filteredEntries.push_back(e);
        }
    }
}

// ── Navigation ──────────────────────────────────────────────────────────────

void AssetBrowser::navigateTo(const std::filesystem::path& dir) {
    if (std::filesystem::is_directory(dir)) {
        m_pathHistory.push_back(m_currentDir);
        m_currentDir = dir;
        refresh();
    }
}

void AssetBrowser::navigateBack() {
    if (!m_pathHistory.empty()) {
        m_currentDir = m_pathHistory.back();
        m_pathHistory.pop_back();
        refresh();
    }
}

bool AssetBrowser::canGoBack() const {
    return !m_pathHistory.empty();
}

const std::filesystem::path& AssetBrowser::currentPath() const {
    return m_currentDir;
}

// ── View mode ───────────────────────────────────────────────────────────────

void AssetBrowser::setViewMode(ViewMode mode) {
    m_viewMode = mode;
}

AssetBrowser::ViewMode AssetBrowser::viewMode() const {
    return m_viewMode;
}

// ── Thumbnail size ──────────────────────────────────────────────────────────

void AssetBrowser::setThumbnailSize(u32 size) {
    m_thumbnailSize = size;
}

u32 AssetBrowser::thumbnailSize() const {
    return m_thumbnailSize;
}

// ── Entry access ────────────────────────────────────────────────────────────

const std::vector<AssetBrowser::Entry>& AssetBrowser::entries() const {
    return m_filteredEntries;
}

usize AssetBrowser::entryCount() const {
    return m_filteredEntries.size();
}

} // namespace Caffeine::Editor


// ═════════════════════════════════════════════════════════════════════════════
// UI layer — requires ImGui
// ═════════════════════════════════════════════════════════════════════════════

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

// ── Icon helper ─────────────────────────────────────────────────────────────

const char* AssetBrowser::iconForType(AssetType type, const std::filesystem::path& path) {
    if (path.extension() == ".lua") {
        return "[L]";
    }
    switch (type) {
        case AssetType::Texture: return "[T]";
        case AssetType::Audio:   return "[A]";
        case AssetType::Mesh:    return "[M]";
        case AssetType::Scene:   return "[S]";
        default:                 return "[ ]";
    }
}

// ── Toolbar ─────────────────────────────────────────────────────────────────

void AssetBrowser::renderToolbar() {
    // Back button
    if (canGoBack()) {
        if (ImGui::Button("<- Back")) {
            navigateBack();
        }
        ImGui::SameLine();
    }

    // Breadcrumbs
    renderBreadcrumbs();
    ImGui::SameLine();

    // Search
    char searchBuf[256] = {};
    std::strncpy(searchBuf, m_searchFilter.c_str(), sizeof(searchBuf) - 1);
    ImGui::PushItemWidth(160.0f);
    if (ImGui::InputTextWithHint("##asset_search", "Search...", searchBuf, sizeof(searchBuf))) {
        setSearchFilter(searchBuf);
    }
    ImGui::PopItemWidth();

    // View mode toggle
    ImGui::SameLine();
    const char* viewLabel = (m_viewMode == ViewMode::Grid) ? "Grid" : "List";
    if (ImGui::Button(viewLabel)) {
        m_viewMode = (m_viewMode == ViewMode::Grid) ? ViewMode::List : ViewMode::Grid;
    }

    // Thumbnail size slider
    ImGui::SameLine();
    ImGui::PushItemWidth(100.0f);
    int size = static_cast<int>(m_thumbnailSize);
    if (ImGui::SliderInt("##thumb_size", &size, 16, 256)) {
        m_thumbnailSize = static_cast<u32>(size);
    }
    ImGui::PopItemWidth();
}

// ── Breadcrumbs ─────────────────────────────────────────────────────────────

void AssetBrowser::renderBreadcrumbs() {
    std::filesystem::path rel;
    if (m_currentDir == m_rootPath) {
        ImGui::TextUnformatted(m_rootPath.c_str());
        return;
    }

    // Build relative path to show clickable segments
    std::string display;
    if (m_currentDir.string().find(m_rootPath) == 0) {
        auto relPath = std::filesystem::relative(m_currentDir, m_rootPath);
        display = m_rootPath + "/" + relPath.generic_string();
    } else {
        display = m_currentDir.generic_string();
    }

    // Show first portion
    float availWidth = ImGui::GetContentRegionAvail().x - 320.0f; // leave room for other toolbar widgets
    if (availWidth > 100.0f) {
        ImGui::TextUnformatted(display.c_str());
    } else {
        ImGui::TextUnformatted(m_currentDir.filename().string().c_str());
    }
}

// ── Grid view ───────────────────────────────────────────────────────────────

void AssetBrowser::renderGridView() {
    float thumbWithPad = static_cast<float>(m_thumbnailSize) + 16.0f;
    int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / thumbWithPad));
    ImGui::Columns(cols, nullptr, false);

    for (usize i = 0; i < m_filteredEntries.size(); ++i) {
        auto& entry = m_filteredEntries[i];

        ImGui::BeginGroup();

        if (entry.isDirectory) {
            ImGui::Text("[dir]");
        } else {
            ImGui::Text("%s", iconForType(entry.type, entry.path));
        }
        ImGui::TextUnformatted(entry.name.c_str());

        // Selection
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_selectedEntry = static_cast<int>(i);
        }

        // Double-click to enter directory
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (entry.isDirectory) {
                navigateTo(entry.path);
                ImGui::EndGroup();
                break;
            }
        }

        // Drag-drop source
        if (!entry.isDirectory && ImGui::BeginDragDropSource()) {
            std::string pathStr = entry.path.string();
            ImGui::SetDragDropPayload("ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
            ImGui::Text("%s", entry.name.c_str());
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
}

// ── List view ───────────────────────────────────────────────────────────────

void AssetBrowser::renderListView() {
    // Table header
    ImGui::Columns(4, "asset_list", false);
    ImGui::Text("Name");   ImGui::NextColumn();
    ImGui::Text("Type");   ImGui::NextColumn();
    ImGui::Text("Size");   ImGui::NextColumn();
    ImGui::Text("Kind");   ImGui::NextColumn();
    ImGui::Separator();

    for (usize i = 0; i < m_filteredEntries.size(); ++i) {
        auto& entry = m_filteredEntries[i];

        // Name column
        if (entry.isDirectory) {
            ImGui::Text("[dir] %s", entry.name.c_str());
        } else {
            ImGui::Text("%s %s", iconForType(entry.type, entry.path), entry.name.c_str());
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_selectedEntry = static_cast<int>(i);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (entry.isDirectory) {
                navigateTo(entry.path);
                break;
            }
        }

        if (!entry.isDirectory && ImGui::BeginDragDropSource()) {
            std::string pathStr = entry.path.string();
            ImGui::SetDragDropPayload("ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
            ImGui::Text("%s", entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::NextColumn();

        // Type column
        if (entry.isDirectory) {
            ImGui::Text("Folder");
        } else {
            if (entry.path.extension() == ".lua") {
                ImGui::Text("Script");
            } else {
                switch (entry.type) {
                    case AssetType::Texture: ImGui::Text("Texture");  break;
                    case AssetType::Audio:   ImGui::Text("Audio");    break;
                    case AssetType::Mesh:    ImGui::Text("Mesh");     break;
                    case AssetType::Scene:   ImGui::Text("Scene");    break;
                    default:                 ImGui::Text("Unknown");  break;
                }
            }
        }
        ImGui::NextColumn();

        // Size column
        if (entry.isDirectory) {
            ImGui::TextUnformatted("-");
        } else {
            f32 sizeKB = static_cast<f32>(entry.fileSize) / 1024.0f;
            if (sizeKB < 1024.0f) {
                ImGui::Text("%.1f KB", sizeKB);
            } else {
                ImGui::Text("%.2f MB", sizeKB / 1024.0f);
            }
        }
        ImGui::NextColumn();

        // Kind column
        if (entry.isDirectory) {
            ImGui::TextUnformatted("dir");
        } else {
            ImGui::TextUnformatted(entry.path.extension().string().c_str());
        }
        ImGui::NextColumn();
    }

    ImGui::Columns(1);
}

// ── Context menu ────────────────────────────────────────────────────────────

void AssetBrowser::renderContextMenu() {
    if (ImGui::BeginPopupContextWindow()) {
        if (ImGui::MenuItem("New Folder")) {
            std::error_code ec;
            std::filesystem::create_directory(m_currentDir / "NewFolder", ec);
            // If created successfully, refresh
            if (!ec) {
                refresh();
            }
        }
        ImGui::EndPopup();
    }
}

// ── Main render ─────────────────────────────────────────────────────────────

void AssetBrowser::render([[maybe_unused]] EditorContext& ctx) {
    if (!m_open) return;

    if (ImGui::Begin("Asset Browser", &m_open)) {

        renderToolbar();
        ImGui::Separator();

        ImGui::BeginChild("asset_content", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

        if (m_viewMode == ViewMode::Grid) {
            renderGridView();
        } else {
            renderListView();
        }

        renderContextMenu();

        ImGui::EndChild();
    }
    ImGui::End();
}

// ── Drag-drop target ─────────────────────────────────────────────────────────

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
