#pragma once
#include "core/Types.hpp"
#include "BuildSystem.hpp"
#include <vector>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

using namespace Caffeine;

class BuildDialog {
public:
    BuildDialog();
    ~BuildDialog() = default;

#ifdef CF_HAS_IMGUI
    void render();
#endif

    bool isOpen() const { return m_open; }
    void open()  { m_open = true; }
    void close() { m_open = false; }

private:
#ifdef CF_HAS_IMGUI
    void renderConfigSection();
    void renderAdvancedSection();
    void renderProgressSection();
    void renderLogSection();
    void onBuildClicked();
    void onCancelClicked();
#endif

    BuildSettings m_settings;
    BuildProgress* m_progress = nullptr;
    char m_projectNameBuf[256];
    char m_outputPathBuf[512];
    char m_executableNameBuf[256];
    char m_iconPathBuf[512];
    char m_versionBuf[64];
    std::vector<std::string> m_scenesToInclude;
    bool m_showBuildLog = true;
    bool m_open = true;
};

}
