#include "LogSystem.hpp"
#include <cstdio>
#include <cstring>

namespace Caffeine::Debug {

LogSystem& LogSystem::instance() {
    static LogSystem s;
    return s;
}

LogSystem::LogSystem() {}

void LogSystem::log(LogLevel level, const char* category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(level, category, fmt, args);
    va_end(args);
}

void LogSystem::vlog(LogLevel level, const char* category, const char* fmt, va_list args) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (level < m_minLevel) return;

    if (category) {
        const CategoryEntry* entry = findCategory(category);
        if (entry && !entry->enabled) return;
    }

    char buffer[MAX_MESSAGE_LENGTH];
    vsnprintf(buffer, MAX_MESSAGE_LENGTH, fmt, args);

    for (usize i = 0; i < m_sinkCount; ++i) {
        if (m_sinks[i]) {
            m_sinks[i](level, category ? category : "", buffer);
        }
    }
}

void LogSystem::setLevel(LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = minLevel;
}

LogLevel LogSystem::getLevel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_minLevel;
}

void LogSystem::setCategoryEnabled(const char* category, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CategoryEntry* entry = findCategory(category);
    if (entry) {
        entry->enabled = enabled;
        return;
    }

    if (m_categoryCount < MAX_CATEGORIES) {
        auto& e = m_categories[m_categoryCount];
        strncpy(e.name, category, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        e.enabled = enabled;
        ++m_categoryCount;
    }
}

bool LogSystem::isCategoryEnabled(const char* category) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const CategoryEntry* entry = findCategory(category);
    if (entry) return entry->enabled;
    return true;
}

void LogSystem::addSink(SinkFn sink) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sinkCount < MAX_SINKS) {
        m_sinks[m_sinkCount] = std::move(sink);
        ++m_sinkCount;
    }
}

void LogSystem::clearSinks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (usize i = 0; i < m_sinkCount; ++i) {
        m_sinks[i] = nullptr;
    }
    m_sinkCount = 0;
}

usize LogSystem::sinkCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sinkCount;
}

const char* LogSystem::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

LogSystem::CategoryEntry* LogSystem::findCategory(const char* category) {
    for (usize i = 0; i < m_categoryCount; ++i) {
        if (strcmp(m_categories[i].name, category) == 0) {
            return &m_categories[i];
        }
    }
    return nullptr;
}

const LogSystem::CategoryEntry* LogSystem::findCategory(const char* category) const {
    for (usize i = 0; i < m_categoryCount; ++i) {
        if (strcmp(m_categories[i].name, category) == 0) {
            return &m_categories[i];
        }
    }
    return nullptr;
}

} // namespace Caffeine::Debug
