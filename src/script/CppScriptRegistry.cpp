#include "script/CppScript.hpp"

namespace Caffeine::Script {

CppScriptRegistry& CppScriptRegistry::instance() {
    static CppScriptRegistry reg;
    return reg;
}

void CppScriptRegistry::registerScript(const char* name, Factory factory) {
    m_entries.push_back({name, std::move(factory)});
    m_names.push_back(name);
}

std::unique_ptr<CppScript> CppScriptRegistry::create(const std::string& name) const {
    for (const auto& entry : m_entries) {
        if (entry.name == name) return entry.factory();
    }
    return nullptr;
}

}
