#include "script/ScriptWorldApi.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {

namespace W = WorldApi;

static sol::table idTable(sol::state_view lua, const std::vector<u32>& ids) {
    sol::table t = lua.create_table();
    for (size_t i = 0; i < ids.size(); ++i) t[i + 1] = ids[i];
    return t;
}

void registerWorldApiBindings(sol::state& lua, ECS::World** worldPtr) {
    auto world = [worldPtr]() -> ECS::World* { return worldPtr ? *worldPtr : nullptr; };

    sol::table ent = lua["caffeine"]["entity"].get_or_create<sol::table>();
    ent["name"] = [world](u32 id) { return W::nameOf(world(), id); };
    ent["setName"] = [world](u32 id, const std::string& n) { return W::setName(world(), id, n); };
    ent["find"] = [world](const std::string& n) { return W::find(world(), n); };
    ent["findAll"] = [world, &lua](const std::string& n) { return idTable(lua, W::findAll(world(), n)); };
    ent["isAlive"] = [world](u32 id) { ECS::Entity e; return W::alive(world(), id, e); };

    sol::table h = lua["caffeine"]["hierarchy"].get_or_create<sol::table>();
    h["parent"] = [world](u32 id) { return W::parentOf(world(), id); };
    h["setParent"] = [world](u32 id, u32 parent) { return W::setParent(world(), id, parent); };
    h["detach"] = [world](u32 id) { return W::setParent(world(), id, 0); };
    h["children"] = [world, &lua](u32 id) { return idTable(lua, W::childrenOf(world(), id)); };
    h["childCount"] = [world](u32 id) { return static_cast<u32>(W::childrenOf(world(), id).size()); };
    h["root"] = [world](u32 id) { return W::rootOf(world(), id); };

    sol::table r = lua["caffeine"]["random"].get_or_create<sol::table>();
    r["setSeed"] = [](double s) { W::Rng::instance().seed(static_cast<u64>(s)); };
    r["value"] = []() { return W::Rng::instance().next01(); };
    r["range"] = [](double lo, double hi) { return W::Rng::instance().range(lo, hi); };
    r["int"] = [](double lo, double hi) {
        return static_cast<double>(W::Rng::instance().integer(static_cast<i64>(lo), static_cast<i64>(hi)));
    };
    r["chance"] = [](double p) { return W::Rng::instance().chance(p); };
}

}  // namespace Caffeine::Script
