#include "editor/ParticleEditorPanel.hpp"

#include "editor/EffectObjectFactory.hpp"
#include "effects/EffectTypes.hpp"
#include "ecs/ComponentQuery.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstdio>
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
    if (!m_open) return;
    ImGui::Begin("Particles", &m_open);
    ECS::World* world = ctx.activeWorld;
    if (!world) {
        ImGui::TextDisabled("Open a scene to edit particles.");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Create 3D Particle")) {
        Effects::EffectComponent effect;
        Effects::configureParticle(effect, Effects::EffectDomain::ThreeD);
        spawnEffectObject(*world, ctx, "Particle 3D", effect, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Create 2D Particle")) {
        Effects::EffectComponent effect;
        Effects::configureParticle(effect, Effects::EffectDomain::TwoD);
        spawnEffectObject(*world, ctx, "Particle 2D", effect, true);
    }
    ImGui::TextDisabled("caffeine.effects.particle(entity, domain)  domain 1 = 2D, 2 = 3D");
    ImGui::TextDisabled("caffeine.effects.particleParams(entity, rate, life, max, startSize, endSize, gravityY)");

    std::vector<ECS::Entity> particles;
    ECS::ComponentQuery query;
    query.with<Effects::EffectComponent>();
    world->forEach<Effects::EffectComponent>(query, [&](ECS::Entity entity, Effects::EffectComponent& effect) {
        if (static_cast<Effects::EffectKind>(effect.kind) == Effects::EffectKind::Particles) {
            particles.push_back(entity);
        }
    });

    ImGui::Separator();
    Effects::EffectComponent* selected = nullptr;
    ECS::Entity selectedEntity = ECS::Entity::INVALID;
    for (ECS::Entity entity : particles) {
        Effects::EffectComponent* effect = world->get<Effects::EffectComponent>(entity);
        if (!effect) continue;
        char row[160];
        std::snprintf(row, sizeof(row), "%s  %s##particle_%u", particleLabel(*effect),
                      getEntityName(*world, entity), entity.id());
        const bool current = ctx.selectedEntity.isValid() && ctx.selectedEntity.id() == entity.id();
        if (ImGui::Selectable(row, current)) ctx.selectEntity(entity);
        if (ctx.selectedEntity.isValid() && ctx.selectedEntity.id() == entity.id()) {
            selected = effect;
            selectedEntity = entity;
        }
    }
    if (particles.empty()) ImGui::TextDisabled("No particle objects in this scene.");

    if (!selected || !selectedEntity.isValid()) {
        ImGui::End();
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted(getEntityName(*world, selectedEntity));
    bool enabled = selected->enabled != 0;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        selected->enabled = enabled ? 1 : 0;
        ctx.isDirty = true;
    }
    int domain = static_cast<int>(selected->domain);
    const char* domains[] = {"2D and 3D", "2D", "3D"};
    if (ImGui::Combo("Domain", &domain, domains, 3)) {
        selected->domain = static_cast<u8>(domain);
        ctx.isDirty = true;
    }
    const char* spaces[] = {"World", "Camera"};
    int space = static_cast<int>(selected->space);
    if (ImGui::Combo("Space", &space, spaces, 2)) {
        selected->space = static_cast<u8>(space);
        ctx.isDirty = true;
    }
    if (dragFloat("Rate", selected->rate, 0.5f, 0.0f, 400.0f)) ctx.isDirty = true;
    if (dragFloat("Lifetime", selected->lifetime, 0.02f, 0.05f, 20.0f)) ctx.isDirty = true;
    int cap = selected->maxParticles;
    if (ImGui::DragInt("Max", &cap, 1.0f, 1, 2048)) {
        selected->maxParticles = cap;
        ctx.isDirty = true;
    }
    if (dragFloat("Start Size", selected->startSize, 0.01f, 0.0f, 8.0f)) ctx.isDirty = true;
    if (dragFloat("End Size", selected->endSize, 0.01f, 0.0f, 8.0f)) ctx.isDirty = true;
    if (ImGui::ColorEdit4("Start Color", &selected->startColor.x)) ctx.isDirty = true;
    if (ImGui::ColorEdit4("End Color", &selected->endColor.x)) ctx.isDirty = true;
    if (ImGui::DragFloat3("Velocity Min", &selected->velocityMin.x, 0.02f)) ctx.isDirty = true;
    if (ImGui::DragFloat3("Velocity Max", &selected->velocityMax.x, 0.02f)) ctx.isDirty = true;
    if (ImGui::DragFloat3("Gravity", &selected->gravity.x, 0.05f)) ctx.isDirty = true;
    if (dragFloat("Drag", selected->drag, 0.01f, 0.0f, 8.0f)) ctx.isDirty = true;
    char path[260] = {};
    std::snprintf(path, sizeof(path), "%s", selected->materialPath);
    if (ImGui::InputText("Material", path, sizeof(path))) {
        std::snprintf(selected->materialPath, sizeof(selected->materialPath), "%s", path);
        ctx.isDirty = true;
    }
    if (ImGui::Button("Delete Particle")) {
        ctx.beginUndo(EditorCommand::RemoveEntity, selectedEntity.id(), *world);
        world->destroy(selectedEntity);
        ctx.isDirty = true;
        ctx.endUndo(*world);
    }
    ImGui::End();
#else
    (void)ctx;
#endif
}

}  // namespace Caffeine::Editor
