#include "editor/AssetBrowser.hpp"
#include "editor/CapLoader.hpp"
#include "editor/FilePicker.hpp"
#include "assets/TextureCompiler.hpp"
#ifdef CF_HAS_CAF_PACK
#include "caf-pack/Packer.hpp"
#include "caf-pack/HeaderGenerator.hpp"
#endif
#include "stb/stb_image.h"

#include <fstream>
#include <system_error>

// ═════════════════════════════════════════════════════════════════════════════
// Data layer — always compiled (no ImGui dependency)
// ═════════════════════════════════════════════════════════════════════════════

namespace Caffeine::Editor {

// ── Init / Refresh ──────────────────────────────────────────────────────────

void AssetBrowser::init(const char* rootPath) {
    m_rootPath = rootPath ? rootPath : "assets";
    m_projectRoot.clear();
    m_rawRoot = std::filesystem::absolute(m_rootPath);
    m_processedRoot.clear();
    m_assetScope = AssetScope::Raw;
    m_currentDir = m_rootPath;
    m_pathHistory.clear();
    refresh();
}

void AssetBrowser::init(const ProjectConfig& projectConfig) {
    m_projectRoot = std::filesystem::absolute(projectConfig.RootPath);
    m_rawRoot = std::filesystem::absolute(m_projectRoot / projectConfig.AssetRawPath);
    m_processedRoot = std::filesystem::absolute(m_projectRoot / projectConfig.AssetProcessedPath);
    m_rootPath = m_rawRoot.string();
    m_assetScope = AssetScope::Raw;
    m_browseMode = BrowseMode::Filesystem;
    m_currentDir = m_rawRoot;
    m_pathHistory.clear();
    std::filesystem::create_directories(m_rawRoot);
    std::filesystem::create_directories(m_processedRoot);
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
    
#ifdef CF_HAS_CAF_PACK
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
#else
    // CAP loading not available without caf-pack
#endif
    
    applySearchFilter();
}

void AssetBrowser::setAssetScope(AssetScope scope) {
    if (scope == m_assetScope && m_browseMode == BrowseMode::Filesystem) {
        return;
    }

    m_assetScope = scope;
    switchToFilesystemRoot(rootForScope(scope));
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

std::filesystem::path AssetBrowser::rootForScope(AssetScope scope) const {
    const auto& root = (scope == AssetScope::Processed) ? m_processedRoot : m_rawRoot;
    if (!root.empty()) {
        return root;
    }
    return std::filesystem::absolute(m_rootPath);
}

void AssetBrowser::switchToFilesystemRoot(const std::filesystem::path& root) {
    m_browseMode = BrowseMode::Filesystem;
    m_currentDir = root;
    m_rootPath = root.string();
    m_pathHistory.clear();
    refresh();
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

    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
        m_showImportFilePicker = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Import Folder...")) {
        m_showImportFolderPicker = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto .caf", &m_autoConvertOnImport);

    ImGui::SameLine();
    if (ImGui::Button("Convert Raw -> .caf")) {
        usize converted = convertAllSupportedAssets();
        if (converted > 0) {
            setStatusMessage("Converted " + std::to_string(converted) + " asset(s) to assets/processed");
        } else {
            setStatusMessage("No supported raw assets found to convert", true);
        }
        if (m_assetScope == AssetScope::Processed) {
            refresh();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Pack CAP")) {
        std::string error;
        if (packCurrentProjectCap(&error)) {
            setStatusMessage("Packed game.cap successfully");
        } else {
            setStatusMessage(error.empty() ? "Failed to pack game.cap" : error, true);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Open CAP")) {
        std::string error;
        if (openCurrentProjectCap(&error)) {
            setStatusMessage("Opened game.cap in CAP view");
        } else {
            setStatusMessage(error.empty() ? "Failed to open game.cap" : error, true);
        }
    }

    ImGui::SameLine();
    bool rawSelected = (m_assetScope == AssetScope::Raw);
    if (ImGui::Selectable("Raw", rawSelected, ImGuiSelectableFlags_None, ImVec2(42.0f, 0.0f))) {
        setAssetScope(AssetScope::Raw);
    }

    ImGui::SameLine();
    bool processedSelected = (m_assetScope == AssetScope::Processed);
    if (ImGui::Selectable("Processed", processedSelected, ImGuiSelectableFlags_None, ImVec2(78.0f, 0.0f))) {
        setAssetScope(AssetScope::Processed);
    }
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

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (entry.isDirectory) {
                navigateTo(entry.path);
                ImGui::EndGroup();
                break;
            } else if (entry.path.extension() == ".lua" && m_onScriptOpen) {
                m_onScriptOpen(entry.path);
            }
        }

        // Drag-drop source
        if (!entry.isDirectory && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
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
            } else if (entry.path.extension() == ".lua" && m_onScriptOpen) {
                m_onScriptOpen(entry.path);
            }
        }

        if (!entry.isDirectory && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
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
            if (!ec) {
                refresh();
            }
        }
        if (ImGui::MenuItem("New Script")) {
            std::filesystem::path newPath = m_currentDir / "NewScript.lua";
            int counter = 1;
            while (std::filesystem::exists(newPath)) {
                newPath = m_currentDir / ("NewScript" + std::to_string(counter++) + ".lua");
            }
            std::ofstream f(newPath);
            if (f.is_open()) {
                f << "function onCreate(entity)\nend\n\nfunction onUpdate(entity, dt)\nend\n\nfunction onDestroy(entity)\nend\n\nfunction onCollision(entity, other)\nend\n";
                f.close();
                refresh();
                if (m_onScriptOpen) {
                    m_onScriptOpen(newPath);
                }
            }
        }
        ImGui::EndPopup();
    }
}

void AssetBrowser::renderPreviewPane() {
    ImGui::TextUnformatted("Preview");
    ImGui::Separator();

    if (m_selectedEntry < 0 || static_cast<usize>(m_selectedEntry) >= m_filteredEntries.size()) {
        ImGui::TextDisabled("Select an item to preview");
        return;
    }

    const auto& entry = m_filteredEntries[static_cast<usize>(m_selectedEntry)];
    ImGui::TextWrapped("%s", entry.name.c_str());
    ImGui::Spacing();

    if (entry.isDirectory) {
        ImGui::TextUnformatted("Type: Folder");
        ImGui::TextWrapped("Path: %s", entry.path.string().c_str());
        return;
    }

    const char* typeLabel = "Unknown";
    switch (entry.type) {
        case AssetType::Texture: typeLabel = "Texture"; break;
        case AssetType::Audio:   typeLabel = "Audio"; break;
        case AssetType::Mesh:    typeLabel = "Mesh"; break;
        case AssetType::Scene:   typeLabel = "Scene/CAF"; break;
        default: break;
    }

    ImGui::Text("Type: %s", typeLabel);
    ImGui::Text("Kind: %s", entry.path.extension().string().c_str());
    ImGui::Text("Size: %.2f KB", static_cast<float>(entry.fileSize) / 1024.0f);

    if (m_browseMode == BrowseMode::Filesystem) {
        ImGui::TextWrapped("Path: %s", entry.path.string().c_str());
    } else {
        ImGui::TextWrapped("Source CAP: %s", m_currentCapPath.string().c_str());
        ImGui::TextWrapped("Asset ID: %s", entry.name.c_str());
    }

    if (entry.type == AssetType::Texture && m_browseMode == BrowseMode::Filesystem) {
        int width = 0;
        int height = 0;
        int channels = 0;
        if (stbi_info(entry.path.string().c_str(), &width, &height, &channels)) {
            ImGui::Text("Resolution: %d x %d", width, height);
            ImGui::Text("Channels: %d", channels);
        }
    }
}

bool AssetBrowser::isSupportedRawAsset(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga";
}

bool AssetBrowser::convertRawAssetToCaf(const std::filesystem::path& rawPath, std::string* errorMessage) {
    if (m_rawRoot.empty() || m_processedRoot.empty()) {
        if (errorMessage) *errorMessage = "Asset pipeline paths are not configured";
        return false;
    }

    if (!isSupportedRawAsset(rawPath)) {
        if (errorMessage) *errorMessage = "No compiler available for " + rawPath.extension().string();
        return false;
    }

    std::error_code ec;
    auto relativePath = std::filesystem::relative(rawPath, m_rawRoot, ec);
    if (ec) {
        relativePath = rawPath.filename();
    }

    std::filesystem::path destPath = m_processedRoot / relativePath;
    destPath.replace_extension(".caf");
    std::filesystem::create_directories(destPath.parent_path(), ec);
    if (ec) {
        if (errorMessage) *errorMessage = "Failed to prepare output directory";
        return false;
    }

    Caffeine::Assets::TextureCompiler compiler;
    Caffeine::Assets::AssetImportContext importCtx;
    importCtx.SourcePath = rawPath;
    importCtx.DestinationPath = destPath;

    if (!compiler.Compile(importCtx)) {
        if (errorMessage) {
            *errorMessage = importCtx.Logs.empty() ? "Texture conversion failed" : importCtx.Logs.back();
        }
        return false;
    }

    return true;
}

usize AssetBrowser::convertAllSupportedAssets() {
    if (m_rawRoot.empty() || !std::filesystem::exists(m_rawRoot)) {
        return 0;
    }

    usize converted = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_rawRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!isSupportedRawAsset(entry.path())) {
            continue;
        }
        std::string error;
        if (convertRawAssetToCaf(entry.path(), &error)) {
            ++converted;
        }
    }
    return converted;
}

