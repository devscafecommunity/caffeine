#pragma once

#include "caffeine/plugin/PluginAPI.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace Caffeine::Editor {

class PluginServiceRegistry {
public:
    static PluginServiceRegistry& instance();

    bool registerService(const std::string& pluginName, const std::string& serviceName,
                         PluginServiceHandler handler);
    void unregisterPlugin(const std::string& pluginName);

    bool invoke(const std::string& serviceName, void* editorContext, const void* request,
                std::size_t requestSize, void* response, std::size_t responseSize) const;

private:
    struct Entry {
        std::string pluginName;
        PluginServiceHandler handler = nullptr;
    };

    std::unordered_map<std::string, Entry> m_services;
};

}  // namespace Caffeine::Editor
