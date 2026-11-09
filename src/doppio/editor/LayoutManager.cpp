#include "editor/LayoutManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Caffeine::Editor {

LayoutManager::LayoutManager() : m_currentProfile(LayoutProfile::defaultLayout()) {
    // Initialize with built-in presets
    m_profiles.push_back(LayoutProfile::defaultLayout());
    m_profiles.push_back(LayoutProfile::verticalLayout());
    m_profiles.push_back(LayoutProfile::horizontalLayout());
    m_profiles.push_back(LayoutProfile::compactLayout());
    m_profiles.push_back(LayoutProfile::fullscreenLayout());

    // Try to load user-saved profiles
    loadProfiles();
}

std::filesystem::path LayoutManager::getProfilesDirectory() const {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData) {
        return std::filesystem::path(appData) / "Caffeine" / "layouts";
    }
    return std::filesystem::path("Caffeine") / "layouts";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "caffeine" / "layouts";
    }
    return std::filesystem::path(".caffeine") / "layouts";
#endif
}

std::filesystem::path LayoutManager::getProfilePath(const std::string& name) const {
    auto dir = getProfilesDirectory();
    return dir / (name + ".layout");
}

bool LayoutManager::loadProfiles() {
    auto dir = getProfilesDirectory();
    if (!std::filesystem::exists(dir)) {
        return true; // No custom profiles yet, that's fine
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".layout") {
                std::ifstream file(entry.path());
                if (!file.is_open()) continue;

                std::stringstream buffer;
                buffer << file.rdbuf();
                auto profile = deserializeProfile(buffer.str());

                if (profile) {
                    // Don't duplicate built-in profiles
                    auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                        [&](const LayoutProfile& p) { return p.name == profile->name; });
                    if (it == m_profiles.end()) {
                        m_profiles.push_back(*profile);
                    }
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool LayoutManager::saveProfile(const LayoutProfile& profile) {
    auto dir = getProfilesDirectory();
    std::filesystem::create_directories(dir);

    auto path = getProfilePath(profile.name);
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << serializeProfile(profile);
    file.close();

    // Add to list if not already there
    auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
        [&](const LayoutProfile& p) { return p.name == profile.name; });
    if (it == m_profiles.end()) {
        m_profiles.push_back(profile);
    } else {
        *it = profile;
    }

    return true;
}

bool LayoutManager::deleteProfile(const std::string& name) {
    auto path = getProfilePath(name);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Remove from list
    auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
        [&](const LayoutProfile& p) { return p.name == name; });
    if (it != m_profiles.end()) {
        m_profiles.erase(it);
    }

    return !ec;
}

std::optional<LayoutProfile> LayoutManager::getProfile(const std::string& name) const {
    auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
        [&](const LayoutProfile& p) { return p.name == name; });
    if (it != m_profiles.end()) {
        return *it;
    }
    return std::nullopt;
}

bool LayoutManager::applyProfile(const std::string& name) {
    auto profile = getProfile(name);
    if (!profile) return false;
    m_currentProfile = *profile;
    return true;
}

std::string LayoutManager::serializeProfile(const LayoutProfile& profile) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"name\": \"" << profile.name << "\",\n";
    json << "  \"hierarchy_open\": " << (profile.hierarchyOpen ? "true" : "false") << ",\n";
    json << "  \"inspector_open\": " << (profile.inspectorOpen ? "true" : "false") << ",\n";
    json << "  \"viewport_open\": " << (profile.viewportOpen ? "true" : "false") << ",\n";
    json << "  \"assets_open\": " << (profile.assetsOpen ? "true" : "false") << ",\n";
    json << "  \"console_open\": " << (profile.consoleOpen ? "true" : "false") << ",\n";
    json << "  \"profiler_open\": " << (profile.profilerOpen ? "true" : "false") << ",\n";
    json << "  \"animation_timeline_open\": " << (profile.animationTimelineOpen ? "true" : "false") << ",\n";
    json << "  \"tilemap_editor_open\": " << (profile.tilemapEditorOpen ? "true" : "false") << ",\n";
    json << "  \"script_editor_open\": " << (profile.scriptEditorOpen ? "true" : "false") << ",\n";
    json << "  \"hierarchy_width\": " << profile.hierarchyWidth << ",\n";
    json << "  \"inspector_width\": " << profile.inspectorWidth << ",\n";
    json << "  \"viewport_width\": " << profile.viewportWidth << "\n";
    json << "}\n";
    return json.str();
}

std::optional<LayoutProfile> LayoutManager::deserializeProfile(const std::string& json) const {
    LayoutProfile profile;

    // Simple JSON parsing for this specific format
    auto getFieldValue = [&json](const std::string& field) -> std::string {
        size_t pos = json.find(field);
        if (pos == std::string::npos) return "";

        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";
        pos++;

        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        size_t end = json.find(",", pos);
        if (end == std::string::npos) {
            end = json.find("}", pos);
        }
        if (end == std::string::npos) return "";

        std::string val = json.substr(pos, end - pos);
        // Trim quotes
        if (!val.empty() && val[0] == '"') val = val.substr(1);
        if (!val.empty() && val.back() == '"') val.pop_back();

        return val;
    };

    auto getBoolField = [&getFieldValue](const std::string& field) -> bool {
        std::string val = getFieldValue(field);
        return val == "true";
    };

    auto getFloatField = [&getFieldValue](const std::string& field) -> f32 {
        std::string val = getFieldValue(field);
        try {
            return std::stof(val);
        } catch (...) {
            return 0.0f;
        }
    };

    profile.name = getFieldValue("\"name\"");
    profile.hierarchyOpen = getBoolField("\"hierarchy_open\"");
    profile.inspectorOpen = getBoolField("\"inspector_open\"");
    profile.viewportOpen = getBoolField("\"viewport_open\"");
    profile.assetsOpen = getBoolField("\"assets_open\"");
    profile.consoleOpen = getBoolField("\"console_open\"");
    profile.profilerOpen = getBoolField("\"profiler_open\"");
    profile.animationTimelineOpen = getBoolField("\"animation_timeline_open\"");
    profile.tilemapEditorOpen = getBoolField("\"tilemap_editor_open\"");
    profile.scriptEditorOpen = getBoolField("\"script_editor_open\"");
    profile.hierarchyWidth = getFloatField("\"hierarchy_width\"");
    profile.inspectorWidth = getFloatField("\"inspector_width\"");
    profile.viewportWidth = getFloatField("\"viewport_width\"");

    if (profile.name.empty()) return std::nullopt;
    return profile;
}

} // namespace Caffeine::Editor
