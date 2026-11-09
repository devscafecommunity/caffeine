#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define CF_PLATFORM_WINDOWS 1
    #if defined(_WIN64)
        #define CF_PLATFORM_64BIT 1
    #else
        #define CF_PLATFORM_32BIT 1
    #endif
#elif defined(__linux__)
    #define CF_PLATFORM_LINUX 1
    #if defined(__LP64__)
        #define CF_PLATFORM_64BIT 1
    #else
        #define CF_PLATFORM_32BIT 1
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #define CF_PLATFORM_MACOS 1
    #if defined(__LP64__)
        #define CF_PLATFORM_64BIT 1
    #else
        #define CF_PLATFORM_32BIT 1
    #endif
#elif defined(__FreeBSD__)
    #define CF_PLATFORM_FREEBSD 1
    #define CF_PLATFORM_64BIT 1
#else
    #define CF_PLATFORM_UNKNOWN 1
#endif

#if defined(CF_PLATFORM_WINDOWS)
    #define CF_PLATFORM_NAME "Windows"
#elif defined(CF_PLATFORM_LINUX)
    #define CF_PLATFORM_NAME "Linux"
#elif defined(CF_PLATFORM_MACOS)
    #define CF_PLATFORM_NAME "macOS"
#elif defined(CF_PLATFORM_FREEBSD)
    #define CF_PLATFORM_NAME "FreeBSD"
#else
    #define CF_PLATFORM_NAME "Unknown"
#endif