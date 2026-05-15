#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include "core/io/FileWatcher.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace Caffeine::Assets {

// ============================================================================
struct AssetImportContext {
    std::filesystem::path SourcePath;
    std::filesystem::path DestinationPath;
    std::vector<std::string> Logs;
    bool Success = false;
};

// ============================================================================
class IAssetCompiler {
public:
    virtual ~IAssetCompiler() = default;
    virtual bool Compile(AssetImportContext& ctx) = 0;
    virtual std::vector<std::string> GetSupportedExtensions() = 0;
};

// ============================================================================
struct ManifestEntry {
    AssetType type;
    std::string relativePath;
    std::string sourcePath;
};

// ============================================================================
class AssetPipeline {
public:
    AssetPipeline();
    ~AssetPipeline();

    AssetPipeline(const AssetPipeline&) = delete;
    AssetPipeline& operator=(const AssetPipeline&) = delete;

    void Initialize(const std::filesystem::path& projectRoot);
    void Update();

    void RegisterCompiler(std::unique_ptr<IAssetCompiler> compiler);
    void Reimport(const std::filesystem::path& path);

    float GetProgress() const { return m_CurrentProgress; }
    bool  IsProcessing() const { return m_Busy; }

    const std::vector<ManifestEntry>& GetManifest() const { return m_Manifest; }

private:
    void OnFileDetected(const std::filesystem::path& path);
    void ProcessQueue();
    void LoadManifest();
    void SaveManifest();
    IAssetCompiler* FindCompiler(const std::string& extension);

    std::filesystem::path m_ProjectRoot;
    std::filesystem::path m_RawDir;
    std::filesystem::path m_ProcessedDir;
    std::filesystem::path m_ManifestPath;

    IO::FileWatcher m_FileWatcher;
    std::queue<std::filesystem::path> m_ImportQueue;
    std::map<std::string, IAssetCompiler*> m_Compilers;
    std::vector<std::unique_ptr<IAssetCompiler>> m_CompilerOwned;

    std::vector<ManifestEntry> m_Manifest;

    bool  m_Busy = false;
    float m_CurrentProgress = 0.0f;
};

} // namespace Caffeine::Assets
