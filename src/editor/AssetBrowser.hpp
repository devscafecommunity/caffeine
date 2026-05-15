#pragma once
#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include "editor/EditorContext.hpp"

#include <vector>
#include <string>
#include <filesystem>
#include <cstring>
#include <optional>
#include <algorithm>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

// ============================================================================
// AssetBrowser v2 — Data/UI separated editor panel.
//
// Data layer (always compiled):
//   init, refresh, search, navigation, view mode, thumbnail size
//
// UI layer (requires CF_HAS_IMGUI):
//   render — toolbar, breadcrumbs, grid/list view, preview, context menu
// ============================================================================
class AssetBrowser {
public:
    // ── View mode ──────────────────────────────────────────────────────
    enum class ViewMode : u8 {
        Grid,
        List
    };

    // ── File entry ─────────────────────────────────────────────────────
    struct Entry {
        std::filesystem::path path;
        AssetType             type       = AssetType::Unknown;
        std::string           name;
        u64                   fileSize   = 0;
        bool                  isDirectory = false;
    };

    AssetBrowser() = default;

    // ── Open / close (always available) ────────────────────────────────
    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

    // ── Data layer ─────────────────────────────────────────────────────
    void init(const char* rootPath);
    void refresh();

    // Search
    void setSearchFilter(const char* filter);
    const std::string& searchFilter() const;

    // Navigation
    void navigateTo(const std::filesystem::path& dir);
    void navigateBack();
    bool canGoBack() const;
    const std::filesystem::path& currentPath() const;

    // View mode
    void     setViewMode(ViewMode mode);
    ViewMode viewMode() const;

    // Thumbnail size (pixels, default 64)
    void setThumbnailSize(u32 size);
    u32  thumbnailSize() const;

    // Entry access (returns filtered entries — respects search filter)
    const std::vector<Entry>& entries() const;
    usize entryCount() const;

    // ── UI layer (requires ImGui) ─────────────────────────────────────
    #ifdef CF_HAS_IMGUI
    void render(EditorContext& ctx);
    std::optional<std::filesystem::path> getDroppedAsset() const;

    private:
    // UI helpers
    void renderToolbar();
    void renderBreadcrumbs();
    void renderGridView();
    void renderListView();
    void renderContextMenu();
    const char* iconForType(AssetType type);

    int m_selectedEntry = -1;
    #endif

private:
    // ── Internal ───────────────────────────────────────────────────────
    void applySearchFilter();

    bool m_open = true;
    std::string m_rootPath = "assets";
    std::filesystem::path m_currentDir;
    std::vector<Entry> m_entries;
    std::vector<Entry> m_filteredEntries;
    std::vector<std::filesystem::path> m_pathHistory;
    std::string m_searchFilter;
    ViewMode m_viewMode = ViewMode::Grid;
    u32 m_thumbnailSize = 64;
};

} // namespace Caffeine::Editor
