#pragma once
#include "core/Types.hpp"
#include "assets/AssetManager.hpp"
#include "assets/AssetTypes.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"

#include <vector>
#include <filesystem>
#include <cstring>
#include <optional>
#include <algorithm>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class AssetBrowser {
public:
    struct Entry {
        std::filesystem::path     path;
        AssetType                 type;
        char                      label[64];
    };

    AssetBrowser() = default;

    void init(const char* rootPath);
    void refreshDirectory();
    void render(EditorContext& ctx);
    std::optional<std::filesystem::path> getDroppedAsset() const;

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
    const char* iconForType(AssetType type);

    bool m_open = true;
    std::string m_rootPath = "assets";
    std::filesystem::path m_currentDir;
    std::vector<Entry> m_entries;
    int m_selectedEntry = -1;
};

} // namespace Caffeine::Editor
