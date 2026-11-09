#include "script/ScriptWatcher.hpp"
#include "debug/LogSystem.hpp"

#include <system_error>

namespace Caffeine::Script {

void ScriptWatcher::init(ScriptEngine* engine, std::string_view scriptsDir) {
    m_engine = engine;
    m_dir = scriptsDir;
    if (m_dir.empty()) return;

    std::error_code ec;
    for (auto& entry : std::filesystem::recursive_directory_iterator(m_dir, ec)) {
        if (entry.path().extension() != ".lua") continue;
        auto mtime = std::filesystem::last_write_time(entry.path(), ec);
        if (ec) continue;
        m_mtimes.set(entry.path().string(), mtime);
    }

    if (ec) {
        CF_WARN("ScriptWatcher", "Error scanning scripts directory: %s", ec.message().c_str());
    }
    CF_INFO("ScriptWatcher", "Watching %s for script changes", m_dir.c_str());
}

void ScriptWatcher::shutdown() {
    m_mtimes.clear();
    m_engine = nullptr;
    m_dir.clear();
}

void ScriptWatcher::poll() {
    if (!m_engine || m_dir.empty()) return;

    std::error_code ec;
    for (auto& entry : std::filesystem::recursive_directory_iterator(m_dir, ec)) {
        if (entry.path().extension() != ".lua") continue;

        auto pathStr = entry.path().string();
        auto currentMtime = std::filesystem::last_write_time(entry.path(), ec);
        if (ec) continue;

        auto* storedMtime = m_mtimes.get(pathStr);
        if (!storedMtime) {
            m_mtimes.set(pathStr, currentMtime);
            continue;
        }

        if (currentMtime != *storedMtime) {
            m_mtimes.set(pathStr, currentMtime);
            std::string err;
            if (!m_engine->reloadScript(pathStr, &err)) {
                CF_ERROR("ScriptWatcher", "Failed to reload %s: %s", pathStr.c_str(), err.c_str());
            } else {
                CF_INFO("ScriptWatcher", "Hot-reloaded %s", pathStr.c_str());
            }
        }
    }

    if (ec) {
        CF_WARN("ScriptWatcher", "Error polling scripts directory: %s", ec.message().c_str());
    }
}

} // namespace Caffeine::Script
