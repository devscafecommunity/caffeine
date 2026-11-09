#pragma once

#if defined(_MSC_VER)
    #define CF_COMPILER_MSVC 1
    #define CF_COMPILER_NAME "MSVC"
#elif defined(__clang__)
    #define CF_COMPILER_CLANG 1
    #define CF_COMPILER_NAME "Clang"
#elif defined(__GNUC__)
    #define CF_COMPILER_GCC 1
    #define CF_COMPILER_NAME "GCC"
#else
    #define CF_COMPILER_UNKNOWN 1
    #define CF_COMPILER_NAME "Unknown"
#endif

#if defined(CF_COMPILER_CLANG) || defined(CF_COMPILER_GCC)
    #define CF_INLINE inline __attribute__((always_inline))
    #define CF_NOINLINE __attribute__((noinline))
    #define CF_NORETURN __attribute__((noreturn))
    #define CF_LIKELY(x) __builtin_expect(!!(x), 1)
    #define CF_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define CF_BUILTIN_TRAP() __builtin_trap()
    #define CF_BUILTIN_DEBUGTRAP() __builtin_debugtrap()
    #define CF_THREAD_LOCAL __thread
#elif defined(CF_COMPILER_MSVC)
    #define CF_INLINE __forceinline
    #define CF_NOINLINE __declspec(noinline)
    #define CF_NORETURN __declspec(noreturn)
    #define CF_LIKELY(x) (x)
    #define CF_UNLIKELY(x) (x)
    #define CF_BUILTIN_TRAP() __debugbreak()
    #define CF_BUILTIN_DEBUGTRAP() __debugbreak()
    #define CF_THREAD_LOCAL thread_local
#else
    #define CF_INLINE inline
    #define CF_NOINLINE
    #define CF_NORETURN
    #define CF_LIKELY(x) (x)
    #define CF_UNLIKELY(x) (x)
    #define CF_BUILTIN_TRAP() ((void)0)
    #define CF_BUILTIN_DEBUGTRAP() ((void)0)
    #define CF_THREAD_LOCAL thread_local
#endif

#if defined(CF_PLATFORM_WINDOWS)
    #define CF_EXPORT __declspec(dllexport)
    #define CF_IMPORT __declspec(dllimport)
    #define CF_HIDDEN
#else
    #define CF_EXPORT __attribute__((visibility("default")))
    #define CF_IMPORT
    #define CF_HIDDEN __attribute__((visibility("hidden")))
#endif

#if defined(CF_PLATFORM_WINDOWS)
    #define CF_CALL __cdecl
#else
    #define CF_CALL
#endif

#if __cplusplus >= 202002L
    #define CF_CPP20 1
#endif
#if __cplusplus >= 201703L
    #define CF_CPP17 1
#endif
#if __cplusplus >= 201402L
    #define CF_CPP14 1
#endif