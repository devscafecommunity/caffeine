#pragma once

#include <filesystem>
#include <string>

namespace Caffeine::Editor {

// Resolves editor chrome assets (fonts, icons, spinners) independent of project assets.
class EditorPaths {
public:
    static void init();

    static const std::filesystem::path& root() { return s_root; }
    static bool isReady() { return !s_root.empty(); }

    static std::filesystem::path resolve(const std::string& relativePath);
    static std::filesystem::path fontPath(const std::string& relativePath);
    static std::filesystem::path iconPath(const std::string& name);
    static std::filesystem::path spinnerPath(const std::string& name);
    static std::filesystem::path brandLogoPath();

private:
    static std::filesystem::path s_root;
    static std::filesystem::path findAssetsRoot();
};

} // namespace Caffeine::Editor