bool AssetBrowser::packCurrentProjectCap(std::string* errorMessage) {
    if (m_projectRoot.empty() || m_rawRoot.empty()) {
        if (errorMessage) *errorMessage = "Project paths are not configured";
        return false;
    }

    if (!std::filesystem::exists(m_rawRoot)) {
        if (errorMessage) *errorMessage = "assets/raw does not exist";
        return false;
    }

#ifdef CF_HAS_CAF_PACK
    std::filesystem::path capPath = m_projectRoot / "game.cap";
    std::filesystem::path headerPath = m_processedRoot.empty()
        ? (m_projectRoot / "game_assets.hpp")
        : (m_processedRoot / "game_assets.hpp");

    std::error_code ec;
    std::filesystem::create_directories(headerPath.parent_path(), ec);

    CafPack::Packer::Config config;
    config.inputDir = m_rawRoot;
    config.outputFile = capPath;
    config.generateHeader = true;
    config.headerPath = headerPath.string();
    config.compress = false;

    CafPack::Packer packer(config);
    if (!packer.pack()) {
        if (errorMessage) *errorMessage = packer.getError();
        return false;
    }

    std::vector<CafPack::AssetEntry> entries;
    for (const auto& [name, id] : packer.getAssetEntries()) {
        entries.push_back({name, id});
    }
    CafPack::HeaderGenerator::generateHeader(entries, headerPath);
    return true;
#else
    if (errorMessage) *errorMessage = "Asset packing not available - caf-pack submodule not included";
    return false;
#endif
}

