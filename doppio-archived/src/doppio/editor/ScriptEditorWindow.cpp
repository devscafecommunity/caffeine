#include "caffeine/ui/UI.hpp"
#include "editor/ScriptEditorWindow.hpp"
#ifdef CF_HAS_SCRIPTING
#include "script/ScriptEngine.hpp"
#endif
#include <fstream>
#include <cstring>

namespace Caffeine::Editor {

bool ScriptEditorWindow::openFile(const std::filesystem::path& path) {
    for (const auto& f : m_openFiles) {
        if (f.path == path.string()) {
            return true;
        }
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    OpenFile f;
    f.path = path.string();
    f.content = std::move(content);
    f.originalContent = f.content;
    f.isDirty = false;
    f.editBuffer.resize(f.content.size() + 1);
    std::memcpy(f.editBuffer.data(), f.content.c_str(), f.content.size() + 1);

    m_openFiles.push_back(std::move(f));
    m_activeFileIndex = static_cast<int>(m_openFiles.size()) - 1;
    
    return true;
}

bool ScriptEditorWindow::saveFile(int index) {
    if (index < 0 || index >= static_cast<int>(m_openFiles.size())) {
        return false;
    }

    auto& file = m_openFiles[index];
    std::ofstream out(file.path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
    out.close();

    file.originalContent = file.content;
    file.isDirty = false;

#ifdef CF_HAS_SCRIPTING
    if (m_scriptEngine) {
        m_scriptEngine->reloadScript(file.path);
    }
#endif
    
    return true;
}

bool ScriptEditorWindow::saveFileAs(int index, const std::filesystem::path& path) {
    if (index < 0 || index >= static_cast<int>(m_openFiles.size())) {
        return false;
    }

    auto& file = m_openFiles[index];
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
    out.close();

    file.path = path.string();
    file.originalContent = file.content;
    file.isDirty = false;
    
    return true;
}

void ScriptEditorWindow::closeFile(int index) {
    if (index < 0 || index >= static_cast<int>(m_openFiles.size())) {
        return;
    }

    m_openFiles.erase(m_openFiles.begin() + index);
    
    if (m_activeFileIndex >= static_cast<int>(m_openFiles.size())) {
        m_activeFileIndex = m_openFiles.empty() ? -1 : 
            static_cast<int>(m_openFiles.size()) - 1;
    }
}

} // namespace Caffeine::Editor


#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void ScriptEditorWindow::render() {
    if (!m_open) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    Panel root("Script Editor", "Lua source", "code", &m_open);
    UIContext ui;

    auto& drop = root.emplace<DropZone>("Drop a .lua file here", "ASSET_PATH");
    drop.setOnDrop([this](const char* path) {
        if (path) openFile(path);
    });

    if (m_activeFileIndex >= 0 &&
        m_activeFileIndex < static_cast<int>(m_openFiles.size())) {
        auto& file = m_openFiles[m_activeFileIndex];

        auto& toolbar = root.emplace<Row>("script_toolbar");
        toolbar.emplace<Button>("Save", ButtonVariant::Primary)
            .setOnClick([this] { saveFile(m_activeFileIndex); });
        if (file.isDirty) toolbar.emplace<Label>("*", TextTone::Warning);
        const std::string filename =
            file.path.substr(file.path.find_last_of("/\\") + 1);
        toolbar.emplace<Label>(filename, TextTone::Muted);

        root.emplace<Separator>();

        const std::size_t bufferSize =
            std::max(file.content.size() + 1, static_cast<std::size_t>(1));
        if (file.editBuffer.size() < bufferSize) {
            file.editBuffer.resize(bufferSize);
        }
        std::memcpy(file.editBuffer.data(), file.content.c_str(), bufferSize);

        root.emplace<MultilineText>(file.editBuffer.data(), file.editBuffer.size());

        ui.draw(root);

        if (std::strcmp(file.editBuffer.data(), file.content.c_str()) != 0) {
            file.content = file.editBuffer.data();
            file.isDirty = true;
        }
    } else {
        root.emplace<Label>(
            "No file open. Drag a .lua file from the Asset Browser or drop it above.",
            TextTone::Muted);
        ui.draw(root);
    }
}

} // namespace Caffeine::Editor

#endif