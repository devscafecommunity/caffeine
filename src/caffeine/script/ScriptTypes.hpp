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
