#include "caffeine/animation/AnimationApi.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {

void registerAnimationScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["animation"] = lua.create_table();
    sol::table animation = lua["caffeine"]["animation"];

    animation["playClip"] = [worldPtr](u32 entityId, const std::string& path) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::playClip(**worldPtr, ECS::Entity(entityId, *worldPtr), path);
    };
    animation["setBool"] = [worldPtr](u32 entityId, const std::string& name, bool value) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::setBool(**worldPtr, ECS::Entity(entityId, *worldPtr), name.c_str(), value);
    };
    animation["setFloat"] = [worldPtr](u32 entityId, const std::string& name, f32 value) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::setFloat(**worldPtr, ECS::Entity(entityId, *worldPtr), name.c_str(), value);
    };
    animation["setTrigger"] = [worldPtr](u32 entityId, const std::string& name) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::setTrigger(**worldPtr, ECS::Entity(entityId, *worldPtr), name.c_str());
    };
    animation["playSkinnedClip"] = [worldPtr](u32 entityId, const std::string& meshPath, i32 clipIndex) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::playSkinnedClip(**worldPtr, ECS::Entity(entityId, *worldPtr), meshPath, clipIndex);
    };
    animation["setSpriteSheet"] = [worldPtr](u32 entityId, u32 columns, u32 rows, u32 frameCount) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::setSpriteSheet(**worldPtr, ECS::Entity(entityId, *worldPtr), columns, rows, frameCount);
    };
    animation["playState"] = [worldPtr](u32 entityId, const std::string& state) {
        if (!worldPtr || !*worldPtr) return false;
        return Animation::playState(**worldPtr, ECS::Entity(entityId, *worldPtr), state.c_str());
    };
}

}  // namespace Caffeine::Script
