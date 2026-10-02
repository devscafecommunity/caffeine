#include "caffeine/navigation/NavigationApi.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {

void registerNavigationScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["navigation"] = lua.create_table();
    sol::table navigation = lua["caffeine"]["navigation"];

    navigation["setDestination"] = [worldPtr](u32 entityId, f32 x, f32 y, f32 z) {
        if (!worldPtr || !*worldPtr) return false;
        return Navigation::setDestination(**worldPtr, ECS::Entity(entityId, *worldPtr), Vec3(x, y, z));
    };
    navigation["setMode"] = [worldPtr](u32 entityId, const std::string& mode) {
        if (!worldPtr || !*worldPtr) return false;
        return Navigation::setMode(**worldPtr, ECS::Entity(entityId, *worldPtr),
                                   Navigation::modeFromName(mode));
    };
    navigation["addPatrolPoint"] = [worldPtr](u32 entityId, f32 x, f32 y, f32 z) {
        if (!worldPtr || !*worldPtr) return false;
        return Navigation::addPatrolPoint(**worldPtr, ECS::Entity(entityId, *worldPtr), Vec3(x, y, z));
    };
    navigation["setFollowTarget"] = [worldPtr](u32 entityId, u32 targetId) {
        if (!worldPtr || !*worldPtr) return false;
        return Navigation::setFollowTarget(**worldPtr, ECS::Entity(entityId, *worldPtr), targetId);
    };
    navigation["setBehaviorScript"] = [worldPtr](u32 entityId, const std::string& path) {
        if (!worldPtr || !*worldPtr) return false;
        return Navigation::setBehaviorScript(**worldPtr, ECS::Entity(entityId, *worldPtr), path);
    };
}

}  // namespace Caffeine::Script
