#include "editor/ProjectManager.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace Caffeine::Editor {

// ── Minimal JSON helpers (project.caffeine only) ─────────────────────────

// Find first non-whitespace character position in a string view.
static usize skipWhitespace(const std::string& s, usize pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                              s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
    return pos;
}

// Extract a quoted string starting at pos (which must point to the opening ").
// Returns the unquoted string and advances pos past the closing ".
static bool readQuotedString(const std::string& s, usize& pos, std::string& out) {
    pos = skipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '"') return false;
    ++pos; // skip opening quote
    out.clear();
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            ++pos; // skip backslash
        }
        out += s[pos];
        ++pos;
    }
    if (pos >= s.size()) return false; // unterminated string
    ++pos; // skip closing quote
    return true;
}

// Find a named key at the top level of a JSON object and read its string value.
// Also handles "key": { "nested": "value" } — extracts nested values prefixed
// with "paths." (e.g. "paths.assets_raw").
static bool readJsonString(const std::string& json, const std::string& key, std::string& out) {
    usize pos = skipWhitespace(json, 0);
    if (pos >= json.size() || json[pos] != '{') return false;
    ++pos; // skip opening brace

    while (pos < json.size()) {
        pos = skipWhitespace(json, pos);
        if (pos >= json.size()) break;
        if (json[pos] == '}') break; // end of object

        std::string foundKey;
        if (!readQuotedString(json, pos, foundKey)) break;
        pos = skipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != ':') break;
        ++pos; // skip colon

        // If this key is "paths", handle nested keys like "paths.assets_raw"
        bool isPaths = (foundKey == "paths");
        if (isPaths) {
            pos = skipWhitespace(json, pos);
            if (pos >= json.size() || json[pos] != '{') break;
            ++pos; // skip opening brace of paths object

            while (pos < json.size()) {
                pos = skipWhitespace(json, pos);
                if (pos >= json.size()) break;
                if (json[pos] == '}') { ++pos; break; }

                std::string subKey;
                if (!readQuotedString(json, pos, subKey)) break;
                pos = skipWhitespace(json, pos);
                if (pos >= json.size() || json[pos] != ':') break;
                ++pos;

                std::string subVal;
                if (!readQuotedString(json, pos, subVal)) break;

                // Check if this nested subKey matches "paths.{remainder}"
                if (key == "paths." + subKey) {
                    out = subVal;
                    return true;
                }

                pos = skipWhitespace(json, pos);
                if (pos < json.size() && json[pos] == ',') ++pos;
            }
            // Continue searching top-level keys after paths block
            pos = skipWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') ++pos;
            continue;
        }

        // Direct match at top level
        if (foundKey == key) {
            return readQuotedString(json, pos, out);
        }

        // Not the key we want — skip the value
        pos = skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == '{') {
            // Skip nested object
            int depth = 1;
            ++pos;
            while (pos < json.size() && depth > 0) {
                if (json[pos] == '{') ++depth;
                else if (json[pos] == '}') --depth;
                ++pos;
            }
        } else {
            std::string ignore;
            readQuotedString(json, pos, ignore);
        }

        pos = skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') ++pos;
    }
    return false;
}

// Serialize a ProjectConfig to JSON string.
static std::string serializeConfig(const ProjectConfig& cfg) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"project_name\": \""     << cfg.Name << "\",\n";
    json << "  \"engine_version\": \""   << cfg.Version << "\",\n";
    json << "  \"paths\": {\n";
    json << "    \"assets_raw\": \""     << cfg.AssetRawPath.generic_string() << "\",\n";
    json << "    \"assets_processed\": \"" << cfg.AssetProcessedPath.generic_string() << "\",\n";
    json << "    \"scripts\": \""        << cfg.ScriptsPath.generic_string() << "\"\n";
    json << "  },\n";
    json << "  \"last_scene\": \""       << cfg.LastScene << "\"\n";
    json << "}\n";
    return json.str();
}

