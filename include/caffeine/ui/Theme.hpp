#pragma once

/**
 * Caffeine UI — design tokens and theme runtime.
 *
 * Public API shared by Doppio and editor plugins. The header is intentionally
 * dependency-free apart from ImGui so a plugin can theme its own panels without
 * linking against Doppio internals.
 */

#include <imgui.h>

#include <string>

namespace Caffeine::UIKit {

/// Semantic color + metric tokens for the whole UI.
/// Widgets read these values instead of hard-coding ImGui style colors.
struct ThemeTokens {
    bool dark = true;

    // Surfaces
    ImVec4 background       = ImVec4(0.086f, 0.090f, 0.100f, 1.00f);
    ImVec4 surface          = ImVec4(0.105f, 0.110f, 0.121f, 1.00f);
    ImVec4 surfaceRaised    = ImVec4(0.135f, 0.141f, 0.155f, 0.98f);
    ImVec4 surfaceOverlay   = ImVec4(0.160f, 0.167f, 0.183f, 0.98f);
    ImVec4 border           = ImVec4(0.215f, 0.223f, 0.242f, 1.00f);
    ImVec4 borderStrong     = ImVec4(0.315f, 0.325f, 0.350f, 1.00f);
    ImVec4 frame            = ImVec4(0.150f, 0.156f, 0.170f, 1.00f);
    ImVec4 frameHovered     = ImVec4(0.190f, 0.198f, 0.215f, 1.00f);
    ImVec4 frameActive      = ImVec4(0.230f, 0.240f, 0.260f, 1.00f);
    ImVec4 header           = ImVec4(0.185f, 0.193f, 0.210f, 1.00f);
    ImVec4 headerHovered    = ImVec4(0.255f, 0.265f, 0.290f, 1.00f);

    // Text
    ImVec4 text             = ImVec4(0.925f, 0.932f, 0.945f, 1.00f);
    ImVec4 textMuted        = ImVec4(0.620f, 0.635f, 0.660f, 1.00f);
    ImVec4 textDisabled     = ImVec4(0.455f, 0.470f, 0.495f, 1.00f);
    ImVec4 textInverse      = ImVec4(0.086f, 0.090f, 0.100f, 1.00f);

    // Accent / selection
    ImVec4 accent           = ImVec4(0.860f, 0.350f, 0.550f, 1.00f);
    ImVec4 accentHovered    = ImVec4(0.950f, 0.440f, 0.640f, 1.00f);
    ImVec4 accentActive     = ImVec4(0.740f, 0.270f, 0.470f, 1.00f);
    ImVec4 accentMuted      = ImVec4(0.860f, 0.350f, 0.550f, 0.30f);
    ImVec4 onAccent         = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    ImVec4 selection        = ImVec4(0.860f, 0.350f, 0.550f, 0.28f);

    // Semantic
    ImVec4 success          = ImVec4(0.420f, 0.850f, 0.480f, 1.00f);
    ImVec4 warning          = ImVec4(0.980f, 0.760f, 0.240f, 1.00f);
    ImVec4 danger           = ImVec4(0.960f, 0.380f, 0.380f, 1.00f);
    ImVec4 info             = ImVec4(0.420f, 0.690f, 0.980f, 1.00f);

    // Metrics
    float fontSize        = 14.0f;
    float scale           = 1.0f;
    float rounding        = 6.0f;
    float roundingSmall   = 4.0f;
    float borderSize      = 1.0f;
    float spacingX        = 8.0f;
    float spacingY        = 6.0f;
    float paddingX        = 10.0f;
    float paddingY        = 8.0f;
    float framePaddingX   = 8.0f;
    float framePaddingY   = 5.0f;
    float rowHeight       = 26.0f;
    float toolbarHeight   = 30.0f;
    float statusBarHeight = 24.0f;
    float scrollbarSize   = 12.0f;
    float indentWidth     = 18.0f;
};

/// Global mutable token table. Defined once in the host (caffeine-ui-kit) so
/// Doppio and its plugins render with the same live theme.
ThemeTokens& tokens();
const ThemeTokens& theme();

/// HiDPI / user UI scale. Widgets multiply fixed metrics by this value.
float& uiScaleRef();
float uiScale();

/// Push every token into ImGuiStyle / ImGuiCol. Call after changing tokens.
void applyTheme();

/// Reset tokens to the built-in palette (dark or light).
void resetTheme(bool dark = true);

/// Load a partial theme from JSON. Unknown keys are ignored; missing keys keep
/// their current value. Returns false when the file cannot be parsed.
bool loadTheme(const std::string& path, std::string* outError = nullptr);

/// Persist the current tokens as JSON.
bool saveTheme(const std::string& path);

/// Serialize the current tokens (used by tests and the editor).
std::string themeToJson();

/// Built-in theme document.
std::string defaultThemeJson(bool dark = true);

}  // namespace Caffeine::UIKit
