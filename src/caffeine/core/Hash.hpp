#pragma once

// Integer hashing for deterministic procedural content.
//
// Everything here is plain 64-bit integer arithmetic (SplitMix64), so the results are
// identical on every platform, compiler and thread count. Never feed a floating point sum
// into a placement decision: derive a seed with `deriveSeed` and turn it into a number with
// `toUnit`/`toRange`. The golden values in tests/test_hash.cpp pin the algorithm: changing
// it changes every generated world, so bump GENERATOR_VERSION and keep the old path instead.

#include <cstdint>

namespace Caffeine::Core {

/// Version of the hashing and placement algorithm, stored with generated content.
/// 1 = the legacy 32-bit `Procedural::hash01`, 2 = this header.
inline constexpr std::uint32_t GENERATOR_VERSION = 2;

/// SplitMix64 finaliser: a bijective mix of one 64-bit value.
constexpr std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/// Order-dependent combination of two values.
constexpr std::uint64_t hashCombine(std::uint64_t a, std::uint64_t b) {
    return splitMix64(a ^ (splitMix64(b) + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2)));
}

/// A child seed for a purpose (`salt`) at integer cell coordinates. Pure function of its inputs.
constexpr std::uint64_t deriveSeed(std::uint64_t parent, std::uint32_t salt, std::int32_t x,
                                   std::int32_t z) {
    std::uint64_t h = hashCombine(parent, salt);
    h = hashCombine(h, static_cast<std::uint32_t>(x));
    return hashCombine(h, static_cast<std::uint32_t>(z));
}

/// Uniform double in [0, 1) from the top 53 bits.
constexpr double toUnit(std::uint64_t hash) {
    return static_cast<double>(hash >> 11) * (1.0 / 9007199254740992.0);
}

/// Uniform double in [lo, hi).
constexpr double toRange(std::uint64_t hash, double lo, double hi) {
    return lo + (hi - lo) * toUnit(hash);
}

/// Next value of a seeded stream (advance the state, return the output).
constexpr std::uint64_t nextRandom(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    return splitMix64(state - 0x9E3779B97F4A7C15ull);
}

}  // namespace Caffeine::Core
