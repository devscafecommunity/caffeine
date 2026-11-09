#pragma once

#include "core/Types.hpp"
#include "containers/HashMap.hpp"
#include "script/ScriptEngine.hpp"

#include <string>
#include <filesystem>

namespace Caffeine::Script {

class ScriptWatcher {
public:
    void init(ScriptEngine* engine, std::string_view scriptsDir);
    void shutdown();
    void poll();

private:
    ScriptEngine* m_engine = nullptr;
    std::string m_dir;
    HashMap<std::string, std::filesystem::file_time_type> m_mtimes;
};

} // namespace Caffeine::Script
