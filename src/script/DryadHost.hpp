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

    /// Project root used to resolve relative script paths (for example
    /// `scripts/player.dyd`). Relative paths that already exist from the
    /// current directory are kept.
    void setSearchRoot(const std::string& root);
    std::string searchRoot() const;

    /// Resolves a project-relative `.dyd` path to a filesystem path.
    std::string resolvePath(const std::string& path) const;

    bool load(const std::string& path, std::string* outError = nullptr);
    bool isLoaded(const std::string& path) const;

    /// Recompiles the script when its modification time changed.
    /// Returns true when the script was reloaded.
    bool reloadIfChanged(const std::string& path, std::string* outError = nullptr);

    /// Missing hook functions are a no-op and return true.
    bool call(const std::string& path, const char* function, ECS::Entity entity, f32 dt,
              std::string* outError = nullptr);

private:
    struct Impl;
    Impl* m_impl;
};

bool isDryadScriptPath(const std::string& path);

} // namespace Caffeine::Script
