#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "script/CppScript.hpp"

#include <string>
#include <functional>
#include <memory>

namespace Caffeine::Script {

struct ScriptComponent {
    std::string scriptPath;
    /// A disabled script is skipped by the ScriptSystem (no hooks, no initialisation).
    bool enabled = true;
    /// Lower runs first within a frame; ties keep entity order.
    i32 executionOrder = 0;
    /// Per-entity overrides of the script's exposed variables: `speed=4.5; god=true`.
    /// See ScriptProperties.hpp. Applied before every hook of this entity.
    std::string properties;
    /// Lua only: give this entity its own copy of the script (private globals) instead of
    /// sharing the file's state with every other entity that uses it.
    bool isolated = false;
};

using ScriptCallback = std::function<void(ECS::Entity, f32 dt)>;
using ScriptInitCallback = std::function<void(ECS::Entity)>;
using ScriptDestroyCallback = std::function<void(ECS::Entity)>;
using ScriptCollisionCallback = std::function<void(ECS::Entity, ECS::Entity)>;

struct NativeScriptComponent {
    ScriptInitCallback onCreate;
    ScriptCallback onUpdate;
    ScriptDestroyCallback onDestroy;
    ScriptCollisionCallback onCollision;
    bool initialized = false;
};

struct CppScriptComponent {
    std::string className;
    std::shared_ptr<CppScript> instance;
    bool initialized = false;
};

}
