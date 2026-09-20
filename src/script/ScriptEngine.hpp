#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Caffeine { namespace Input { class InputManager; } }
namespace Caffeine { namespace Events { class EventBus; } }

namespace Caffeine::Script {

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    struct InitParams {
        ECS::World* world = nullptr;
        Input::InputManager* input = nullptr;
        Events::EventBus* events = nullptr;
    };

    bool init(const InitParams& params);
    void shutdown();
    void setWorld(ECS::World* world);
    void setInput(Input::InputManager* input);
    void setSearchRoot(const std::string& root);

    struct ExposedVar {
        enum class Kind { Number, Boolean, String };
        std::string name;
        Kind kind = Kind::Number;
        double number = 0.0;
        bool boolean = false;
        std::string string;
    };
    std::vector<ExposedVar> listExposedVars(const std::string& path);
    bool setExposedVar(const std::string& path, const ExposedVar& var);

    bool loadScript(const std::string& path, std::string* outError = nullptr);
    bool loadString(const std::string& code, const std::string& virtualPath,
                    std::string* outError = nullptr);
    bool reloadScript(const std::string& path, std::string* outError = nullptr);
    bool isLoaded(const std::string& path) const;

    // Lifecycle calls
    bool callOnCreate(const std::string& path, ECS::Entity entity);
    bool callOnUpdate(const std::string& path, ECS::Entity entity, f32 dt);
    bool callOnDestroy(const std::string& path, ECS::Entity entity);
    bool callOnCollision(const std::string& path, ECS::Entity entity,
                         ECS::Entity other);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void registerBindings();
};

} // namespace Caffeine::Script
