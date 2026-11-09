#pragma once

#include "core/Types.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/Entity.hpp"
#include "containers/Vector.hpp"
#include "script/ScriptEngine.hpp"

#include <memory>
#include <string>

namespace Caffeine { namespace Events { class EventBus; } }

namespace Caffeine::Script {

class DryadHost;

class ScriptSystem : public ECS::ISystem {
public:
    explicit ScriptSystem(ScriptEngine* engine, Events::EventBus* eventBus = nullptr);
    ~ScriptSystem() override;
    ScriptSystem(ScriptSystem&&) noexcept;
    ScriptSystem& operator=(ScriptSystem&&) noexcept;

    void onUpdate(ECS::World& world, f32 dt) override;
    void resetPlayState();

    /// Root used to resolve project-relative `.dyd` paths.
    void setProjectRoot(const std::string& root);

private:
    void processLuaScripts(ECS::World& world, f32 dt);
    void processDryadScripts(ECS::World& world, f32 dt);
    void processNativeScripts(ECS::World& world, f32 dt);
    void processCppScripts(ECS::World& world, f32 dt);
    void processPostProcessScripts(ECS::World& world, f32 dt);

    ScriptEngine* m_engine = nullptr;
    Events::EventBus* m_events = nullptr;
    std::unique_ptr<DryadHost> m_dryad;
    Vector<ECS::Entity> m_initializedLua;
    Vector<ECS::Entity> m_initializedDryad;
    Vector<ECS::Entity> m_initializedNative;
    Vector<ECS::Entity> m_initializedPostProcess;
    bool m_warnedNoLua = false;
    std::string m_projectRoot;
};

}
