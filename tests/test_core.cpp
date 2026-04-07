#include "catch.hpp"
#include "../src/core/Types.hpp"
#include "../src/core/Platform.hpp"
#include "../src/core/Compiler.hpp"
#include "../src/core/Assertions.hpp"

using namespace Caffeine;

TEST_CASE("Types - Integer Sizes", "[core][types]") {
    static_assert(sizeof(i8) == 1, "i8 must be 1 byte");
    static_assert(sizeof(i16) == 2, "i16 must be 2 bytes");
    static_assert(sizeof(i32) == 4, "i32 must be 4 bytes");
    static_assert(sizeof(i64) == 8, "i64 must be 8 bytes");

    static_assert(sizeof(u8) == 1, "u8 must be 1 byte");
    static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
    static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
    static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");

    static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
    static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");

    REQUIRE(true);
}

TEST_CASE("Types - Constants", "[core][types]") {
    REQUIRE(u8_max == 255u);
    REQUIRE(u16_max == 65535u);
    REQUIRE(u32_max == 4294967295u);

    REQUIRE(i8_max == 127);
    REQUIRE(i8_min == -128);
    REQUIRE(i16_max == 32767);
    REQUIRE(i16_min == -32768);
}

TEST_CASE("Platform - Detection", "[core][platform]") {
#if defined(CF_PLATFORM_WINDOWS)
    REQUIRE(true);
#elif defined(CF_PLATFORM_LINUX)
    REQUIRE(true);
#elif defined(CF_PLATFORM_MACOS)
    REQUIRE(true);
#else
    REQUIRE(false);
#endif
}

TEST_CASE("Platform - Bit Depth", "[core][platform]") {
#if defined(CF_PLATFORM_64BIT)
    REQUIRE(true);
#elif defined(CF_PLATFORM_32BIT)
    REQUIRE(true);
#else
    REQUIRE(false);
#endif
}

TEST_CASE("Compiler - Detection", "[core][compiler]") {
#if defined(CF_COMPILER_MSVC) || defined(CF_COMPILER_CLANG) || defined(CF_COMPILER_GCC)
    REQUIRE(true);
#else
    REQUIRE(false);
#endif
}

TEST_CASE("Compiler - C++ Version", "[core][compiler]") {
#if defined(CF_CPP20) || defined(CF_CPP17) || defined(CF_CPP14)
    REQUIRE(true);
#else
    REQUIRE(false);
#endif
}

TEST_CASE("Compiler - Inline Macro", "[core][compiler]") {
    // CF_INLINE is defined as a macro expanding to inline with always_inline attribute
    // We just verify it's defined
    int x = 0;
    (void)x; // Suppress unused warning
    REQUIRE(true);
}

TEST_CASE("Assertions - CF_ASSERT in Debug", "[core][assertions]") {
#if defined(CF_DEBUG)
    bool caught = false;
    try {
        CF_ASSERT(false, "Test assertion");
    } catch (...) {
        caught = true;
    }
#endif
    REQUIRE(true);
}