#include "editor/EditorPaths.hpp"

#include <vector>

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

namespace Caffeine::Editor {

std::filesystem::path EditorPaths::s_root;

void EditorPaths::init() {
    if (!s_root.empty()) return;
    s_root = findAssetsRoot();
}

std::filesystem::path EditorPaths::findAssetsRoot() {
    std::vector<std::filesystem::path> candidates;

#ifdef CAFFEINE_SOURCE_DIR
    candidates.push_back(std::filesystem::path(CAFFEINE_SOURCE_DIR) / "assets");
#endif

    candidates.push_back(std::filesystem::current_path() / "assets");
    candidates.push_back(std::filesystem::current_path() / ".." / "assets");

#ifdef __linux__
    char exePath[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        candidates.push_back(exeDir / "assets");
        candidates.push_back(exeDir / ".." / "assets");
    }
#endif

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate / "jetbrains-mono", ec) &&
            std::filesystem::exists(candidate / "icons", ec)) {
            return std::filesystem::weakly_canonical(candidate, ec);
        }
    }

    return {};
}

std::filesystem::path EditorPaths::resolve(const std::string& relativePath) {
    if (s_root.empty()) init();
    if (s_root.empty()) return std::filesystem::path(relativePath);
    return s_root / relativePath;
}

std::filesystem::path EditorPaths::fontPath(const std::string& relativePath) {
    return resolve((std::filesystem::path("jetbrains-mono") / relativePath).string());
}

std::filesystem::path EditorPaths::iconPath(const std::string& name) {
    std::string file = name;
    if (file.find(".svg") == std::string::npos) {
        file = "line-md--" + file + ".svg";
    }
    if (file.find('/') == std::string::npos) {
        return resolve((std::filesystem::path("icons") / file).string());
    }
    return resolve(file);
}

std::filesystem::path EditorPaths::brandLogoPath() {
    return resolve("café_no_back_logo.png");
}

std::filesystem::path EditorPaths::spinnerPath(const std::string& name) {
    std::string file = name;
    if (file.find(".svg") == std::string::npos) {
        file = "svg-spinners--" + file + ".svg";
    }
    return resolve((std::filesystem::path("spinners") / file).string());
}

std::filesystem::path EditorPaths::skyboxPath(const std::string& fileName) {
    return resolve((std::filesystem::path("kenney_skyboxes") / "Skyboxes" / fileName).string());
}

} // namespace Caffeine::Editor