// ── ProjectManager implementation ─────────────────────────────────────────

ProjectManager::ProjectManager() {
    m_RecentProjectsFile = DefaultRecentPath();
    LoadRecentProjects();
}

std::filesystem::path ProjectManager::DefaultRecentPath() {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData) {
        return std::filesystem::path(appData) / "Caffeine" / "recent.txt";
    }
    return std::filesystem::path("Caffeine") / "recent.txt";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "caffeine" / "recent.txt";
    }
    return std::filesystem::path(".caffeine") / "recent.txt";
#endif
}

void ProjectManager::CreateDirectoryStructure(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(root / "assets" / "raw");
    std::filesystem::create_directories(root / "assets" / "processed");
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "build");
}

bool ProjectManager::SaveProjectFile(const ProjectConfig& config) {
    auto filePath = config.RootPath / "project.caffeine";
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << serializeConfig(config);
    return file.good();
}

bool ProjectManager::LoadProjectFile(const std::filesystem::path& path, ProjectConfig& out) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string& json = buffer.str();

    ProjectConfig cfg;
    cfg.RootPath = std::filesystem::absolute(path.parent_path());

    std::string val;

    if (readJsonString(json, "project_name", val))
        cfg.Name = val;
    if (readJsonString(json, "engine_version", val))
        cfg.Version = val;
    if (readJsonString(json, "paths.assets_raw", val))
        cfg.AssetRawPath = val;
    if (readJsonString(json, "paths.assets_processed", val))
        cfg.AssetProcessedPath = val;
    if (readJsonString(json, "paths.scripts", val))
        cfg.ScriptsPath = val;
    if (readJsonString(json, "last_scene", val))
        cfg.LastScene = val;

    out = cfg;
    return true;
}

bool ProjectManager::CreateNewProject(const ProjectConfig& config) {
    if (config.Name.empty()) return false;
    if (config.RootPath.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(config.RootPath, ec);
    if (ec) return false;
    std::filesystem::create_directories(config.RootPath / "assets" / "raw", ec);
    if (ec) return false;
    std::filesystem::create_directories(config.RootPath / "assets" / "processed", ec);
    if (ec) return false;
    std::filesystem::create_directories(config.RootPath / "scripts", ec);
    if (ec) return false;
    std::filesystem::create_directories(config.RootPath / "build", ec);
    if (ec) return false;
    if (!SaveProjectFile(config)) return false;

    m_CurrentConfig = config;
    UpdateRecentProjects(config.RootPath / "project.caffeine");
    return true;
}

bool ProjectManager::OpenProject(const std::filesystem::path& projectFilePath) {
    ProjectConfig cfg;
    if (!LoadProjectFile(projectFilePath, cfg)) return false;
    m_CurrentConfig = cfg;
    UpdateRecentProjects(projectFilePath);
    return true;
}

void ProjectManager::UpdateRecentProjects(const std::filesystem::path& path) {
    auto absPath = std::filesystem::absolute(path);
    auto it = std::find(m_RecentProjects.begin(), m_RecentProjects.end(), absPath);
    if (it != m_RecentProjects.end()) {
        m_RecentProjects.erase(it);
    }
    m_RecentProjects.insert(m_RecentProjects.begin(), absPath);
    if (m_RecentProjects.size() > 10) {
        m_RecentProjects.resize(10);
    }
    SaveRecentProjects();
}

void ProjectManager::LoadRecentProjects() {
    m_RecentProjects.clear();
    std::ifstream file(m_RecentProjectsFile);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            m_RecentProjects.push_back(std::filesystem::path(line));
        }
    }
}

void ProjectManager::SaveRecentProjects() {
    std::error_code ec;
    std::filesystem::create_directories(m_RecentProjectsFile.parent_path(), ec);

    std::ofstream file(m_RecentProjectsFile);
    if (!file.is_open()) return;

    for (const auto& p : m_RecentProjects) {
        file << p.string() << "\n";
    }
}

} // namespace Caffeine::Editor
