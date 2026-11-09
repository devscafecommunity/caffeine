#include "script/NativeModuleHost.hpp"

#include "caffeine/runtime/NativeModuleAPI.h"
#include "debug/LogSystem.hpp"

#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Caffeine::Script {

namespace {

using InfoFn = const CaffeineNativeModuleInfo* (*)();
using CallFn = int (*)(const char*, const char*, char*, uint32_t);
using OnLoadFn = CaffeineNativeOnLoadFn;
using OnUnloadFn = CaffeineNativeOnUnloadFn;

void* openLibrary(const std::string& path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* getSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

std::string lastLoaderError() {
#if defined(_WIN32)
    return "LoadLibrary failed";
#else
    const char* err = dlerror();
    return err ? err : "dlopen failed";
#endif
}

}  // namespace

NativeModuleHost& NativeModuleHost::instance() {
    static NativeModuleHost s;
    return s;
}

void NativeModuleHost::setSearchRoot(const std::string& root) {
    m_searchRoot = root;
}

void NativeModuleHost::unloadAll() {
    for (auto& [name, mod] : m_modules) {
        if (mod.handle) {
            if (auto* onUnload = reinterpret_cast<OnUnloadFn>(getSymbol(mod.handle, "caffeine_native_on_unload"))) {
                onUnload(nullptr);
            }
            closeLibrary(mod.handle);
        }
    }
    m_modules.clear();
}

std::string NativeModuleHost::resolvePath(const std::string& moduleName) const {
    namespace fs = std::filesystem;
    fs::path path = fs::path(m_searchRoot) / "native" / moduleName;
#if defined(_WIN32)
    path += ".dll";
#else
    path += ".so";
#endif
    return path.string();
}

NativeModuleHost::Module* NativeModuleHost::findModule(const std::string& moduleName) {
    auto it = m_modules.find(moduleName);
    return it == m_modules.end() ? nullptr : &it->second;
}

bool NativeModuleHost::load(const std::string& moduleName, std::string* outError) {
    if (findModule(moduleName)) return true;

    const std::string path = resolvePath(moduleName);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (outError) *outError = "Native module not found: " + path;
        return false;
    }

    void* handle = openLibrary(path);
    if (!handle) {
        if (outError) *outError = lastLoaderError();
        return false;
    }

    auto* infoFn = reinterpret_cast<InfoFn>(getSymbol(handle, "caffeine_native_module_info"));
    if (!infoFn || !infoFn()) {
        closeLibrary(handle);
        if (outError) *outError = "Missing caffeine_native_module_info export";
        return false;
    }
    const CaffeineNativeModuleInfo* info = infoFn();
    if (!info || info->abi_version != CAFFEINE_NATIVE_ABI_VERSION) {
        closeLibrary(handle);
        if (outError) *outError = "Unsupported native module ABI version";
        return false;
    }

    if (auto* onLoad = reinterpret_cast<OnLoadFn>(getSymbol(handle, "caffeine_native_on_load"))) {
        onLoad(nullptr);
    }

    Module mod;
    mod.handle = handle;
    mod.path = path;
    mod.name = info->name ? info->name : moduleName;
    m_modules[moduleName] = std::move(mod);
    CF_INFO("Native", "Loaded runtime module '%s' from %s", moduleName.c_str(), path.c_str());
    return true;
}

bool NativeModuleHost::call(const std::string& moduleName, const std::string& functionName,
                            const std::string& argsJson, std::string& outResult,
                            std::string* outError) {
    Module* mod = findModule(moduleName);
    if (!mod || !mod->handle) {
        if (!load(moduleName, outError)) return false;
        mod = findModule(moduleName);
    }
    if (!mod || !mod->handle) {
        if (outError) *outError = "Module not loaded";
        return false;
    }

    auto* callFn = reinterpret_cast<CallFn>(getSymbol(mod->handle, "caffeine_native_call"));
    if (!callFn) {
        if (outError) *outError = "Module does not export caffeine_native_call";
        return false;
    }

    char buffer[4096];
    buffer[0] = '\0';
    const int code = callFn(functionName.c_str(), argsJson.c_str(), buffer, sizeof(buffer));
    outResult.assign(buffer);
    if (code != 0) {
        if (outError) *outError = outResult.empty() ? "Native call failed" : outResult;
        return false;
    }
    return true;
}

}  // namespace Caffeine::Script
