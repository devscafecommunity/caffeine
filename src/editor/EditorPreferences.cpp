#include "editor/EditorPreferences.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Caffeine::Editor {

namespace {

std::filesystem::path configDirectory() {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData) return std::filesystem::path(appData) / "Caffeine";
#else
    const char* home = std::getenv("HOME");
    if (home) return std::filesystem::path(home) / ".config" / "caffeine";
#endif
    return std::filesystem::path(".caffeine");
}

bool readStringField(const std::string& json, const char* key, std::string& out) {
    const std::string needle = std::string("\"") + key + "\": \"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    const size_t start = pos + needle.size();
    const size_t end = json.find('"', start);
    if (end == std::string::npos) return false;
    out = json.substr(start, end - start);
    return true;
}

bool readBoolField(const std::string& json, const char* key, bool& out) {
    const std::string needle = std::string("\"") + key + "\": ";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    const size_t start = pos + needle.size();
    if (json.compare(start, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(start, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

bool readIntField(const std::string& json, const char* key, int& out) {
    const std::string needle = std::string("\"") + key + "\": ";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    const size_t start = pos + needle.size();
    out = std::atoi(json.c_str() + start);
    return true;
}

bool readFloatField(const std::string& json, const char* key, f32& out) {
    const std::string needle = std::string("\"") + key + "\": ";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    const size_t start = pos + needle.size();
    out = static_cast<f32>(std::atof(json.c_str() + start));
    return true;
}

}  // namespace

std::filesystem::path EditorPreferences::preferencesPath() {
    return configDirectory() / "editor_preferences.json";
}

EditorPreferences EditorPreferences::load() {
    EditorPreferences prefs;
    std::ifstream file(preferencesPath());
    if (!file.is_open()) return prefs;

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    readStringField(json, "active_layout_profile", prefs.activeLayoutProfile);
    readBoolField(json, "vsync_enabled", prefs.vsyncEnabled);
    readIntField(json, "font_size", prefs.fontSize);
    readBoolField(json, "dark_mode", prefs.darkMode);
    readBoolField(json, "auto_save_enabled", prefs.autoSaveEnabled);
    readIntField(json, "auto_save_interval", prefs.autoSaveInterval);
    readFloatField(json, "default_grid_size", prefs.defaultGridSize);
    readBoolField(json, "show_grid", prefs.showGrid);
    readBoolField(json, "snap_enabled", prefs.snapEnabled);
    readFloatField(json, "snap_increment", prefs.snapIncrement);
    readBoolField(json, "uniform_scale_default", prefs.uniformScaleDefault);
    readFloatField(json, "camera_move_speed", prefs.cameraMoveSpeed);
    readFloatField(json, "camera_orbit_speed", prefs.cameraOrbitSpeed);
    readBoolField(json, "show_fps_status_bar", prefs.showFPSInStatusBar);
    readBoolField(json, "confirm_scene_close", prefs.confirmOnSceneClose);
    readBoolField(json, "reopen_last_scene", prefs.reopenLastSceneOnStartup);
    return prefs;
}

bool EditorPreferences::save() const {
    std::error_code ec;
    std::filesystem::create_directories(preferencesPath().parent_path(), ec);

    std::ofstream file(preferencesPath());
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"active_layout_profile\": \"" << activeLayoutProfile << "\",\n";
    file << "  \"vsync_enabled\": " << (vsyncEnabled ? "true" : "false") << ",\n";
    file << "  \"font_size\": " << fontSize << ",\n";
    file << "  \"dark_mode\": " << (darkMode ? "true" : "false") << ",\n";
    file << "  \"auto_save_enabled\": " << (autoSaveEnabled ? "true" : "false") << ",\n";
    file << "  \"auto_save_interval\": " << autoSaveInterval << ",\n";
    file << "  \"default_grid_size\": " << defaultGridSize << ",\n";
    file << "  \"show_grid\": " << (showGrid ? "true" : "false") << ",\n";
    file << "  \"snap_enabled\": " << (snapEnabled ? "true" : "false") << ",\n";
    file << "  \"snap_increment\": " << snapIncrement << ",\n";
    file << "  \"uniform_scale_default\": " << (uniformScaleDefault ? "true" : "false") << ",\n";
    file << "  \"camera_move_speed\": " << cameraMoveSpeed << ",\n";
    file << "  \"camera_orbit_speed\": " << cameraOrbitSpeed << ",\n";
    file << "  \"show_fps_status_bar\": " << (showFPSInStatusBar ? "true" : "false") << ",\n";
    file << "  \"confirm_scene_close\": " << (confirmOnSceneClose ? "true" : "false") << ",\n";
    file << "  \"reopen_last_scene\": " << (reopenLastSceneOnStartup ? "true" : "false") << "\n";
    file << "}\n";
    return file.good();
}

}  // namespace Caffeine::Editor
