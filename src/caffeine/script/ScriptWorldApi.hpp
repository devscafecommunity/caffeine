#pragma once

// Language-independent implementations behind `caffeine.entity`, `caffeine.hierarchy` and
// `caffeine.random`. Lua (WorldApiBindings.cpp) and Dryad (DryadWorldBindings.cpp) both call
// these, so the two languages cannot disagree about behaviour.

#include "core/Types.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/World.hpp"
#include "scene/EditorContext.hpp"
#include "scene/SceneComponents.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Caffeine::Script::WorldApi {

inline bool alive(ECS::World* world, u32 id, ECS::Entity& out) {
    if (!world || id == 0) return false;
    out = ECS::Entity(id, world);
    return out.isValid() && world->isEntityAlive(out);
}

// --- entity names ---------------------------------------------------------------------------

inline std::string nameOf(ECS::World* world, u32 id) {
    ECS::Entity e;
    if (!alive(world, id, e)) return {};
    if (auto* nc = world->get<Editor::NameComponent>(e)) return nc->name;
    return {};
}

inline bool setName(ECS::World* world, u32 id, const std::string& name) {
    ECS::Entity e;
    if (!alive(world, id, e)) return false;
    Editor::setEntityName(*world, e, name.c_str());
    return true;
}

inline std::vector<u32> findAll(ECS::World* world, const std::string& name) {
    std::vector<u32> out;
    if (!world) return out;
    ECS::ComponentQuery q;
    q.with<Editor::NameComponent>();
    world->forEach<Editor::NameComponent>(q, [&](ECS::Entity e, Editor::NameComponent& nc) {
        if (name == nc.name) out.push_back(e.id());
    });
    std::sort(out.begin(), out.end());
    return out;
}

/// Lowest entity id with this name, or 0.
inline u32 find(ECS::World* world, const std::string& name) {
    const auto all = findAll(world, name);
    return all.empty() ? 0u : all.front();
}

// --- hierarchy ------------------------------------------------------------------------------

inline u32 parentOf(ECS::World* world, u32 id) {
    ECS::Entity e;
    if (!alive(world, id, e)) return 0;
    auto* p = world->get<Scene::Parent>(e);
    return p && p->parent.isValid() ? p->parent.id() : 0u;
}

inline std::vector<u32> childrenOf(ECS::World* world, u32 id) {
    std::vector<u32> out;
    if (!world || id == 0) return out;
    ECS::ComponentQuery q;
    q.with<Scene::Parent>();
    world->forEach<Scene::Parent>(q, [&](ECS::Entity e, Scene::Parent& p) {
        if (p.parent.isValid() && p.parent.id() == id) out.push_back(e.id());
    });
    std::sort(out.begin(), out.end());
    return out;
}

inline u32 rootOf(ECS::World* world, u32 id) {
    u32 cur = id;
    for (int guard = 0; guard < 64; ++guard) {
        const u32 p = parentOf(world, cur);
        if (p == 0) return cur;
        cur = p;
    }
    return cur;
}

/// `parent` 0 detaches. Refuses a missing entity, itself and any cycle.
inline bool setParent(ECS::World* world, u32 child, u32 parent) {
    ECS::Entity c, p;
    if (!alive(world, child, c)) return false;
    if (parent == 0) {
        world->remove<Scene::Parent>(c);
        return true;
    }
    if (!alive(world, parent, p) || parent == child) return false;
    for (u32 cur = parent, guard = 0; cur != 0 && guard < 64; cur = parentOf(world, cur), ++guard) {
        if (cur == child) return false;  // would create a cycle
    }
    auto& comp = world->add<Scene::Parent>(c);
    comp.parent = p;
    comp.dirty = true;
    return true;
}

// --- random ---------------------------------------------------------------------------------

/// Deterministic xorshift64*; the sequence depends only on the seed.
class Rng {
public:
    static Rng& instance() { static Rng r; return r; }
    void seed(u64 s) { m_state = s ? s : 0x9E3779B97F4A7C15ull; }
    f64 next01() {
        m_state ^= m_state >> 12; m_state ^= m_state << 25; m_state ^= m_state >> 27;
        const u64 v = m_state * 0x2545F4914F6CDD1Dull;
        return static_cast<f64>(v >> 11) / 9007199254740992.0;  // 53 bits -> [0, 1)
    }
    f64 range(f64 lo, f64 hi) { return lo + (hi - lo) * next01(); }
    /// Inclusive on both ends; swaps reversed bounds.
    i64 integer(i64 lo, i64 hi) {
        if (lo > hi) std::swap(lo, hi);
        const u64 span = static_cast<u64>(hi - lo) + 1;
        return lo + static_cast<i64>(static_cast<u64>(next01() * static_cast<f64>(span)) % span);
    }
    bool chance(f64 p) { return next01() < p; }
private:
    u64 m_state = 0x9E3779B97F4A7C15ull;
};

}  // namespace Caffeine::Script::WorldApi