bool AssetBrowser::openCurrentProjectCap(std::string* errorMessage) {
    if (m_projectRoot.empty()) {
        if (errorMessage) *errorMessage = "Project path is not configured";
        return false;
    }

    std::filesystem::path capPath = m_projectRoot / "game.cap";
    if (!std::filesystem::exists(capPath)) {
        if (errorMessage) *errorMessage = "game.cap not found. Pack assets first.";
        return false;
    }

    loadCapFile(capPath);
    return true;
}

bool AssetBrowser::importPath(const std::filesystem::path& sourcePath, bool autoConvert) {
    if (sourcePath.empty()) {
        setStatusMessage("Provide a source path to import", true);
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec) || ec) {
        setStatusMessage("Source path does not exist", true);
        return false;
    }

    if (m_rawRoot.empty()) {
        setStatusMessage("Raw asset path is not configured", true);
        return false;
    }

    std::filesystem::create_directories(m_rawRoot, ec);
    if (ec) {
        setStatusMessage("Failed to create assets/raw", true);
        return false;
    }

    usize copiedCount = 0;
    usize convertedCount = 0;

    auto importSingleFile = [&](const std::filesystem::path& filePath, const std::filesystem::path& targetPath) {
        std::filesystem::create_directories(targetPath.parent_path(), ec);
        if (ec) return false;
        std::filesystem::copy_file(filePath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return false;
        ++copiedCount;
        if (autoConvert && isSupportedRawAsset(targetPath)) {
            std::string error;
            if (convertRawAssetToCaf(targetPath, &error)) {
                ++convertedCount;
            }
        }
        return true;
    };

    if (std::filesystem::is_directory(sourcePath)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto rel = std::filesystem::relative(entry.path(), sourcePath, ec);
            if (ec) rel = entry.path().filename();
            if (!importSingleFile(entry.path(), m_rawRoot / rel)) {
                setStatusMessage("Failed to import some files from folder", true);
                refresh();
                return false;
            }
        }
    } else {
        if (!importSingleFile(sourcePath, m_rawRoot / sourcePath.filename())) {
            setStatusMessage("Failed to import file", true);
            refresh();
            return false;
        }
    }

    refresh();
    if (m_assetScope == AssetScope::Processed && convertedCount > 0) {
        setAssetScope(AssetScope::Processed);
    }

    std::string message = "Imported " + std::to_string(copiedCount) + " file(s)";
    if (convertedCount > 0) {
        message += " — converted " + std::to_string(convertedCount) + " to .caf";
    }
    setStatusMessage(message, false);
    return true;
}

