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
    ScriptEngine* m_engine;
    Vector<ECS::Entity> m_initialized;
};

} // namespace Caffeine::Script
