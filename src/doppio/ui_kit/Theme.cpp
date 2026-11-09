#include "caffeine/ui/Theme.hpp"

#include "Json.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace Caffeine::UIKit {

ThemeTokens& tokens() {
    static ThemeTokens value;
    return value;
}

const ThemeTokens& theme() { return tokens(); }

float& uiScaleRef() {
    static float value = 1.0f;
    return value;
}

float uiScale() { return uiScaleRef(); }

namespace {

ThemeTokens makeDarkTokens() {
    return ThemeTokens{};
}

ThemeTokens makeLightTokens() {
    ThemeTokens t;
    t.dark = false;
    t.background       = ImVec4(0.965f, 0.967f, 0.972f, 1.00f);
    t.surface          = ImVec4(0.990f, 0.992f, 1.000f, 1.00f);
    t.surfaceRaised    = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    t.surfaceOverlay   = ImVec4(0.985f, 0.987f, 0.995f, 1.00f);
    t.border           = ImVec4(0.820f, 0.830f, 0.850f, 1.00f);
    t.borderStrong     = ImVec4(0.660f, 0.675f, 0.700f, 1.00f);
    t.frame            = ImVec4(0.925f, 0.930f, 0.940f, 1.00f);
    t.frameHovered     = ImVec4(0.880f, 0.888f, 0.900f, 1.00f);
    t.frameActive      = ImVec4(0.835f, 0.845f, 0.860f, 1.00f);
    t.header           = ImVec4(0.900f, 0.908f, 0.920f, 1.00f);
    t.headerHovered    = ImVec4(0.855f, 0.865f, 0.880f, 1.00f);
    t.text             = ImVec4(0.100f, 0.110f, 0.130f, 1.00f);
    t.textMuted        = ImVec4(0.380f, 0.395f, 0.420f, 1.00f);
    t.textDisabled     = ImVec4(0.520f, 0.535f, 0.560f, 1.00f);
    t.textInverse      = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    t.accent           = ImVec4(0.780f, 0.180f, 0.450f, 1.00f);
    t.accentHovered    = ImVec4(0.860f, 0.260f, 0.530f, 1.00f);
    t.accentActive     = ImVec4(0.660f, 0.130f, 0.370f, 1.00f);
    t.accentMuted      = ImVec4(0.780f, 0.180f, 0.450f, 0.22f);
    t.onAccent         = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    t.selection        = ImVec4(0.780f, 0.180f, 0.450f, 0.22f);
    t.success          = ImVec4(0.120f, 0.520f, 0.240f, 1.00f);
    t.warning          = ImVec4(0.720f, 0.500f, 0.040f, 1.00f);
    t.danger           = ImVec4(0.760f, 0.180f, 0.180f, 1.00f);
    t.info             = ImVec4(0.160f, 0.420f, 0.780f, 1.00f);
    return t;
}

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

ImVec4 colorOr(const Detail::JsonValue* value, const ImVec4& fallback) {
    if (!value || !value->isArray() || value->array.size() < 3) return fallback;
    ImVec4 out = fallback;
    out.x = static_cast<float>(value->array[0].numberOr(fallback.x));
    out.y = static_cast<float>(value->array[1].numberOr(fallback.y));
    out.z = static_cast<float>(value->array[2].numberOr(fallback.z));
    if (value->array.size() >= 4) {
        out.w = static_cast<float>(value->array[3].numberOr(fallback.w));
    }
    return out;
}

void loadColor(const Detail::JsonValue& root, const char* key, ImVec4& target) {
    if (const Detail::JsonValue* colors = root.find("colors")) {
        if (const Detail::JsonValue* value = colors->find(key)) {
            target = colorOr(value, target);
            return;
        }
    }
    if (const Detail::JsonValue* value = root.find(key)) {
        target = colorOr(value, target);
    }
}

float loadFloat(const Detail::JsonValue& root, const char* key, float fallback) {
    if (const Detail::JsonValue* value = root.find(key)) {
        if (value->isNumber()) return static_cast<float>(value->number);
    }
    if (const Detail::JsonValue* metrics = root.find("metrics")) {
        if (const Detail::JsonValue* value = metrics->find(key)) {
            if (value->isNumber()) return static_cast<float>(value->number);
        }
    }
    return fallback;
}

void writeColor(std::ostringstream& out, const char* key, const ImVec4& color,
                bool last = false) {
    out << "    \"" << key << "\": [" << color.x << ", " << color.y << ", "
        << color.z << ", " << color.w << "]" << (last ? "" : ",") << "\n";
}

}  // namespace

