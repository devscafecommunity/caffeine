#include "editor/ComponentRegistry.hpp"
#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/CameraComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "audio/AudioComponents.hpp"
#include "script/ScriptTypes.hpp"
#include "ui/UIComponents.hpp"

namespace Caffeine::Editor {

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry reg;
    return reg;
}

void ComponentRegistry::registerComponent(ComponentEntry entry) {
    m_entries.push_back(std::move(entry));
}

void registerAllComponents(ComponentRegistry& reg) {
    reg.registerComponent({
        "Physics 2D", "RigidBody2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<Physics2D::RigidBody2D>(e); },
        [](ECS::World& w, ECS::Entity e){
            w.add<Physics2D::RigidBody2D>(e);
            if (!w.has<ECS::Velocity2D>(e)) w.add<ECS::Velocity2D>(e);
        }
    });
    reg.registerComponent({
        "Physics 2D", "Collider2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<Physics2D::Collider2D>(e); },
        [](ECS::World& w, ECS::Entity e){
            Physics2D::Collider2D col;
            col.shape = Physics2D::ColliderShape::AABB;
            col.size  = {1.0f, 1.0f};
            col.radius = 0.5f;
            w.add<Physics2D::Collider2D>(e, col);
        }
    });
    reg.registerComponent({
        "Rendering", "Sprite Renderer",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Sprite>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Sprite>(e); }
    });
    reg.registerComponent({
        "Rendering", "Mesh Filter",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::MeshFilterComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::MeshFilterComponent>(e); }
    });
    reg.registerComponent({
        "Rendering", "Mesh Renderer",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::MeshRendererComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::MeshRendererComponent>(e); }
    });
    reg.registerComponent({
        "Audio", "Audio Source",
        [](ECS::World& w, ECS::Entity e){ return w.has<Audio::AudioEmitter>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Audio::AudioEmitter>(e); }
    });
    reg.registerComponent({
        "Scripting", "Script (Lua)",
        [](ECS::World& w, ECS::Entity e){ return w.has<Script::ScriptComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Script::ScriptComponent>(e); }
    });
    reg.registerComponent({
        "Scripting", "Script (C++)",
        [](ECS::World& w, ECS::Entity e){ return w.has<Script::CppScriptComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Script::CppScriptComponent>(e); }
    });
    reg.registerComponent({
        "UI", "UI Widget",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UIWidget>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UIWidget>(e); }
    });
    reg.registerComponent({
        "UI", "UI Button",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UIButton>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UIButton>(e); }
    });
    reg.registerComponent({
        "UI", "UI Label",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UILabel>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UILabel>(e); }
    });
    reg.registerComponent({
        "UI", "UI Progress Bar",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UIProgressBar>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UIProgressBar>(e); }
    });
    reg.registerComponent({
        "UI", "UI Slider",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UISlider>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UISlider>(e); }
    });
    reg.registerComponent({
        "System", "Persistent",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::PersistentComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::PersistentComponent>(e); }
    });
    reg.registerComponent({
        "Game", "Health",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Health>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Health>(e); }
    });
    reg.registerComponent({
        "Game", "Velocity2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Velocity2D>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Velocity2D>(e); }
    });
    reg.registerComponent({
        "Camera", "Camera2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Camera2DComponent>(e); },
        [](ECS::World& w, ECS::Entity e){
            ECS::Camera2DComponent cam;
            cam.zoom = 1.0f;
            w.add<ECS::Camera2DComponent>(e, cam);
        }
    });
    reg.registerComponent({
        "Camera", "Camera3D",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Camera3DComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Camera3DComponent>(e); }
    });
}

} // namespace Caffeine::Editor
