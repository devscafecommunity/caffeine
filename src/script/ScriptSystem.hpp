#pragma once

#include "core/Types.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/Entity.hpp"
#include "containers/Vector.hpp"
#include "script/ScriptEngine.hpp"

namespace Caffeine::Script {

class ScriptSystem : public ECS::ISystem {
public:
    explicit ScriptSystem(ScriptEngine* engine);

    void onUpdate(ECS::World& world, f32 dt) override;

private:
    void processLuaScripts(ECS::World& world, f32 dt);
    void processNativeScripts(ECS::World& world, f32 dt);

    ScriptEngine* m_engine;
    Vector<ECS::Entity> m_initializedLua;
    Vector<ECS::Entity> m_initializedNative;
};

} // namespace Caffeine::Script
