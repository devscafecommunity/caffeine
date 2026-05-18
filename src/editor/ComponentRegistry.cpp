#include "editor/ComponentRegistry.hpp"
#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
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
        [](ECS::World& w, ECS::Entity e){ w.add<Physics2D::RigidBody2D>(e); }
    });
    reg.registerComponent({
        "Physics 2D", "Collider2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<Physics2D::Collider2D>(e); },
        [](ECS::World& w, ECS::Entity e){
            Physics2D::Collider2D col;
            col.shape = Physics2D::ColliderShape::AABB;
            col.size  = {64.0f, 64.0f};
            col.radius = 32.0f;
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
        "Scripting", "Script",
        [](ECS::World& w, ECS::Entity e){ return w.has<Script::ScriptComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Script::ScriptComponent>(e); }
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
}

} // namespace Caffeine::Editor
