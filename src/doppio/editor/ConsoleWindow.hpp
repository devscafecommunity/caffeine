#pragma once
#include "caffeine/ui/UI.hpp"
#include "core/Types.hpp"
#include "debug/LogSystem.hpp"
#include "containers/FixedString.hpp"
#include <vector>
#include <cstring>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

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

        using namespace Caffeine::UIKit;
        using namespace Caffeine::UIKit::Elements;

        Panel root("Console", "Runtime log", "alert-circle", &m_open);
        UIContext ui;

        auto& toolbar = root.emplace<Row>("console_toolbar");
        m_filterIndex = static_cast<int>(m_filterLevel);
        auto& filter = toolbar.emplace<Elements::Combo>(
            "Filter", &m_filterIndex,
            std::vector<std::string>{"Trace", "Info", "Warn", "Error", "Fatal"});
        filter.setWidth(140.0f);
        filter.setOnChange([this](int value) {
            m_filterLevel = static_cast<Debug::LogLevel>(value);
        });
        toolbar.emplace<Checkbox>("Auto-scroll", &m_autoScroll)
            .setTooltip("Follow new log entries");
        toolbar.emplace<Button>("Clear").setOnClick([this] { clear(); });

        auto& scroll = root.emplace<ScrollArea>("console_scroll");
        scroll.setScrollToBottom(m_autoScroll);
        scroll.setReserveBottom(44.0f);
        for (const auto& e : m_entries) {
            if (e.level < m_filterLevel) continue;
            TextTone tone = TextTone::Normal;
            if (e.level == Debug::LogLevel::Trace) tone = TextTone::Muted;
            if (e.level == Debug::LogLevel::Warn) tone = TextTone::Warning;
            if (e.level == Debug::LogLevel::Error ||
                e.level == Debug::LogLevel::Fatal) {
                tone = TextTone::Danger;
            }
            scroll.emplace<Label>(
                std::string("[") + Debug::LogSystem::levelToString(e.level) + "] " +
                    e.category.cStr() + "  " + e.message.cStr(),
                tone);
        }

        auto& inputRow = root.emplace<Row>("console_input");
        auto& input = inputRow.emplace<TextInput>(m_inputBuf, sizeof(m_inputBuf));
        input.setHint("Type a command...");
        input.setGrow(true);
        input.setOnSubmit([this] { m_inputBuf[0] = '\0'; });
        inputRow.emplace<Button>("Send").setOnClick([this] { m_inputBuf[0] = '\0'; });

        ui.draw(root);
    }
#endif

private:
    std::vector<LogEntry> m_entries;
    char            m_inputBuf[256] = {};
    int             m_filterIndex   = 0;
    bool            m_autoScroll    = true;
    bool            m_open          = true;
    Debug::LogLevel m_filterLevel   = Debug::LogLevel::Trace;
};

}  // namespace Caffeine::Editor
