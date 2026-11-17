#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Caffeine::ECS { class World; }

namespace Caffeine::Scene::Reflection {

/// Kinds of reflected fields. The numeric values match the C ABI's CaffeineFieldKind.
enum class FieldKind : int {
    F32 = 0, Bool = 1, I32 = 2, U32 = 3, Vec2 = 4, Vec3 = 5, Vec4 = 6, String = 7, Enum = 8, Color = 9,
};

/// A field value: vectors use `vec`, enums and ints `integer`, bools `boolean`, strings `text`.
struct FieldValue {
    FieldKind kind = FieldKind::F32;
    float vec[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::int32_t integer = 0;
    std::int32_t boolean = 0;
    char text[256] = {};
};

struct FieldDesc {
    const char* name;
    FieldKind kind;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float step = 0.0f;
    std::vector<const char*> options;
    std::function<bool(ECS::World&, ECS::Entity, FieldValue&)> get;
    std::function<bool(ECS::World&, ECS::Entity, const FieldValue&)> set;
};

struct ComponentDesc {
    std::uint32_t id;
    const char* name;
    std::function<bool(ECS::World&, ECS::Entity)> has;
    std::function<void(ECS::World&, ECS::Entity)> add;
    std::function<void(ECS::World&, ECS::Entity)> remove;
    std::vector<FieldDesc> fields;
};

/// Every component the engine can describe, with typed access to its fields. The editor inspector,
/// the C ABI and the scripting languages all use this one table.
const std::vector<ComponentDesc>& registry();

const ComponentDesc* findById(std::uint32_t id);
/// Case-insensitive match on the display name ("Rigid Body 3D") or the name without spaces.
const ComponentDesc* findByName(const std::string& name);
const FieldDesc* findField(const ComponentDesc& component, const std::string& name);

std::vector<const ComponentDesc*> presentComponents(ECS::World& world, ECS::Entity entity);

}  // namespace Caffeine::Scene::Reflection
