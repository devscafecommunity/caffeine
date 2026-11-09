#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"

namespace Caffeine::Core {

// ============================================================================
// @brief  Frame statistics published by the engine every frame.
// ============================================================================
struct FrameStats {
    f64 deltaTime   = 0.0;
    f64 fps         = 0.0;
    u64 frameCount  = 0;
    f64 elapsedTime = 0.0;
};

// ============================================================================
// @brief  Extension point: the IDE implements this interface and registers
//         it via DebugHookRegistry so the core can notify the IDE about
//         engine events WITHOUT the core knowing about the IDE.
//
//  The core calls these hooks if-and-only-if a consumer has registered
//  them. Zero overhead when no hooks are registered (nullptr check).
// ============================================================================
struct IDebugHooks {
    virtual ~IDebugHooks() = default;

    /// Called when a new entity is created.
    virtual void onEntityCreated(ECS::Entity e) = 0;

    /// Called just before an entity is destroyed.
    virtual void onEntityDestroyed(ECS::Entity e) = 0;

    /// Called when a log message is emitted.
    virtual void onLogMessage(const char* msg, u8 level) = 0;

    /// Called at the end of every frame with timing stats.
    virtual void onFrameEnd(const FrameStats& stats) = 0;
};

// ============================================================================
// @brief  Optional registry for IDebugHooks.
//
//  Usage:
//    // In doppio (IDE) main:
//    struct MyHooks : Caffeine::Core::IDebugHooks { ... };
//    MyHooks hooks;
//    Caffeine::Core::DebugHookRegistry::registerHooks(&hooks);
//
//    // In core (engine):
//    if (auto* h = Caffeine::Core::DebugHookRegistry::hooks()) {
//        h->onFrameEnd(stats);
//    }
// ============================================================================
class DebugHookRegistry {
public:
    /// Register a hooks implementation (only one at a time).
    /// Pass nullptr to unregister.
    static void registerHooks(IDebugHooks* hooks) {
        s_hooks = hooks;
    }

    /// Get the currently registered hooks (may be nullptr).
    static IDebugHooks* hooks() {
        return s_hooks;
    }

private:
    static IDebugHooks* s_hooks;
};

} // namespace Caffeine::Core
