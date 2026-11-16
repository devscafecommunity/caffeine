#pragma once

#include "core/Types.hpp"
#include <mutex>
#include <string>
#include <vector>

namespace Caffeine::Editor {

class BuildLog {
public:
    static void clear();
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
    static void runtime(const std::string& message);
    static void setTask(const std::string& task);
    static std::string task();
    static std::vector<std::string> snapshot();
    static std::string joinedText();

private:
    static void append(const char* level, const std::string& message);

    static std::mutex s_mutex;
    static std::vector<std::string> s_lines;
    static std::string s_task;
};

} // namespace Caffeine::Editor
