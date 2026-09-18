#pragma once
#include "core/Types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Caffeine::Editor {

// ============================================================================
// ProjectConfig — holds all metadata for a Caffeine Studio project.
//
// A project is defined by a project.caffeine (JSON) file at its root.
// ============================================================================
struct ProjectConfig {
    std::string              Name;
    std::string              Version         = "0.2.0";
    std::filesystem::path    RootPath;
    std::filesystem::path    AssetRawPath    = "assets/raw";
    std::filesystem::path    AssetProcessedPath = "assets/processed";
    std::filesystem::path    ScriptsPath     = "scripts";
    std::string              TemplateType    = "Empty";   // "2D", "3D", "Empty"
    std::string              LastScene;                   // relative path
};

// ============================================================================
// ProjectManager — entry point for project lifecycle operations.
//
// Create new projects from templates, open existing ones, track recent
// projects. All file operations return bool for error reporting.
// ============================================================================
class ProjectManager {
public:
    ProjectManager();
    ~ProjectManager() = default;

    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    // Create a new project at config.RootPath, generating the directory
    // structure and project.caffeine file. Returns false on failure.
    bool CreateNewProject(const ProjectConfig& config);

    // Open an existing project from its project.caffeine path.
    // On success, m_CurrentConfig is populated and the path is added
    // to the recent projects list.
    bool OpenProject(const std::filesystem::path& projectFilePath);

    // Load project metadata without touching the recent-projects list.
    bool TryLoadProject(const std::filesystem::path& projectFilePath, ProjectConfig& out) const;

    const ProjectConfig&              GetCurrentProject() const { return m_CurrentConfig; }
    const std::vector<std::filesystem::path>& GetRecentProjects() const { return m_RecentProjects; }

    // Save the current project configuration back to disk.
    bool SaveProjectFile(const ProjectConfig& config);

    // True when project.caffeine lives inside a packaged game build folder.
    static bool IsPackagedBuildRoot(const std::filesystem::path& root, const ProjectConfig& cfg);

    // Map a build-output project.caffeine back to the editor project file.
    static std::filesystem::path ResolveEditorProjectFile(std::filesystem::path projectFile);

    // Canonical editor project root (never a nested build/ output folder).
    static std::filesystem::path ResolveEditorProjectRoot(std::filesystem::path root);

    // Override the recent projects file path (used for testing).
    // Reloads from the new path immediately.
    void SetRecentProjectsPath(std::filesystem::path path) {
        m_RecentProjectsFile = std::move(path);
        LoadRecentProjects();
    }

    // Returns the default platform-specific path for the recent projects file.
    static std::filesystem::path DefaultRecentPath();

private:
    bool LoadProjectFile(const std::filesystem::path& path, ProjectConfig& out) const;
    void CreateDirectoryStructure(const std::filesystem::path& root);
    void UpdateRecentProjects(const std::filesystem::path& path);
    void PruneRecentProjectsList();
    void LoadRecentProjects();
    void SaveRecentProjects();

    ProjectConfig                 m_CurrentConfig;
    std::vector<std::filesystem::path> m_RecentProjects;
    std::filesystem::path              m_RecentProjectsFile;
};

} // namespace Caffeine::Editor