void resetTheme(bool dark) {
    tokens() = dark ? makeDarkTokens() : makeLightTokens();
}

void applyTheme() {
    ThemeTokens& t = tokens();
    const float s = t.scale > 0.0f ? t.scale : 1.0f;
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = t.rounding * s;
    style.ChildRounding     = t.roundingSmall * s;
    style.FrameRounding     = t.roundingSmall * s;
    style.PopupRounding     = t.rounding * s;
    style.ScrollbarRounding = t.roundingSmall * s;
    style.GrabRounding      = t.roundingSmall * s;
    style.TabRounding       = t.roundingSmall * s;
    style.WindowBorderSize  = t.borderSize;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = t.borderSize;
    style.WindowPadding     = ImVec2(t.paddingX * s, t.paddingY * s);
    style.FramePadding      = ImVec2(t.framePaddingX * s, t.framePaddingY * s);
    style.ItemSpacing       = ImVec2(t.spacingX * s, t.spacingY * s);
    style.ItemInnerSpacing  = ImVec2(t.spacingX * 0.75f * s, t.spacingY * 0.66f * s);
    style.IndentSpacing     = t.indentWidth * s;
    style.ScrollbarSize     = t.scrollbarSize * s;
    style.GrabMinSize       = 10.0f * s;
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowMinSize     = ImVec2(200.0f * s, 120.0f * s);
    style.TabBarBorderSize  = 1.0f;
    style.TabBarOverlineSize = 2.0f * s;
    style.DockingSeparatorSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(20.0f * s, 3.0f);
    style.ScrollbarPadding  = 2.0f;
    style.DisabledAlpha     = 0.58f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = t.text;
    c[ImGuiCol_TextDisabled]          = t.textDisabled;
    c[ImGuiCol_WindowBg]              = t.background;
    c[ImGuiCol_ChildBg]               = t.surface;
    c[ImGuiCol_PopupBg]               = t.surfaceRaised;
    c[ImGuiCol_Border]                = t.border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = t.frame;
    c[ImGuiCol_FrameBgHovered]        = t.frameHovered;
    c[ImGuiCol_FrameBgActive]         = t.frameActive;
    c[ImGuiCol_TitleBg]               = t.background;
    c[ImGuiCol_TitleBgActive]         = t.surfaceRaised;
    c[ImGuiCol_TitleBgCollapsed]      = t.background;
    c[ImGuiCol_MenuBarBg]             = t.surface;
    c[ImGuiCol_ScrollbarBg]           = t.background;
    c[ImGuiCol_ScrollbarGrab]         = t.border;
    c[ImGuiCol_ScrollbarGrabHovered]  = t.borderStrong;
    c[ImGuiCol_ScrollbarGrabActive]   = t.accent;
    c[ImGuiCol_CheckMark]             = t.accent;
    c[ImGuiCol_SliderGrab]            = t.accent;
    c[ImGuiCol_SliderGrabActive]      = t.accentHovered;
    c[ImGuiCol_Button]                = t.frame;
    c[ImGuiCol_ButtonHovered]         = t.frameHovered;
    c[ImGuiCol_ButtonActive]          = t.accentMuted;
    c[ImGuiCol_Header]                = t.header;
    c[ImGuiCol_HeaderHovered]         = t.headerHovered;
    c[ImGuiCol_HeaderActive]          = t.accentMuted;
    c[ImGuiCol_Separator]             = t.border;
    c[ImGuiCol_SeparatorHovered]      = t.accentMuted;
    c[ImGuiCol_SeparatorActive]       = t.accent;
    c[ImGuiCol_ResizeGrip]            = t.accentMuted;
    c[ImGuiCol_ResizeGripHovered]     = t.accentMuted;
    c[ImGuiCol_ResizeGripActive]      = t.accent;
    c[ImGuiCol_Tab]                   = t.surface;
    c[ImGuiCol_TabHovered]            = t.headerHovered;
#if IMGUI_VERSION_NUM >= 19000
    c[ImGuiCol_TabSelected]           = t.header;
    c[ImGuiCol_TabSelectedOverline]   = t.accent;
    c[ImGuiCol_TabDimmed]             = t.surface;
    c[ImGuiCol_TabDimmedSelected]     = t.header;
    c[ImGuiCol_TabDimmedSelectedOverline] = t.accentMuted;
#else
    c[ImGuiCol_TabActive]             = t.header;
    c[ImGuiCol_TabUnfocused]          = t.surface;
    c[ImGuiCol_TabUnfocusedActive]    = t.header;
#endif
    c[ImGuiCol_DockingPreview]        = t.accentMuted;
    c[ImGuiCol_DockingEmptyBg]        = t.background;
    c[ImGuiCol_PlotLines]             = t.accent;
    c[ImGuiCol_PlotLinesHovered]      = t.accentHovered;
    c[ImGuiCol_PlotHistogram]         = t.accent;
    c[ImGuiCol_PlotHistogramHovered]  = t.accentHovered;
    c[ImGuiCol_TableHeaderBg]         = t.header;
    c[ImGuiCol_TableBorderStrong]     = t.border;
    c[ImGuiCol_TableBorderLight]      = t.border;
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(t.text.x, t.text.y, t.text.z, 0.03f);
    c[ImGuiCol_TextSelectedBg]        = t.selection;
    c[ImGuiCol_InputTextCursor]       = t.accent;
    c[ImGuiCol_DragDropTarget]        = t.accentHovered;
    c[ImGuiCol_DragDropTargetBg]      = t.accentMuted;
    c[ImGuiCol_NavHighlight]          = t.accent;
    c[ImGuiCol_NavCursor]             = t.accent;
    c[ImGuiCol_NavWindowingHighlight] = t.accent;
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.35f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.45f);
}

