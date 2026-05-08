#pragma once
#include "core/Types.hpp"
#include "debug/LogSystem.hpp"
#include "containers/FixedString.hpp"
#include <vector>
#include <cstring>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class ConsoleWindow {
public:
    ConsoleWindow() = default;

    struct LogEntry {
        Debug::LogLevel  level;
        FixedString<32>  category;
        FixedString<256> message;
    };

    void addLog(Debug::LogLevel level, const char* category, const char* message) {
        LogEntry e;
        e.level = level;
        e.category = FixedString<32>(category);
        e.message  = FixedString<256>(message);
        m_entries.push_back(e);
    }

    void clear() {
        m_entries.clear();
    }

    usize entryCount() const { return m_entries.size(); }
    const LogEntry& entry(usize i) const { return m_entries[i]; }

    bool isOpen()     const { return m_open; }
    bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool v) { m_autoScroll = v; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

    Debug::LogLevel filterLevel() const { return m_filterLevel; }
    void setFilterLevel(Debug::LogLevel lvl) { m_filterLevel = lvl; }

#ifdef CF_HAS_IMGUI
    void render() {
        if (!m_open) return;
        if (ImGui::Begin("Console", &m_open)) {
            const char* levels[] = { "Trace", "Info", "Warn", "Error", "Fatal" };
            int lvl = static_cast<int>(m_filterLevel);
            if (ImGui::Combo("Filter", &lvl, levels, 5)) {
                m_filterLevel = static_cast<Debug::LogLevel>(lvl);
            }
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &m_autoScroll);
            ImGui::SameLine();
            if (ImGui::Button("Clear")) clear();

            ImGui::Separator();
            ImGui::BeginChild("scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
            for (const auto& e : m_entries) {
                if (e.level < m_filterLevel) continue;
                ImVec4 col = ImVec4(1, 1, 1, 1);
                if (e.level == Debug::LogLevel::Warn)  col = ImVec4(1, 1, 0, 1);
                if (e.level == Debug::LogLevel::Error) col = ImVec4(1, 0.4f, 0.4f, 1);
                if (e.level == Debug::LogLevel::Fatal) col = ImVec4(1, 0, 0, 1);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::Text("[%s] %s  %s",
                    Debug::LogSystem::levelToString(e.level),
                    e.category.cStr(),
                    e.message.cStr());
                ImGui::PopStyleColor();
            }
            if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (ImGui::InputText("##input", m_inputBuf, sizeof(m_inputBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_inputBuf[0] = '\0';
            }
        }
        ImGui::End();
    }
#endif

private:
    std::vector<LogEntry> m_entries;
    char            m_inputBuf[256] = {};
    bool            m_autoScroll    = true;
    bool            m_open          = true;
    Debug::LogLevel m_filterLevel   = Debug::LogLevel::Trace;
};

}  // namespace Caffeine::Editor
