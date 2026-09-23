#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Editor {

struct EditorPreferences {
    std::string activeLayoutProfile = "Default";

    bool vsyncEnabled = true;
    int  fontSize = 14;
    bool darkMode = true;
    bool autoSaveEnabled = true;
    int  autoSaveInterval = 300;

    f32  defaultGridSize = 0.1f;
    bool showGrid = true;
    bool snapEnabled = false;
    f32  snapIncrement = 0.1f;
    bool uniformScaleDefault = true;
    f32  cameraMoveSpeed = 5.0f;
    f32  cameraOrbitSpeed = 0.005f;
    bool textureQualityEnabled = true;
    f32  textureQualityRadius = 35.0f;
    f32  textureQualityFalloff = 100.0f;
    f32  textureQualityMinScale = 0.25f;
    bool showFPSInStatusBar = true;
    bool confirmOnSceneClose = true;
    bool reopenLastSceneOnStartup = true;

    static std::filesystem::path preferencesPath();
    static EditorPreferences load();
    bool save() const;
};

}  // namespace Caffeine::Editor
