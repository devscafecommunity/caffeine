// Dryad natives for entity names, hierarchy and random numbers. They call the same
// implementations as Lua's `caffeine.entity`, `caffeine.hierarchy` and `caffeine.random`.

#include "script/DryadNatives.hpp"
#include "script/ScriptWorldApi.hpp"

namespace Caffeine::Script {

namespace W = WorldApi;
using Args = std::vector<dryad::Value>;

namespace {
double num(const Args& a, size_t i) {
    if (i >= a.size()) return 0.0;
    if (a[i].is_integer()) return static_cast<double>(a[i].as_integer());
    if (a[i].is_float()) return a[i].as_float();
    return 0.0;
}
std::string str(const Args& a, size_t i) { return i < a.size() && a[i].is_string() ? a[i].as_string() : std::string(); }
dryad::Value ids(const std::vector<u32>& v) {
    dryad::Value arr = dryad::Value::create_array();
    for (u32 id : v) arr.array_push(dryad::Value(static_cast<int64_t>(id)));
    return arr;
}
dryad::Value id(u32 v) { return dryad::Value(static_cast<int64_t>(v)); }
}  // namespace

void bindWorldApiNatives(dryad::Interpreter& interp, ECS::World** worldPtr) {
    auto world = [worldPtr]() -> ECS::World* { return worldPtr ? *worldPtr : nullptr; };

    defineNative(interp, "caffeine_entity_name", [=](const Args& a) { return dryad::Value(W::nameOf(world(), static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_entity_set_name", [=](const Args& a) { return dryad::Value(W::setName(world(), static_cast<u32>(num(a, 0)), str(a, 1))); });
    defineNative(interp, "caffeine_entity_find", [=](const Args& a) { return id(W::find(world(), str(a, 0))); });
    defineNative(interp, "caffeine_entity_find_all", [=](const Args& a) { return ids(W::findAll(world(), str(a, 0))); });

    defineNative(interp, "caffeine_hierarchy_parent", [=](const Args& a) { return id(W::parentOf(world(), static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_hierarchy_set_parent", [=](const Args& a) { return dryad::Value(W::setParent(world(), static_cast<u32>(num(a, 0)), static_cast<u32>(num(a, 1)))); });
    defineNative(interp, "caffeine_hierarchy_detach", [=](const Args& a) { return dryad::Value(W::setParent(world(), static_cast<u32>(num(a, 0)), 0)); });
    defineNative(interp, "caffeine_hierarchy_children", [=](const Args& a) { return ids(W::childrenOf(world(), static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_hierarchy_child_count", [=](const Args& a) { return id(static_cast<u32>(W::childrenOf(world(), static_cast<u32>(num(a, 0))).size())); });
    defineNative(interp, "caffeine_hierarchy_root", [=](const Args& a) { return id(W::rootOf(world(), static_cast<u32>(num(a, 0)))); });

    defineNative(interp, "caffeine_random_set_seed", [](const Args& a) { W::Rng::instance().seed(static_cast<u64>(num(a, 0))); return dryad::Value(); });
    defineNative(interp, "caffeine_random_value", [](const Args&) { return dryad::Value(W::Rng::instance().next01()); });
    defineNative(interp, "caffeine_random_range", [](const Args& a) { return dryad::Value(W::Rng::instance().range(num(a, 0), num(a, 1))); });
    defineNative(interp, "caffeine_random_int", [](const Args& a) { return dryad::Value(static_cast<int64_t>(W::Rng::instance().integer(static_cast<i64>(num(a, 0)), static_cast<i64>(num(a, 1))))); });
    defineNative(interp, "caffeine_random_chance", [](const Args& a) { return dryad::Value(W::Rng::instance().chance(num(a, 0))); });
}

}  // namespace Caffeine::Script