void AssetBrowser::setStatusMessage(const std::string& message, bool isError) {
    m_statusMessage = message;
    m_statusIsError = isError;
}

// ── Main render ─────────────────────────────────────────────────────────────

void AssetBrowser::render([[maybe_unused]] EditorContext& ctx) {
    if (!m_open) return;

    if (ImGui::Begin("Asset Browser", &m_open)) {

        renderToolbar();
        ImGui::Separator();

        if (m_showImportFilePicker) {
            std::filesystem::path startPath = m_rawRoot.empty() ? std::filesystem::current_path() : m_rawRoot;
            if (auto selected = FilePicker::pickPath(FilePicker::Mode::PickFile, "Import Asset File", startPath)) {
                importPath(selected.value(), m_autoConvertOnImport);
                m_showImportFilePicker = false;
            } else if (FilePicker::consumeCloseEvent("Import Asset File")) {
                m_showImportFilePicker = false;
            }
        }

        if (m_showImportFolderPicker) {
            std::filesystem::path startPath = m_rawRoot.empty() ? std::filesystem::current_path() : m_rawRoot;
            if (auto selected = FilePicker::pickPath(FilePicker::Mode::PickFolder, "Import Asset Folder", startPath)) {
                importPath(selected.value(), m_autoConvertOnImport);
                m_showImportFolderPicker = false;
            } else if (FilePicker::consumeCloseEvent("Import Asset Folder")) {
                m_showImportFolderPicker = false;
            }
        }

        if (!m_statusMessage.empty()) {
            ImVec4 color = m_statusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.45f, 0.9f, 0.45f, 1.0f);
            ImGui::TextColored(color, "%s", m_statusMessage.c_str());
        }

        ImGui::BeginChild("asset_content", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

        const float previewWidth = 280.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float leftWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - previewWidth - spacing);

        ImGui::BeginChild("asset_list_area", ImVec2(leftWidth, 0), false);
        if (m_viewMode == ViewMode::Grid) {
            renderGridView();
        } else {
            renderListView();
        }
        renderContextMenu();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("asset_preview_area", ImVec2(0, 0), true);
        renderPreviewPane();
        ImGui::EndChild();

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
