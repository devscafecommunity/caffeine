#include "caffeine/ui/UI.hpp"
#include "editor/ParticleEditorPanel.hpp"

#include "editor/EffectObjectFactory.hpp"
#include "effects/EffectTypes.hpp"
#include "ecs/ComponentQuery.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Caffeine::Editor {
namespace {

const char* particleLabel(const Effects::EffectComponent& effect) {
    switch (static_cast<Effects::EffectDomain>(effect.domain)) {
        case Effects::EffectDomain::TwoD: return "2D Particle";
        case Effects::EffectDomain::ThreeD: return "3D Particle";
        case Effects::EffectDomain::Both: break;
    }
    return "Particle";
}

bool dragFloat(const char* label, f32& value, f32 speed, f32 lo, f32 hi) {
#ifdef CF_HAS_IMGUI
    return ImGui::DragFloat(label, &value, speed, lo, hi);
#else
    (void)label; (void)value; (void)speed; (void)lo; (void)hi;
    return false;
#endif
}

}  // namespace

void ParticleEditorPanel::onImGuiRender(EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_open) return;
    ECS::World* world = ctx.activeWorld;
    Panel root("Particles", "2D and 3D particle objects", "effects", &m_open);

    UIContext ui;
    if (!world) {
        root.emplace<Label>("Open a scene to edit particles.", TextTone::Muted);
        ui.draw(root);
        return;
    }

    auto& create = root.emplace<Card>("create");
    auto& createRow = create.emplace<Row>("create_row");
    createRow.emplace<Button>("Create 3D Particle", ButtonVariant::Primary)
        .setOnClick([&] {
            Effects::EffectComponent effect;
            Effects::configureParticle(effect, Effects::EffectDomain::ThreeD);
            spawnEffectObject(*world, ctx, "Particle 3D", effect, false);
        });
    createRow.emplace<Button>("Create 2D Particle")
        .setOnClick([&] {
            Effects::EffectComponent effect;
            Effects::configureParticle(effect, Effects::EffectDomain::TwoD);
            spawnEffectObject(*world, ctx, "Particle 2D", effect, true);
        });
    create.emplace<Elements::Paragraph>(
        "caffeine.effects.particle(entity, domain)  domain 1 = 2D, 2 = 3D\n"
        "caffeine.effects.particleParams(entity, rate, life, max, startSize, endSize, gravityY)");

    std::vector<ECS::Entity> particles;
    ECS::ComponentQuery query;
    query.with<Effects::EffectComponent>();
    world->forEach<Effects::EffectComponent>(
        query, [&](ECS::Entity entity, Effects::EffectComponent& effect) {
            if (static_cast<Effects::EffectKind>(effect.kind) ==
                Effects::EffectKind::Particles) {
                particles.push_back(entity);
            }
        });

    auto& listCard = root.emplace<Card>("particles");
    listCard.emplace<Label>("Particles", TextTone::Accent).setHeading(true);
    auto& list = listCard.emplace<ListBox>("particle_list");
    list.setHeight(150.0f);
    for (ECS::Entity entity : particles) {
        Effects::EffectComponent* effect = world->get<Effects::EffectComponent>(entity);
        if (!effect) continue;
        const bool current =
            ctx.selectedEntity.isValid() && ctx.selectedEntity.id() == entity.id();
        std::string label = std::string(particleLabel(*effect)) + "  " +
                            getEntityName(*world, entity);
        auto& item = list.emplace<ListItem>(label, current);
        item.setId("particle_" + std::to_string(entity.id()));
        item.setOnClick([&ctx, entity] { ctx.selectEntity(entity); });
    }
    if (particles.empty()) {
        listCard.emplace<Label>("No particle objects in this scene.", TextTone::Muted);
    }

    Effects::EffectComponent* selected = nullptr;
    ECS::Entity selectedEntity = ECS::Entity::INVALID;
    if (ctx.selectedEntity.isValid()) {
        Effects::EffectComponent* effect = world->get<Effects::EffectComponent>(ctx.selectedEntity);
        if (effect &&
            static_cast<Effects::EffectKind>(effect->kind) == Effects::EffectKind::Particles) {
            selected = effect;
            selectedEntity = ctx.selectedEntity;
        }
    }

    if (selected && selectedEntity.isValid()) {
        const Effects::EffectComponent before = *selected;
        bool enabled = selected->enabled != 0;
        int domain = static_cast<int>(selected->domain);
        int space = static_cast<int>(selected->space);
        int capacity = selected->maxParticles;
        bool deleted = false;

        auto& card = root.emplace<Card>("selected");
        card.emplace<Label>(getEntityName(*world, selectedEntity), TextTone::Accent)
            .setHeading(true);
        auto& grid = card.emplace<PropertyGrid>("particle_properties");
        grid.typedRow<BoolProperty>("Enabled", enabled);
        grid.typedRow<ChoiceProperty>("Domain", domain,
                                      std::vector<std::string>{"2D and 3D", "2D", "3D"});
        grid.typedRow<ChoiceProperty>("Space", space,
                                      std::vector<std::string>{"World", "Camera"});
        grid.typedRow<FloatProperty>("Rate", selected->rate, 0.0f, 400.0f, 0.5f, "%.2f");
        grid.typedRow<FloatProperty>("Lifetime", selected->lifetime, 0.05f, 20.0f, 0.02f,
                                     "%.2f");
        grid.typedRow<IntProperty>("Max", capacity, 1, 2048, 1.0f, "%d");
        grid.typedRow<FloatProperty>("Start Size", selected->startSize, 0.0f, 8.0f, 0.01f,
                                     "%.3f");
        grid.typedRow<FloatProperty>("End Size", selected->endSize, 0.0f, 8.0f, 0.01f,
                                     "%.3f");
        grid.typedRow<ColorFieldProperty>("Start Color", &selected->startColor.x);
        grid.typedRow<ColorFieldProperty>("End Color", &selected->endColor.x);
        grid.typedRow<FloatProperty>("Velocity Min X", selected->velocityMin.x, -100.0f,
                                     100.0f, 0.02f, "%.2f");
        grid.typedRow<FloatProperty>("Velocity Min Y", selected->velocityMin.y, -100.0f,
                                     100.0f, 0.02f, "%.2f");
        grid.typedRow<FloatProperty>("Velocity Min Z", selected->velocityMin.z, -100.0f,
                                     100.0f, 0.02f, "%.2f");
        grid.typedRow<FloatProperty>("Velocity Max X", selected->velocityMax.x, -100.0f,
                                     100.0f, 0.02f, "%.2f");
        grid.typedRow<FloatProperty>("Velocity Max Y", selected->velocityMax.y, -100.0f,
                                     100.0f, 0.02f, "%.2f");
        grid.typedRow<FloatProperty>("Velocity Max Z", selected->velocityMax.z, -100.0f,
                                     100.0f, 0.02f, "%.2f");
        grid.typedRow<FloatProperty>("Gravity X", selected->gravity.x, -50.0f, 50.0f, 0.05f,
                                     "%.2f");
        grid.typedRow<FloatProperty>("Gravity Y", selected->gravity.y, -50.0f, 50.0f, 0.05f,
                                     "%.2f");
        grid.typedRow<FloatProperty>("Gravity Z", selected->gravity.z, -50.0f, 50.0f, 0.05f,
                                     "%.2f");
        grid.typedRow<FloatProperty>("Drag", selected->drag, 0.0f, 8.0f, 0.01f, "%.2f");
        grid.typedRow<BufferProperty>("Material", selected->materialPath,
                                      sizeof(selected->materialPath));

        card.emplace<Button>("Delete Particle", ButtonVariant::Danger)
            .setOnClick([&] {
                ctx.beginUndo(EditorCommand::RemoveEntity, selectedEntity.id(), *world);
                world->destroy(selectedEntity);
                ctx.markDirty();
                ctx.endUndo(*world);
                deleted = true;
            });

        ui.draw(root);
        if (deleted) return;

        bool changed = false;
        if (enabled != (selected->enabled != 0)) {
            selected->enabled = enabled ? 1 : 0;
            changed = true;
        }
        if (domain != static_cast<int>(selected->domain)) {
            selected->domain = static_cast<u8>(domain);
            changed = true;
        }
        if (space != static_cast<int>(selected->space)) {
            selected->space = static_cast<u8>(space);
            changed = true;
        }
        if (capacity != selected->maxParticles) {
            selected->maxParticles = capacity;
            changed = true;
        }
        if (std::memcmp(&before, selected, sizeof(Effects::EffectComponent)) != 0) {
            changed = true;
        }
        if (changed) ctx.markDirty();
        return;
    }

    ui.draw(root);
#else
    (void)ctx;
#endif
}

}  // namespace Caffeine::Editor
