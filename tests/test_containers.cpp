#include "catch.hpp"
#include "../src/containers/Vector.hpp"
#include "../src/containers/HashMap.hpp"
#include "../src/containers/StringView.hpp"
#include "../src/containers/FixedString.hpp"

using namespace Caffeine;

TEST_CASE("Vector - Basic Operations", "[containers][vector]") {
    Vector<int> vec;

    REQUIRE(vec.size() == 0);
    REQUIRE(vec.capacity() == 0);
    REQUIRE(vec.empty() == true);

    vec.pushBack(10);
    vec.pushBack(20);
    vec.pushBack(30);

    REQUIRE(vec.size() == 3);
    REQUIRE(vec[0] == 10);
    REQUIRE(vec[1] == 20);
    REQUIRE(vec[2] == 30);
}

TEST_CASE("Vector - Push and Pop", "[containers][vector]") {
    Vector<int> vec;

    vec.pushBack(1);
    vec.pushBack(2);
    vec.pushBack(3);

    REQUIRE(vec.back() == 3);
    vec.popBack();
    REQUIRE(vec.back() == 2);
    REQUIRE(vec.size() == 2);
}

TEST_CASE("Vector - Resize", "[containers][vector]") {
    Vector<int> vec;
    vec.pushBack(1);
    vec.pushBack(2);

    vec.resize(5);
    REQUIRE(vec.size() == 5);
    REQUIRE(vec[3] == 0);
    REQUIRE(vec[4] == 0);

    vec.resize(1);
    REQUIRE(vec.size() == 1);
}

TEST_CASE("Vector - Reserve", "[containers][vector]") {
    Vector<int> vec;
    usize initialCapacity = vec.capacity();

    vec.reserve(100);
    REQUIRE(vec.capacity() >= 100);
    REQUIRE(vec.size() == 0);
}

TEST_CASE("Vector - Clear", "[containers][vector]") {
    Vector<int> vec;
    vec.pushBack(1);
    vec.pushBack(2);
    vec.pushBack(3);

    REQUIRE(vec.size() == 3);
    vec.clear();
    REQUIRE(vec.size() == 0);
}

TEST_CASE("HashMap - Basic Operations", "[containers][hashmap]") {
    HashMap<int, int> map;

    REQUIRE(map.size() == 0);
    REQUIRE(map.empty() == true);

    map.set(1, 100);
    map.set(2, 200);
    map.set(3, 300);

    REQUIRE(map.size() == 3);

    int* val = map.get(2);
    REQUIRE(val != nullptr);
    REQUIRE(*val == 200);
}

TEST_CASE("HashMap - Contains", "[containers][hashmap]") {
    HashMap<int, int> map;
    map.set(1, 100);
    map.set(2, 200);

    REQUIRE(map.contains(1) == true);
    REQUIRE(map.contains(2) == true);
    REQUIRE(map.contains(3) == false);
}

TEST_CASE("HashMap - Update Existing", "[containers][hashmap]") {
    HashMap<int, int> map;
    map.set(1, 100);

    map.set(1, 200);

    REQUIRE(map.size() == 1);
    int* val = map.get(1);
    REQUIRE(*val == 200);
}

TEST_CASE("HashMap - Remove", "[containers][hashmap]") {
    HashMap<int, int> map;
    map.set(1, 100);
    map.set(2, 200);
    map.set(3, 300);

    map.remove(2);
    REQUIRE(map.size() == 2);
    REQUIRE(map.contains(2) == false);
}

TEST_CASE("StringView - Basic Operations", "[containers][stringview]") {
    const char* str = "Hello World";
    StringView sv(str, 5);

    REQUIRE(sv.length() == 5);
    REQUIRE(sv.size() == 5);
    REQUIRE(sv.empty() == false);
    REQUIRE(sv[0] == 'H');
    REQUIRE(sv[4] == 'o');
}

TEST_CASE("StringView - Comparison", "[containers][stringview]") {
    StringView sv1("Hello");
    StringView sv2("Hello");
    StringView sv3("World");

    REQUIRE(sv1 == sv2);
    REQUIRE(sv1 != sv3);
    REQUIRE(sv1 < sv3);
}

TEST_CASE("StringView - Empty", "[containers][stringview]") {
    StringView sv;
    REQUIRE(sv.empty() == true);
    REQUIRE(sv.length() == 0);
}

TEST_CASE("FixedString - Basic Operations", "[containers][fixedstring]") {
    FixedString<32> fs("Hello");

    REQUIRE(fs.length() == 5);
    REQUIRE(fs.size() == 5);
    REQUIRE(fs.empty() == false);
    REQUIRE(fs[0] == 'H');
}

TEST_CASE("FixedString - Append", "[containers][fixedstring]") {
    FixedString<32> fs("Hello");
    fs.append(" World");

    REQUIRE(fs.length() == 11);
    REQUIRE(fs[5] == ' ');
    REQUIRE(fs[6] == 'W');
}

TEST_CASE("FixedString - Clear", "[containers][fixedstring]") {
    FixedString<32> fs("Hello");
    fs.clear();

    REQUIRE(fs.empty() == true);
    REQUIRE(fs.length() == 0);
}

TEST_CASE("FixedString - Comparison", "[containers][fixedstring]") {
    FixedString<32> fs1("Hello");
    FixedString<32> fs2("Hello");
    FixedString<32> fs3("World");

    REQUIRE(fs1 == fs2);
    REQUIRE(fs1 != fs3);
}

TEST_CASE("Container Stress - Vector Large Dataset", "[containers][stress]") {
    Vector<int> vec;
    vec.reserve(10000);

    constexpr int ITERATIONS = 10000;
    for (int i = 0; i < ITERATIONS; i++) {
        vec.pushBack(i);
    }

    REQUIRE(vec.size() == ITERATIONS);
    REQUIRE(vec[5000] == 5000);
    REQUIRE(vec.back() == ITERATIONS - 1);
}