#include "editor/BuildLog.hpp"

namespace Caffeine::Editor {

std::mutex BuildLog::s_mutex;
std::vector<std::string> BuildLog::s_lines;
std::string BuildLog::s_task;

void BuildLog::clear() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_lines.clear();
    s_task.clear();
}

void BuildLog::append(const char* level, const std::string& message) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_lines.push_back(std::string("[") + level + "] " + message);
    if (s_lines.size() > 2000) {
        s_lines.erase(s_lines.begin(), s_lines.begin() + 500);
    }
}

void BuildLog::info(const std::string& message) { append("Build", message); }
void BuildLog::warn(const std::string& message) { append("Warn", message); }
void BuildLog::error(const std::string& message) { append("Error", message); }
void BuildLog::runtime(const std::string& message) { append("Runtime", message); }

void BuildLog::setTask(const std::string& task) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_task = task;
}

std::string BuildLog::task() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_task;
}

std::vector<std::string> BuildLog::snapshot() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_lines;
}

std::string BuildLog::joinedText() {
    std::lock_guard<std::mutex> lock(s_mutex);
    std::string out;
    for (const auto& line : s_lines) {
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace Caffeine::Editor
