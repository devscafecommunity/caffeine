#pragma once
#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include "editor/EditorContext.hpp"
#include "editor/ProjectManager.hpp"

#include <vector>
#include <string>
#include <filesystem>
#include <cstring>
#include <optional>
#include <algorithm>
#include <functional>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

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

    // ── Browse mode ────────────────────────────────────────────────────
    enum class BrowseMode : u8 {
        Filesystem,
        CapFile
    };

    enum class AssetScope : u8 {
        Raw,
        Processed
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

    void setOnScriptOpen(std::function<void(const std::filesystem::path&)> cb) {
        m_onScriptOpen = std::move(cb);
    }

    // ── Data layer ─────────────────────────────────────────────────────
    void init(const char* rootPath);
    void init(const ProjectConfig& projectConfig);
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

    // CAP file browsing
    void loadCapFile(const std::filesystem::path& capPath);
    BrowseMode browseMode() const { return m_browseMode; }
    AssetScope assetScope() const { return m_assetScope; }
    void setAssetScope(AssetScope scope);

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
    void renderPreviewPane();
    void renderContextMenu();
    const char* iconForType(AssetType type, const std::filesystem::path& path = {});
    bool importPath(const std::filesystem::path& sourcePath, bool autoConvert = true);
    bool convertRawAssetToCaf(const std::filesystem::path& rawPath, std::string* errorMessage = nullptr);
    usize convertAllSupportedAssets();
    bool packCurrentProjectCap(std::string* errorMessage = nullptr);
    bool openCurrentProjectCap(std::string* errorMessage = nullptr);
    bool isSupportedRawAsset(const std::filesystem::path& path) const;
    void setStatusMessage(const std::string& message, bool isError = false);

    int m_selectedEntry = -1;
    bool m_autoConvertOnImport = true;
    bool m_showImportFilePicker = false;
    bool m_showImportFolderPicker = false;
    bool m_showAssetCreator = false;
    int  m_assetCreatorCategory = 0;
    bool m_showNamingPopup = false;
    char m_assetNamingBuf[256] = {};
    int  m_pendingCreateType = -1;
    std::filesystem::path m_clipboardPath;
    bool m_clipboardIsCut = false;
    int  m_renamingEntry = -1;
    char m_renameBuf[256] = {};
    void renderAssetCreatorModal();
    void renderNamingPopup();
    void renderRenamePopup();
    std::string m_statusMessage;
    bool m_statusIsError = false;
    #endif

private:
    // ── Internal ───────────────────────────────────────────────────────
    void applySearchFilter();
    std::filesystem::path rootForScope(AssetScope scope) const;
    void switchToFilesystemRoot(const std::filesystem::path& root);

    bool m_open = true;
    std::string m_rootPath = "assets";
    std::filesystem::path m_projectRoot;
    std::filesystem::path m_rawRoot;
    std::filesystem::path m_processedRoot;
    std::filesystem::path m_currentDir;
    std::vector<Entry> m_entries;
    std::vector<Entry> m_filteredEntries;
    std::vector<std::filesystem::path> m_pathHistory;
    std::string m_searchFilter;
    ViewMode m_viewMode = ViewMode::Grid;
    u32 m_thumbnailSize = 64;
    AssetScope m_assetScope = AssetScope::Raw;
    
    BrowseMode m_browseMode = BrowseMode::Filesystem;
    std::filesystem::path m_currentCapPath;

    std::function<void(const std::filesystem::path&)> m_onScriptOpen;
};

} // namespace Caffeine::Editor
