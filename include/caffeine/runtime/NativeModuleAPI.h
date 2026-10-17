/**
 * Caffeine runtime native module ABI (play mode).
 * Not used by Doppio editor plugins — see PluginAPI.hpp for IDE extensions.
 */
#ifndef CAFFEINE_RUNTIME_NATIVE_MODULE_API_H
#define CAFFEINE_RUNTIME_NATIVE_MODULE_API_H

#include <stdint.h>

#ifdef _WIN32
#define CAFFEINE_NATIVE_EXPORT __declspec(dllexport)
#define CAFFEINE_NATIVE_IMPORT __declspec(dllimport)
#else
#define CAFFEINE_NATIVE_EXPORT __attribute__((visibility("default")))
#define CAFFEINE_NATIVE_IMPORT
#endif

#define CAFFEINE_NATIVE_ABI_VERSION 1u

typedef struct CaffeineNativeModuleInfo {
    uint32_t abi_version;
    const char* name;
    const char* version;
} CaffeineNativeModuleInfo;

/**
 * Required export: returns static module metadata.
 * CAFFEINE_NATIVE_EXPORT const CaffeineNativeModuleInfo* caffeine_native_module_info(void);
 */

/**
 * Optional export: dispatch by function name. Arguments and result are UTF-8 JSON strings.
 * Return 0 on success, non-zero on error (message in out_buf when provided).
 * CAFFEINE_NATIVE_EXPORT int caffeine_native_call(const char* function, const char* args_json,
 *                                               char* out_buf, uint32_t out_capacity);
 */

typedef void (*CaffeineNativeOnLoadFn)(void* host_opaque);
typedef void (*CaffeineNativeOnUnloadFn)(void* host_opaque);

/**
 * Optional lifecycle exports:
 * CAFFEINE_NATIVE_EXPORT void caffeine_native_on_load(void* host_opaque);
 * CAFFEINE_NATIVE_EXPORT void caffeine_native_on_unload(void* host_opaque);
 */

#endif /* CAFFEINE_RUNTIME_NATIVE_MODULE_API_H */
