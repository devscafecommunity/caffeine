#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"

#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace Caffeine::ECS { class World; }

namespace Caffeine::Script {

class CppScript {
public:
    virtual ~CppScript() = default;

    virtual void onCreate(ECS::Entity entity, ECS::World& world) { (void)entity; (void)world; }
    virtual void onUpdate(ECS::Entity entity, ECS::World& world, f32 dt) { (void)entity; (void)world; (void)dt; }
    virtual void onDestroy(ECS::Entity entity, ECS::World& world) { (void)entity; (void)world; }
    virtual void onCollision(ECS::Entity entity, ECS::Entity other, ECS::World& world) { (void)entity; (void)other; (void)world; }
};

class CppScriptRegistry {
public:
    using Factory = std::function<std::unique_ptr<CppScript>()>;

    static CppScriptRegistry& instance();

    void registerScript(const char* name, Factory factory);
    std::unique_ptr<CppScript> create(const std::string& name) const;
    const std::vector<std::string>& names() const { return m_names; }

private:
    struct Entry {
        std::string name;
        Factory factory;
    };
    std::vector<Entry> m_entries;
    std::vector<std::string> m_names;
};

}

#define REGISTER_CPP_SCRIPT(ClassName)                                                          \
    namespace {                                                                                  \
        static const bool s_registered_##ClassName = []() -> bool {                             \
            ::Caffeine::Script::CppScriptRegistry::instance().registerScript(                   \
                #ClassName,                                                                      \
                []() -> std::unique_ptr<::Caffeine::Script::CppScript> {                        \
                    return std::make_unique<ClassName>();                                        \
                }                                                                                \
            );                                                                                   \
            return true;                                                                         \
        }();                                                                                     \
    }
