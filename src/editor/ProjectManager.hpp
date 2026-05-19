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

    const ProjectConfig&              GetCurrentProject() const { return m_CurrentConfig; }
    const std::vector<std::filesystem::path>& GetRecentProjects() const { return m_RecentProjects; }

    // Save the current project configuration back to disk.
    bool SaveProjectFile(const ProjectConfig& config);

    // Override the recent projects file path (used for testing).
    // Reloads from the new path immediately.
    void SetRecentProjectsPath(std::filesystem::path path) {
        m_RecentProjectsFile = std::move(path);
        LoadRecentProjects();
    }

    // Returns the default platform-specific path for the recent projects file.
    static std::filesystem::path DefaultRecentPath();

private:
    bool LoadProjectFile(const std::filesystem::path& path, ProjectConfig& out);
    void CreateDirectoryStructure(const std::filesystem::path& root);
    void UpdateRecentProjects(const std::filesystem::path& path);
    void LoadRecentProjects();
    void SaveRecentProjects();

    ProjectConfig                 m_CurrentConfig;
    std::vector<std::filesystem::path> m_RecentProjects;
    std::filesystem::path              m_RecentProjectsFile;
};

} // namespace Caffeine::Editor
