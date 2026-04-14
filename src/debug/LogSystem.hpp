#pragma once

#include "../core/Types.hpp"

#include <cstdarg>
#include <functional>
#include <mutex>

namespace Caffeine::Debug {

enum class LogLevel : u8 {
    Trace = 0,
    Info,
    Warn,
    Error,
    Fatal
};

class LogSystem {
public:
    static LogSystem& instance();

    void log(LogLevel level, const char* category, const char* fmt, ...);
    void vlog(LogLevel level, const char* category, const char* fmt, va_list args);

    void setLevel(LogLevel minLevel);
    LogLevel getLevel() const;

    void setCategoryEnabled(const char* category, bool enabled);
    bool isCategoryEnabled(const char* category) const;

    using SinkFn = std::function<void(LogLevel, const char*, const char*)>;
    void addSink(SinkFn sink);
    void clearSinks();
    usize sinkCount() const;

    static const char* levelToString(LogLevel level);

private:
    LogSystem();
    ~LogSystem() = default;
    LogSystem(const LogSystem&) = delete;
    LogSystem& operator=(const LogSystem&) = delete;

    struct CategoryEntry {
        char name[64];
        bool enabled;
    };

    static constexpr usize MAX_CATEGORIES = 64;
    static constexpr usize MAX_SINKS = 16;
    static constexpr usize MAX_MESSAGE_LENGTH = 2048;

    LogLevel m_minLevel = LogLevel::Trace;
    CategoryEntry m_categories[MAX_CATEGORIES]{};
    usize m_categoryCount = 0;
    SinkFn m_sinks[MAX_SINKS]{};
    usize m_sinkCount = 0;
    mutable std::mutex m_mutex;

    CategoryEntry* findCategory(const char* category);
    const CategoryEntry* findCategory(const char* category) const;
};

} // namespace Caffeine::Debug

#ifdef CF_DEBUG
    #define CF_TRACE(cat, ...) Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Trace, cat, __VA_ARGS__)
#else
    #define CF_TRACE(cat, ...) ((void)0)
#endif

#define CF_INFO(cat, ...)  Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Info,  cat, __VA_ARGS__)
#define CF_WARN(cat, ...)  Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Warn,  cat, __VA_ARGS__)
#define CF_ERROR(cat, ...) Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Error, cat, __VA_ARGS__)
#define CF_FATAL(cat, ...) Caffeine::Debug::LogSystem::instance().log(Caffeine::Debug::LogLevel::Fatal, cat, __VA_ARGS__)
