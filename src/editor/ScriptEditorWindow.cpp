#include "editor/ScriptEditorWindow.hpp"
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
    
    if (ImGui::Begin("Script Editor", &m_open)) {
        if (m_activeFileIndex >= 0 && m_activeFileIndex < static_cast<int>(m_openFiles.size())) {
            auto& file = m_openFiles[m_activeFileIndex];
            
            if (ImGui::Button("Save")) {
                saveFile(m_activeFileIndex);
            }
            ImGui::SameLine();
            
            if (file.isDirty) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "*");
                ImGui::SameLine();
            }
            
            std::string filename = file.path.substr(file.path.find_last_of("/\\") + 1);
            ImGui::Text("%s", filename.c_str());
            
            ImGui::Separator();
            
            // Ensure the edit buffer is sized to hold content + NUL
            size_t bufSize = std::max(file.content.size() + 1, size_t(1));
            if (file.editBuffer.size() < bufSize) {
                file.editBuffer.resize(bufSize);
            }
            std::memcpy(file.editBuffer.data(), file.content.c_str(), bufSize);
            
            ImGui::InputTextMultiline(
                "##script_content",
                file.editBuffer.data(),
                file.editBuffer.size(),
                ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() - 40),
                ImGuiInputTextFlags_AllowTabInput
            );
            
            if (std::strcmp(file.editBuffer.data(), file.content.c_str()) != 0) {
                file.content = file.editBuffer.data();
                file.isDirty = true;
            }
        } else {
            ImGui::Text("No file open. Double-click a .lua file in Asset Browser to open.");
        }
    }
    ImGui::End();
}

} // namespace Caffeine::Editor

#endif