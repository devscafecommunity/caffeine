#include "editor/PluginServiceRegistry.hpp"

#include <iostream>

namespace Caffeine::Editor {

PluginServiceRegistry& PluginServiceRegistry::instance() {
    static PluginServiceRegistry registry;
    return registry;
}

bool PluginServiceRegistry::registerService(const std::string& pluginName,
                                            const std::string& serviceName,
                                            PluginServiceHandler handler) {
    if (serviceName.empty() || !handler) return false;
    m_services[serviceName] = Entry{pluginName, handler};
    return true;
}

void PluginServiceRegistry::unregisterPlugin(const std::string& pluginName) {
    for (auto it = m_services.begin(); it != m_services.end();) {
        if (it->second.pluginName == pluginName) {
            it = m_services.erase(it);
        } else {
            ++it;
        }
    }
}

bool PluginServiceRegistry::invoke(const std::string& serviceName, void* editorContext,
                                   const void* request, std::size_t requestSize, void* response,
                                   std::size_t responseSize) const {
    const auto it = m_services.find(serviceName);
    if (it == m_services.end() || !it->second.handler) {
        return false;
    }
    return it->second.handler(editorContext, request, requestSize, response, responseSize);
}

}  // namespace Caffeine::Editor
