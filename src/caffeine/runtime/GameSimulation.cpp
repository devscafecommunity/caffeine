#include "runtime/GameSimulation.hpp"

#include "animation/AnimationSystem.hpp"
#include "debug/Profiler.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components3D.hpp"
#include "effects/EffectSystem.hpp"
#include "events/EventBus.hpp"
#include "events/Events.hpp"
#include "navigation/NavigationSystem.hpp"
#include "physics/PhysicsSystem2D.hpp"
#include "physics/PhysicsSystem3D.hpp"
#include "procedural/ProceduralWorldSystem.hpp"
#include "scene/EditorSceneSerializer.hpp"
#include "scene/EnvironmentEffectsSystem.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/NaturalFluidSystem.hpp"
#include "ui/UISystem.hpp"
#ifdef CF_HAS_SCRIPTING
#include "script/ScriptEngine.hpp"
#include "script/ScriptSystem.hpp"
#include "script/ScriptTypes.hpp"
#endif

#include <algorithm>
#include <filesystem>

namespace Caffeine::Runtime {

struct GameSimulation::Impl {
    bool running = false;
    u64 frames = 0;
    f32 time = 0.0f;
    std::string projectRoot;
    std::string pendingScene;

    Input::InputManager input;
    Events::EventBus events;
    Physics2D::PhysicsSystem2D physics2d{&events};
    Physics3D::PhysicsSystem3D physics3d{&events};
    Animation::AnimationSystem animation;
    UI::UISystem ui{&events};
#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine scripts;
    std::unique_ptr<Script::ScriptSystem> scriptSystem;
    bool scriptsReady = false;
#endif
};

GameSimulation::GameSimulation() : m_impl(std::make_unique<Impl>()) {}

GameSimulation::~GameSimulation() { stop(); }

bool GameSimulation::start(ECS::World& world, const std::string& projectRoot, const std::string& scenePath) {
    stop();
    Impl& g = *m_impl;
    g.running = true;
    g.frames = 0;
    g.time = 0.0f;
    g.projectRoot = projectRoot;
    g.pendingScene.clear();
    g.physics3d.reset();
    bool ok = true;
#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine::InitParams params;
    params.world = &world;
    params.input = &g.input;
    params.events = &g.events;
    params.ui = &g.ui;
    params.physics3d = &g.physics3d.world();
    g.scriptsReady = g.scripts.init(params);
    if (g.scriptsReady) {
        g.scripts.setSearchRoot(projectRoot);
        g.scripts.sceneRequests().setCurrent(scenePath);
        g.scriptSystem = std::make_unique<Script::ScriptSystem>(&g.scripts, &g.events);
        if (!projectRoot.empty()) g.scriptSystem->setProjectRoot(projectRoot);
        g.scriptSystem->resetPlayState();
        g.events.subscribe<Events::OnCollision2D>([&g, &world](const Events::OnCollision2D& event) {
            auto notify = [&](u32 entityId, u32 otherId) {
                ECS::Entity self(entityId, &world);
                ECS::Entity other(otherId, &world);
                if (!self.isValid() || !other.isValid()) return;
                const auto* script = world.get<Script::ScriptComponent>(self);
                if (!script || script->scriptPath.empty()) return;
                g.scripts.callOnCollision(script->scriptPath, self, other);
            };
            notify(event.entityA, event.entityB);
            notify(event.entityB, event.entityA);
        });
    } else {
        ok = false;
    }
#else
    (void)world;
    (void)scenePath;
    ok = false;
#endif
    return ok;
}

void GameSimulation::stop() {
    Impl& g = *m_impl;
    if (!g.running) return;
    g.running = false;
#ifdef CF_HAS_SCRIPTING
    g.scriptSystem.reset();
    if (g.scriptsReady) g.scripts.shutdown();
    g.scriptsReady = false;
#endif
    g.physics3d.reset();
    g.input.endFrame();
}

bool GameSimulation::running() const { return m_impl->running; }

void GameSimulation::update(ECS::World& world, f32 dt) {
    Impl& g = *m_impl;
    if (!g.running) return;
    dt = std::clamp(dt, 0.0f, 0.1f);
    g.input.beginFrame();
    { CF_PROFILE_SCOPE("Game::animation"); g.animation.onUpdate(world, dt); }
    Vec3 effectCamera{};
    {
        ECS::ComponentQuery query;
        query.with<ECS::Camera3DComponent>();
        query.with<ECS::Position3D>();
        world.forEach<ECS::Camera3DComponent, ECS::Position3D>(
            query, [&](ECS::Entity, ECS::Camera3DComponent&, ECS::Position3D& position) {
                effectCamera = position.position;
            });
    }
    { CF_PROFILE_SCOPE("Game::effects"); Effects::tickEffects(world, dt, effectCamera);
    Scene::tickEnvironmentEffects(world, dt, effectCamera);
    Scene::tickNaturalFluids(world, dt, false);
    Navigation::updateNavigation(world, dt); }
    { CF_PROFILE_SCOPE("Game::physics2d"); g.physics2d.onUpdate(world, dt); }
    { CF_PROFILE_SCOPE("Game::physics3d"); g.physics3d.onUpdate(world, dt); }
#ifdef CF_HAS_SCRIPTING
    if (g.scriptsReady && g.scriptSystem) {
        g.scripts.setWorld(&world);
        g.scripts.setInput(&g.input);
        {
            CF_PROFILE_SCOPE("Game::scripts");
            g.scriptSystem->onUpdate(world, dt);
        }
        // A script asked for another scene: swap it between frames.
        auto& requests = g.scripts.sceneRequests();
        if (requests.hasPending()) {
            const std::string requested = requests.take();
            std::filesystem::path next(requested);
            std::error_code ec;
            if (!std::filesystem::exists(next, ec)) next = std::filesystem::path(g.projectRoot) / requested;
            if (std::filesystem::exists(next, ec)) {
                g.physics3d.reset();
                Editor::SceneSerializer serializer(world);
                if (serializer.deserialize(next.string())) {
                    Scene::resetNaturalFluidRuntime();
                    g.scriptSystem->resetPlayState();
                    requests.setCurrent(requested);
                    g.pendingScene = next.string();
                }
            }
        }
    }
#endif
    { CF_PROFILE_SCOPE("Game::world_and_ui"); Procedural::ProceduralWorldSystem::update(world);
    g.ui.onUpdate(world, dt); }    Scene::propagateTransforms(world);
    g.input.endFrame();
    ++g.frames;
    g.time += dt;
}

void GameSimulation::keyDown(u32 scancode) {
    if (scancode < static_cast<u32>(Input::Key::KeyCount)) m_impl->input.injectKeyDown(static_cast<Input::Key>(scancode));
}

void GameSimulation::keyUp(u32 scancode) {
    if (scancode < static_cast<u32>(Input::Key::KeyCount)) m_impl->input.injectKeyUp(static_cast<Input::Key>(scancode));
}

void GameSimulation::releaseAllKeys() {
    for (u32 key = 0; key < static_cast<u32>(Input::Key::KeyCount); ++key) {
        m_impl->input.injectKeyUp(static_cast<Input::Key>(key));
    }
}

void GameSimulation::mouseMove(f32 x, f32 y) { m_impl->input.injectMouseMove(x, y); }

void GameSimulation::mouseButton(u32 button, bool down) {
    const auto b = static_cast<Input::MouseButton>(button);
    if (down) m_impl->input.injectMouseButtonDown(b);
    else m_impl->input.injectMouseButtonUp(b);
}

u64 GameSimulation::frames() const { return m_impl->frames; }
f32 GameSimulation::time() const { return m_impl->time; }

bool GameSimulation::scriptsRunning() const {
#ifdef CF_HAS_SCRIPTING
    return m_impl->scriptsReady;
#else
    return false;
#endif
}

std::string GameSimulation::takeSceneChange() {
    std::string out = std::move(m_impl->pendingScene);
    m_impl->pendingScene.clear();
    return out;
}

}  // namespace Caffeine::Runtime
