#pragma once

#include "core/Types.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Caffeine::Editor {

// Semantic icon names mapped to line-md assets (see assets/icons/).
namespace EditorIcon {
    inline constexpr const char* NewScene = "account-add";
    inline constexpr const char* Save = "backup-restore";
    inline constexpr const char* SaveAs = "arrow-down-square";
    inline constexpr const char* Open = "arrow-open-right";
    inline constexpr const char* Exit = "arrow-close-left";
    inline constexpr const char* Undo = "arrow-left";
    inline constexpr const char* Redo = "arrow-right";
    inline constexpr const char* Copy = "arrows-diagonal";
    inline constexpr const char* Paste = "arrow-down";
    inline constexpr const char* Duplicate = "arrows-diagonal-rotated";
    inline constexpr const char* Hierarchy = "arrows-vertical";
    inline constexpr const char* Inspector = "align-left";
    inline constexpr const char* Viewport = "arrows-diagonal";
    inline constexpr const char* Assets = "arrow-down-square";
    inline constexpr const char* Console = "alert-circle";
    inline constexpr const char* Profiler = "bell";
    inline constexpr const char* EntityDebugger = "account";
    inline constexpr const char* Animation = "arrow-right-circle";
    inline constexpr const char* Animator = "arrows-horizontal";
    inline constexpr const char* Plugins = "beer";
    inline constexpr const char* Refresh = "backup-restore";
    inline constexpr const char* Build = "beer";
    inline constexpr const char* Folder = "arrow-open-down";
    inline constexpr const char* Search = "at";
}

class EditorIcons {
public:
    static void init();
    static void shutdown();

#ifdef CF_HAS_IMGUI
    static bool hasIcon(const std::string& name);
    static ImTextureRef get(const std::string& name);
    static void image(const std::string& name, f32 size = 16.0f, ImU32 tint = IM_COL32_WHITE);
    static bool iconButton(const std::string& name, const char* id = nullptr, f32 size = 18.0f);
    static void iconLabel(const std::string& name, const char* text, f32 size = 16.0f);

    static bool hasBrandLogo();
    static ImTextureRef brandLogoTexture();
    static void brandLogo(f32 height = 24.0f);

    static bool menuItem(const char* icon, const char* label, const char* shortcut = nullptr,
                         bool selected = false, bool enabled = true);
    static bool menuItem(const char* icon, const char* label, const char* shortcut, bool* open,
                         bool enabled = true);
#endif

private:
#ifdef CF_HAS_IMGUI
    struct IconEntry {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
        bool failed = false;
    };

    static IconEntry& load(const std::string& name);
    static IconEntry& loadPng(const std::string& cacheKey, const std::filesystem::path& path);
    static std::string preprocessSvg(std::string svg);
    static bool rasterizeSvg(const std::string& path, IconEntry& out, int sizePx);
    static bool rasterizePng(const std::filesystem::path& path, IconEntry& out);

    static std::unordered_map<std::string, IconEntry> s_cache;
#endif
};

} // namespace Caffeine::Editor
