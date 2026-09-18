#include "editor/EditorTheme.hpp"
#include "editor/EditorPaths.hpp"

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
    applyStyleVars();
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
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    if (dark) {
        colors[ImGuiCol_Text]                  = ImVec4(0.91f, 0.89f, 0.86f, 1.00f);
        colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.48f, 0.46f, 1.00f);
        colors[ImGuiCol_WindowBg]              = ImVec4(0.11f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.09f, 0.08f, 1.00f);
        colors[ImGuiCol_PopupBg]               = ImVec4(0.13f, 0.12f, 0.11f, 0.98f);
        colors[ImGuiCol_Border]                = ImVec4(0.24f, 0.21f, 0.19f, 1.00f);
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.14f, 0.13f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.17f, 0.16f, 1.00f);
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.20f, 0.18f, 1.00f);
        colors[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.08f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.12f, 0.11f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.09f, 0.08f, 0.07f, 0.75f);
        colors[ImGuiCol_MenuBarBg]             = ImVec4(0.09f, 0.08f, 0.07f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.09f, 0.08f, 0.07f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.24f, 0.22f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.36f, 0.30f, 0.27f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.82f, 0.33f, 0.55f, 1.00f);
        colors[ImGuiCol_CheckMark]             = ImVec4(0.90f, 0.36f, 0.58f, 1.00f);
        colors[ImGuiCol_SliderGrab]            = ImVec4(0.82f, 0.33f, 0.55f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.92f, 0.42f, 0.64f, 1.00f);
        colors[ImGuiCol_Button]                = ImVec4(0.20f, 0.17f, 0.15f, 1.00f);
        colors[ImGuiCol_ButtonHovered]         = ImVec4(0.28f, 0.23f, 0.20f, 1.00f);
        colors[ImGuiCol_ButtonActive]          = ImVec4(0.82f, 0.33f, 0.55f, 0.85f);
        colors[ImGuiCol_Header]                = ImVec4(0.22f, 0.18f, 0.16f, 1.00f);
        colors[ImGuiCol_HeaderHovered]         = ImVec4(0.30f, 0.24f, 0.21f, 1.00f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(0.82f, 0.33f, 0.55f, 0.55f);
        colors[ImGuiCol_Separator]             = ImVec4(0.24f, 0.21f, 0.19f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.82f, 0.33f, 0.55f, 0.78f);
        colors[ImGuiCol_SeparatorActive]       = ImVec4(0.82f, 0.33f, 0.55f, 1.00f);
        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.82f, 0.33f, 0.55f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.82f, 0.33f, 0.55f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.92f, 0.42f, 0.64f, 0.95f);
        colors[ImGuiCol_Tab]                   = ImVec4(0.14f, 0.12f, 0.11f, 1.00f);
        colors[ImGuiCol_TabHovered]            = ImVec4(0.30f, 0.24f, 0.21f, 1.00f);
        colors[ImGuiCol_TabActive]             = ImVec4(0.20f, 0.17f, 0.15f, 1.00f);
        colors[ImGuiCol_TabUnfocused]          = ImVec4(0.11f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.16f, 0.14f, 0.13f, 1.00f);
        colors[ImGuiCol_DockingPreview]        = ImVec4(0.82f, 0.33f, 0.55f, 0.30f);
        colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.09f, 0.08f, 0.07f, 1.00f);
        colors[ImGuiCol_PlotLines]             = ImVec4(0.90f, 0.55f, 0.70f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.95f, 0.65f, 0.78f, 1.00f);
        colors[ImGuiCol_PlotHistogram]         = ImVec4(0.82f, 0.33f, 0.55f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.92f, 0.42f, 0.64f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.82f, 0.33f, 0.55f, 0.35f);
        colors[ImGuiCol_NavHighlight]          = ImVec4(0.82f, 0.33f, 0.55f, 1.00f);
    } else {
        ImGui::StyleColorsLight();
        colors[ImGuiCol_Text] = ImVec4(0.12f, 0.11f, 0.10f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.72f, 0.22f, 0.45f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.72f, 0.22f, 0.45f, 1.00f);
    }
#endif
}

void EditorTheme::applyStyleVars() {
#ifdef CF_HAS_IMGUI
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(10.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;
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

} // namespace Caffeine::Editor
