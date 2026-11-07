#include "editor/EditorTheme.hpp"
#include "editor/EditorPaths.hpp"

#include "caffeine/ui/Theme.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

f32 EditorTheme::s_fontSize = 14.0f;
bool EditorTheme::s_darkMode = true;
bool EditorTheme::s_fontsLoaded = false;

void EditorTheme::init(const EditorThemeSettings& settings) {
    EditorPaths::init();
    apply(settings);
}

void EditorTheme::apply(const EditorThemeSettings& settings) {
#ifdef CF_HAS_IMGUI
    s_darkMode = settings.darkMode;
    s_fontSize = settings.fontSize;
    applyPalette(s_darkMode);
    loadFonts(s_fontSize);
    ImGui::GetIO().FontGlobalScale = 1.0f;
#endif
}

void EditorTheme::setFontSize(f32 size) {
    EditorThemeSettings settings;
    settings.darkMode = s_darkMode;
    settings.fontSize = size;
    apply(settings);
}

void EditorTheme::applyPalette(bool dark) {
#ifdef CF_HAS_IMGUI
    // Reset to the built-in palette, then merge the project/user theme if one
    // exists. The JSON document is intentionally partial: it only overrides the
    // tokens it declares, so new tokens keep working after an upgrade.
    UIKit::resetTheme(dark);

    const std::filesystem::path themePath = EditorPaths::resolve("ui/theme.json");
    if (dark && std::filesystem::exists(themePath)) {
        std::string error;
        if (!UIKit::loadTheme(themePath.string(), &error)) {
            std::fprintf(stderr, "[theme] %s\n", error.c_str());
        }
    }

    UIKit::tokens().dark = dark;
    UIKit::tokens().fontSize = s_fontSize;
    UIKit::applyTheme();
#else
    (void)dark;
#endif
}

void EditorTheme::loadFonts(f32 size) {
#ifdef CF_HAS_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const std::filesystem::path regular =
        EditorPaths::fontPath("static/jetbrains-mono-latin-400-normal.ttf");
    const std::filesystem::path bold =
        EditorPaths::fontPath("static/jetbrains-mono-latin-600-normal.ttf");

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;

    ImFont* mainFont = nullptr;
    if (std::filesystem::exists(regular)) {
        mainFont = io.Fonts->AddFontFromFileTTF(regular.string().c_str(), size, &cfg);
    }
    if (!mainFont) {
        mainFont = io.Fonts->AddFontDefault(&cfg);
    }

    if (std::filesystem::exists(bold)) {
        ImFontConfig boldCfg = cfg;
        boldCfg.MergeMode = false;
        io.Fonts->AddFontFromFileTTF(bold.string().c_str(), size, &boldCfg);
    }

    s_fontsLoaded = mainFont != nullptr;
#endif
}

}  // namespace Caffeine::Editor
