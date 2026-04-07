#pragma once

#include "Compiler.hpp"

#if defined(CF_DEBUG)
    #define CF_ASSERT_ENABLED 1
#else
    #define CF_ASSERT_ENABLED 0
#endif

namespace Caffeine {

CF_NORETURN void assertFailed(const char* condition, const char* file, int line, const char* msg);

#if CF_ASSERT_ENABLED
    #define CF_ASSERT(cond, msg) \
        ((cond) ? (void)0 : Caffeine::assertFailed(#cond, __FILE__, __LINE__, msg))
#else
    #define CF_ASSERT(cond, msg) ((void)0)
#endif

#define CF_UNREACHABLE() Caffeine::assertFailed("CF_UNREACHABLE", __FILE__, __LINE__, "Unreachable code executed")

#define CF_ASSERT_NOT_NULL(ptr) CF_ASSERT((ptr) != nullptr, "Pointer must not be null")

#if defined(CF_DEBUG)
    #define CF_DEBUG_ONLY(x) x
#else
    #define CF_DEBUG_ONLY(x)
#endif

inline CF_NORETURN void assertFailed(const char* condition, const char* file, int line, const char* msg) {
    (void)condition;
    (void)file;
    (void)line;
    (void)msg;
    CF_BUILTIN_TRAP();
}

} // namespace Caffeine