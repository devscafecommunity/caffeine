#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "scene/SceneSerializer.hpp"

#include <vector>
#include <memory>
#include <unordered_map>

namespace Caffeine::Scene {

struct Color {
    u8 r = 0, g = 0, b = 0, a = 255;
};

enum class TransitionType : u8 { None, Fade, Slide, Custom };

struct TransitionConfig {
    TransitionType type      = TransitionType::Fade;
    f32            duration  = 0.5f;
    Color          fadeColor = {};
    TransitionConfig() = default;
};

class SceneManager {
public:
    using SceneHandle = u32;
    static constexpr SceneHandle kInvalidHandle = ~u32(0);

    SceneHandle loadScene(const char* path, bool async = false) {
        (void)async;
        auto world = std::make_unique<ECS::World>();
        SceneSerializer serializer(*world);
        if (!serializer.deserialize(path)) return kInvalidHandle;
        SceneHandle handle = m_nextHandle++;
        m_preloaded[handle] = std::move(world);
        return handle;
    }

    bool switchScene(const char* path, TransitionConfig trans = {}) {
        auto world = std::make_unique<ECS::World>();
        SceneSerializer serializer(*world);
        if (!serializer.deserialize(path)) return false;
        m_worldStack.clear();
        m_worldStack.push_back(std::move(world));
        m_transitioning = true;
        m_currentTrans  = trans;
        m_transProgress = 0.0f;
        return true;
    }

    bool pushScene(const char* path) {
        auto world = std::make_unique<ECS::World>();
        SceneSerializer serializer(*world);
        if (!serializer.deserialize(path)) return false;
        m_worldStack.push_back(std::move(world));
        return true;
    }

    void popScene() {
        if (!m_worldStack.empty()) m_worldStack.pop_back();
    }

    void setActiveWorld(std::unique_ptr<ECS::World> world) {
        m_worldStack.clear();
        m_worldStack.push_back(std::move(world));
    }

    void pushWorld(std::unique_ptr<ECS::World> world) {
        m_worldStack.push_back(std::move(world));
    }

    ECS::World* activeWorld() const {
        return m_worldStack.empty() ? nullptr : m_worldStack.back().get();
    }

    bool isTransitioning()    const { return m_transitioning; }
    f32  transitionProgress() const { return m_transProgress; }

    void update(f64 dt) {
        if (!m_transitioning) return;
        m_transProgress += static_cast<f32>(dt) / m_currentTrans.duration;
        if (m_transProgress >= 1.0f) {
            m_transProgress = 0.0f;
            m_transitioning = false;
        }
    }

private:
    std::vector<std::unique_ptr<ECS::World>>             m_worldStack;
    std::unordered_map<u32, std::unique_ptr<ECS::World>> m_preloaded;
    bool             m_transitioning = false;
    TransitionConfig m_currentTrans;
    f32              m_transProgress = 0.0f;
    u32              m_nextHandle    = 0;
};

}
