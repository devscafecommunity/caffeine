#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"

#include <string>

namespace Caffeine::ECS { class World; }

namespace Caffeine::Script {

/// One Dryad interpreter per `.dyd` file. Host bindings live here, not in the language.
class DryadHost {
public:
    DryadHost();
    ~DryadHost();

    DryadHost(const DryadHost&) = delete;
    DryadHost& operator=(const DryadHost&) = delete;

    void setWorld(ECS::World* world);

    bool load(const std::string& path, std::string* outError = nullptr);
    bool isLoaded(const std::string& path) const;

    /// Missing hook functions are a no-op and return true.
    bool call(const std::string& path, const char* function, ECS::Entity entity, f32 dt,
              std::string* outError = nullptr);

private:
    struct Impl;
    Impl* m_impl;
};

bool isDryadScriptPath(const std::string& path);

} // namespace Caffeine::Script
