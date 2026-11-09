// ============================================================================
// @file    Types.hpp
// @brief   Basic type definitions for Caffeine Engine
// @note    Part of core/ module - no external dependencies
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace Caffeine {

// ============================================================================
// Integer Types - Explicit sizes for cross-platform consistency
// ============================================================================

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

// ============================================================================
// Floating Point Types
// ============================================================================

using f32 = float;
using f64 = double;

// ============================================================================
// Character Types
// ============================================================================

using c8  = char;
using c16 = char16_t;
using c32 = char32_t;

// ============================================================================
// Pointer-sized types
// ============================================================================

using usize = std::size_t;   // Unsigned size type
using isize = std::ptrdiff_t; // Signed size type

// ============================================================================
// Common constants
// ============================================================================

constexpr usize u8_max  = 255u;
constexpr usize u16_max = 65535u;
constexpr usize u32_max = 4294967295u;
constexpr usize u64_max = 18446744073709551615u;

constexpr i8  i8_max  = 127;
constexpr i8  i8_min  = -128;
constexpr i16 i16_max = 32767;
constexpr i16 i16_min = -32768;
constexpr i32 i32_max = 2147483647;
constexpr i32 i32_min = -2147483648;
constexpr i64 i64_max = 9223372036854775807LL;
constexpr i64 i64_min = -9223372036854775807LL - 1;

// ============================================================================
// Size assertions - Validate platform assumptions at compile time
// ============================================================================

static_assert(sizeof(i8)  == 1, "i8 must be 1 byte");
static_assert(sizeof(i16) == 2, "i16 must be 2 bytes");
static_assert(sizeof(i32) == 4, "i32 must be 4 bytes");
static_assert(sizeof(i64) == 8, "i64 must be 8 bytes");

static_assert(sizeof(u8)  == 1, "u8 must be 1 byte");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");

static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");

static_assert(sizeof(usize) >= 4, "usize must be at least 4 bytes");

// ============================================================================
// Common type aliases
// ============================================================================

using Byte = u8;

} // namespace Caffeine