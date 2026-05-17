#pragma once
#include <string>
#include <vector>
#include <filesystem>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class ScriptEditorWindow {
public:
    ScriptEditorWindow() = default;

    struct OpenFile {
        std::string path;
        std::string content;
        std::string originalContent;
        bool isDirty = false;
        std::vector<char> editBuffer;  // Per-file ImGui editing buffer
    };

    bool openFile(const std::filesystem::path& path);
    bool saveFile(int index);
    bool saveFileAs(int index, const std::filesystem::path& path);
    void closeFile(int index);
    
    int openFileCount() const { return static_cast<int>(m_openFiles.size()); }
    const OpenFile& file(int index) const { return m_openFiles[index]; }
    int activeFileIndex() const { return m_activeFileIndex; }
    void setActiveFile(int index) { m_activeFileIndex = index; }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open() { m_open = true; }

#ifdef CF_HAS_IMGUI
    void render();
#endif

private:
    std::vector<OpenFile> m_openFiles;
    int m_activeFileIndex = -1;
    bool m_open = true;
};

} // namespace Caffeine::Editor