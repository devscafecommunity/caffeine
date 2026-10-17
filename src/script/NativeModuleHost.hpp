#pragma once

#include "core/Types.hpp"

#include <string>
#include <unordered_map>

namespace Caffeine::Script {

/// Runtime play-mode native libraries (`project/native/*.so`). Separate from Doppio editor plugins.
class NativeModuleHost {
public:
    static NativeModuleHost& instance();

    void setSearchRoot(const std::string& root);
    void unloadAll();

    bool load(const std::string& moduleName, std::string* outError = nullptr);
    bool call(const std::string& moduleName, const std::string& functionName,
              const std::string& argsJson, std::string& outResult, std::string* outError = nullptr);

private:
    struct Module {
        void* handle = nullptr;
        std::string path;
        std::string name;
    };

    std::string resolvePath(const std::string& moduleName) const;
    Module* findModule(const std::string& moduleName);

    std::string m_searchRoot;
    std::unordered_map<std::string, Module> m_modules;
};

}  // namespace Caffeine::Script
