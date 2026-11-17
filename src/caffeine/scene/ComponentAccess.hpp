#pragma once

#include "ecs/Entity.hpp"
#include "scene/ComponentReflection.hpp"

#include <string>

namespace Caffeine::Scene::Access {

/// Language-neutral component access by name, shared by the scripting languages (Lua and Dryad).
/// Names are matched case-insensitively and ignore spaces: "Rigid Body 3D", "rigidbody3d".
/// On failure the functions return false and put a message for the script author in `error`.

bool has(ECS::World& world, ECS::Entity entity, const std::string& component, std::string* error);
bool add(ECS::World& world, ECS::Entity entity, const std::string& component, std::string* error);
bool remove(ECS::World& world, ECS::Entity entity, const std::string& component, std::string* error);

/// Reads a number from a field: floats, ints, enums (their index), bools (0/1) and vector parts
/// (`index` 0-3).
bool getNumber(ECS::World& world, ECS::Entity entity, const std::string& component,
               const std::string& field, int index, double& out, std::string* error);
bool setNumber(ECS::World& world, ECS::Entity entity, const std::string& component,
               const std::string& field, int index, double value, std::string* error);

/// Strings, and enums by their label ("Cube").
bool getText(ECS::World& world, ECS::Entity entity, const std::string& component,
             const std::string& field, std::string& out, std::string* error);
bool setText(ECS::World& world, ECS::Entity entity, const std::string& component,
             const std::string& field, const std::string& value, std::string* error);

/// The enum label for `index`, or the index of `label` (-1 when unknown).
std::string enumLabel(const Reflection::FieldDesc& field, int index);
int enumIndex(const Reflection::FieldDesc& field, const std::string& label);

}  // namespace Caffeine::Scene::Access
