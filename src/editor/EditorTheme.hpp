#pragma once

#include "core/Types.hpp"

namespace Caffeine::Editor {

struct EditorThemeSettings {
    bool darkMode = true;
    f32  fontSize = 14.0f;
};

class EditorTheme {
public:
    static void init(const EditorThemeSettings& settings = {});
    static void apply(const EditorThemeSettings& settings);
    static void setFontSize(f32 size);

    static f32 fontSize() { return s_fontSize; }
    static bool darkMode() { return s_darkMode; }

private:
    static void applyPalette(bool dark);
    static void loadFonts(f32 size);
    static void applyStyleVars();

    static f32 s_fontSize;
    static bool s_darkMode;
    static bool s_fontsLoaded;
};

} // namespace Caffeine::Editor