bool loadTheme(const std::string& path, std::string* outError) {
    std::string text;
    if (!readTextFile(path, text)) {
        if (outError) *outError = "cannot open theme: " + path;
        return false;
    }

    Detail::JsonValue root;
    std::string parseError;
    if (!Detail::parseJson(text, root, &parseError) || !root.isObject()) {
        if (outError) *outError = "invalid theme JSON: " + parseError;
        return false;
    }

    if (const Detail::JsonValue* dark = root.find("dark")) {
        if (dark->isBool()) resetTheme(dark->boolean);
    }

    ThemeTokens& t = tokens();
    loadColor(root, "background", t.background);
    loadColor(root, "surface", t.surface);
    loadColor(root, "surfaceRaised", t.surfaceRaised);
    loadColor(root, "surfaceOverlay", t.surfaceOverlay);
    loadColor(root, "border", t.border);
    loadColor(root, "borderStrong", t.borderStrong);
    loadColor(root, "frame", t.frame);
    loadColor(root, "frameHovered", t.frameHovered);
    loadColor(root, "frameActive", t.frameActive);
    loadColor(root, "header", t.header);
    loadColor(root, "headerHovered", t.headerHovered);
    loadColor(root, "text", t.text);
    loadColor(root, "textMuted", t.textMuted);
    loadColor(root, "textDisabled", t.textDisabled);
    loadColor(root, "textInverse", t.textInverse);
    loadColor(root, "accent", t.accent);
    loadColor(root, "accentHovered", t.accentHovered);
    loadColor(root, "accentActive", t.accentActive);
    loadColor(root, "accentMuted", t.accentMuted);
    loadColor(root, "onAccent", t.onAccent);
    loadColor(root, "selection", t.selection);
    loadColor(root, "success", t.success);
    loadColor(root, "warning", t.warning);
    loadColor(root, "danger", t.danger);
    loadColor(root, "info", t.info);

    t.fontSize        = loadFloat(root, "fontSize", t.fontSize);
    t.scale           = loadFloat(root, "scale", t.scale);
    t.rounding        = loadFloat(root, "rounding", t.rounding);
    t.roundingSmall   = loadFloat(root, "roundingSmall", t.roundingSmall);
    t.borderSize      = loadFloat(root, "borderSize", t.borderSize);
    t.spacingX        = loadFloat(root, "spacingX", t.spacingX);
    t.spacingY        = loadFloat(root, "spacingY", t.spacingY);
    t.paddingX        = loadFloat(root, "paddingX", t.paddingX);
    t.paddingY        = loadFloat(root, "paddingY", t.paddingY);
    t.framePaddingX   = loadFloat(root, "framePaddingX", t.framePaddingX);
    t.framePaddingY   = loadFloat(root, "framePaddingY", t.framePaddingY);
    t.rowHeight       = loadFloat(root, "rowHeight", t.rowHeight);
    t.toolbarHeight   = loadFloat(root, "toolbarHeight", t.toolbarHeight);
    t.statusBarHeight = loadFloat(root, "statusBarHeight", t.statusBarHeight);
    t.scrollbarSize   = loadFloat(root, "scrollbarSize", t.scrollbarSize);
    t.indentWidth     = loadFloat(root, "indentWidth", t.indentWidth);
    return true;
}

