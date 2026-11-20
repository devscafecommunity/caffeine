#include "catch.hpp"
#include "../src/caffeine/core/Hash.hpp"

#include <thread>
#include <vector>

using namespace Caffeine::Core;

TEST_CASE("Hash: golden values pin the algorithm on every platform", "[hash][determinism]") {
    CHECK(splitMix64(0) == 0xe220a8397b1dcdafull);
    CHECK(splitMix64(1) == 0x910a2dec89025cc1ull);
    CHECK(splitMix64(0xFFFFFFFFFFFFFFFFull) == 0xe4d971771b652c20ull);
    CHECK(deriveSeed(42, 7, 3, -5) == 0x9fd0188ac08d7c44ull);
    CHECK(deriveSeed(42, 7, -5, 3) == 0xbfb8f192698b81d0ull);
    CHECK(deriveSeed(0, 0, 0, 0) == 0x488f4fa3326fde8bull);
    CHECK(toUnit(deriveSeed(42, 7, 3, -5)) == Approx(0.624269040942229).epsilon(1e-15));
    static_assert(deriveSeed(0, 0, 0, 0) == 0x488f4fa3326fde8bull, "constexpr and stable");
    static_assert(GENERATOR_VERSION == 2, "bump together with the golden values");
}

TEST_CASE("Hash: coordinates and salts are not interchangeable", "[hash]") {
    CHECK(deriveSeed(1, 2, 3, 4) != deriveSeed(1, 2, 4, 3));
    CHECK(deriveSeed(1, 2, 3, 4) != deriveSeed(1, 3, 3, 4));
    CHECK(deriveSeed(1, 2, 3, 4) != deriveSeed(2, 2, 3, 4));
    CHECK(toRange(deriveSeed(5, 1, 0, 0), 10.0, 20.0) >= 10.0);
    CHECK(toRange(deriveSeed(5, 1, 0, 0), 10.0, 20.0) < 20.0);
}

TEST_CASE("Hash: chunk results do not depend on the number of threads", "[hash][determinism]") {
    constexpr int N = 4096;
    auto fill = [](int threads) {
        std::vector<std::uint64_t> out(N);
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&out, t, threads] {
                for (int i = t; i < N; i += threads) out[i] = deriveSeed(99, 3, i % 64, i / 64);
            });
        }
        for (auto& th : pool) th.join();
        return out;
    };
    CHECK(fill(1) == fill(7));
}

TEST_CASE("Hash: values are evenly spread over [0, 1)", "[hash]") {
    double sum = 0.0;
    int below = 0;
    constexpr int N = 20000;
    for (int i = 0; i < N; ++i) {
        const double v = toUnit(deriveSeed(7, 1, i, -i));
        sum += v;
        if (v < 0.5) ++below;
    }
    CHECK(sum / N == Approx(0.5).margin(0.01));
    CHECK(below > N * 0.48);
    CHECK(below < N * 0.52);
}