std::string themeToJson() {
    const ThemeTokens& t = tokens();
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"dark\": " << (t.dark ? "true" : "false") << ",\n";
    out << "  \"fontSize\": " << t.fontSize << ",\n";
    out << "  \"rounding\": " << t.rounding << ",\n";
    out << "  \"roundingSmall\": " << t.roundingSmall << ",\n";
    out << "  \"spacingX\": " << t.spacingX << ",\n";
    out << "  \"spacingY\": " << t.spacingY << ",\n";
    out << "  \"paddingX\": " << t.paddingX << ",\n";
    out << "  \"paddingY\": " << t.paddingY << ",\n";
    out << "  \"rowHeight\": " << t.rowHeight << ",\n";
    out << "  \"colors\": {\n";
    writeColor(out, "background", t.background);
    writeColor(out, "surface", t.surface);
    writeColor(out, "surfaceRaised", t.surfaceRaised);
    writeColor(out, "surfaceOverlay", t.surfaceOverlay);
    writeColor(out, "border", t.border);
    writeColor(out, "borderStrong", t.borderStrong);
    writeColor(out, "frame", t.frame);
    writeColor(out, "frameHovered", t.frameHovered);
    writeColor(out, "frameActive", t.frameActive);
    writeColor(out, "header", t.header);
    writeColor(out, "headerHovered", t.headerHovered);
    writeColor(out, "text", t.text);
    writeColor(out, "textMuted", t.textMuted);
    writeColor(out, "textDisabled", t.textDisabled);
    writeColor(out, "accent", t.accent);
    writeColor(out, "accentHovered", t.accentHovered);
    writeColor(out, "accentActive", t.accentActive);
    writeColor(out, "accentMuted", t.accentMuted);
    writeColor(out, "onAccent", t.onAccent);
    writeColor(out, "selection", t.selection);
    writeColor(out, "success", t.success);
    writeColor(out, "warning", t.warning);
    writeColor(out, "danger", t.danger);
    writeColor(out, "info", t.info, true);
    out << "  }\n";
    out << "}\n";
    return out.str();
}

std::string defaultThemeJson(bool dark) {
    ThemeTokens backup = tokens();
    resetTheme(dark);
    std::string json = themeToJson();
    tokens() = backup;
    return json;
}

bool saveTheme(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file << themeToJson();
    return true;
}

}  // namespace Caffeine::UIKit
