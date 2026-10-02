#include "editor/EffectEditorPanel.hpp"

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

const char* fogStyleName(Effects::EffectFogStyle style) {
    switch (style) {
        case Effects::EffectFogStyle::Dust: return "Dust";
        case Effects::EffectFogStyle::Mist: return "Mist";
        case Effects::EffectFogStyle::Smoke: return "Smoke";
        case Effects::EffectFogStyle::Fog: break;
    }
    return "Fog";
}

const char* effectLabel(const Effects::EffectComponent& effect) {
    switch (static_cast<Effects::EffectKind>(effect.kind)) {
        case Effects::EffectKind::VolumetricLight: {
            static char label[64];
            std::snprintf(label, sizeof(label), "Volumetric %s",
                          Effects::volumetricShapeName(Effects::volumetricShapeOf(effect)));
            return label;
        }
        case Effects::EffectKind::ReflectiveSurface: return "Reflective Surface";
        case Effects::EffectKind::MaterialShade: return "Material Shade";
        case Effects::EffectKind::Fog: {
            const bool twoD = static_cast<Effects::EffectDomain>(effect.domain) == Effects::EffectDomain::TwoD;
            static char label[64];
            std::snprintf(label, sizeof(label), "%s %s",
                          twoD ? "2D" : "3D",
                          fogStyleName(static_cast<Effects::EffectFogStyle>(effect.pad0 > 3 ? 0 : effect.pad0)));
            return label;
        }
        case Effects::EffectKind::Particles: break;
    }
    return "Effect";
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

void EffectEditorPanel::onImGuiRender(EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;
    ImGui::Begin("Effects", &m_open);
    ECS::World* world = ctx.activeWorld;
    if (!world) {
        ImGui::TextDisabled("Open a scene to edit effects.");
        ImGui::End();
        return;
    }

    const char* volumeShapes[] = {"Sphere", "Cone", "Box", "Cylinder", "Window"};
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("Format##vol_create", &m_volumeShape, volumeShapes, 5);
    ImGui::SameLine();
    if (ImGui::Button("Create Volumetric")) {
        Effects::EffectComponent effect;
        const auto shape = static_cast<Effects::VolumetricShape>(m_volumeShape);
        Effects::configureVolumetricLight(effect, shape);
        char name[64];
        std::snprintf(name, sizeof(name), "Volumetric %s", volumeShapes[m_volumeShape]);
        spawnEffectObject(*world, ctx, name, effect, false);
    }
    ImGui::SameLine();
    const char* styles[] = {"Fog", "Dust", "Mist", "Smoke"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("Style##effect_fog_style", &m_fogStyle, styles, 4);
    if (ImGui::Button("Create 3D Fog")) {
        Effects::EffectComponent effect;
        const auto style = static_cast<Effects::EffectFogStyle>(m_fogStyle);
        Effects::configureFog(effect, Effects::EffectDomain::ThreeD, style);
        char name[64];
        std::snprintf(name, sizeof(name), "3D %s", styles[m_fogStyle]);
        spawnEffectObject(*world, ctx, name, effect, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Create 2D Fog")) {
        Effects::EffectComponent effect;
        const auto style = static_cast<Effects::EffectFogStyle>(m_fogStyle);
        Effects::configureFog(effect, Effects::EffectDomain::TwoD, style);
        char name[64];
        std::snprintf(name, sizeof(name), "2D %s", styles[m_fogStyle]);
        spawnEffectObject(*world, ctx, name, effect, true);
    }
    if (ImGui::Button("Reflective Surface")) {
        Effects::EffectComponent effect;
        effect.kind = static_cast<u8>(Effects::EffectKind::ReflectiveSurface);
        effect.reflection = 0.85f;
        effect.roughness = 0.12f;
        effect.metallic = 0.85f;
        effect.domain = static_cast<u8>(Effects::EffectDomain::ThreeD);
        spawnEffectObject(*world, ctx, "Reflective Surface", effect, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Material Shade")) {
        Effects::EffectComponent effect;
        effect.kind = static_cast<u8>(Effects::EffectKind::MaterialShade);
        effect.domain = static_cast<u8>(Effects::EffectDomain::Both);
        spawnEffectObject(*world, ctx, "Material Shade", effect, false);
    }
    ImGui::TextDisabled("caffeine.effects.volumetricShape(entity, shape, columns, rows)");
    ImGui::TextDisabled("shape 0 sphere, 1 cone, 2 box, 3 cylinder, 4 window. Shafts follow -Z.");
    ImGui::TextDisabled("caffeine.effects.fog(entity, domain, style)  style 0 fog, 1 dust, 2 mist, 3 smoke");

    std::vector<ECS::Entity> effects;
    ECS::ComponentQuery query;
    query.with<Effects::EffectComponent>();
    world->forEach<Effects::EffectComponent>(query, [&](ECS::Entity entity, Effects::EffectComponent& effect) {
        if (static_cast<Effects::EffectKind>(effect.kind) != Effects::EffectKind::Particles) {
            effects.push_back(entity);
        }
    });

    ImGui::Separator();
    Effects::EffectComponent* selected = nullptr;
    ECS::Entity selectedEntity = ECS::Entity::INVALID;
    for (ECS::Entity entity : effects) {
        Effects::EffectComponent* effect = world->get<Effects::EffectComponent>(entity);
        if (!effect) continue;
        char row[160];
        std::snprintf(row, sizeof(row), "%s  %s##effect_%u", effectLabel(*effect),
                      getEntityName(*world, entity), entity.id());
        const bool current = ctx.selectedEntity.isValid() && ctx.selectedEntity.id() == entity.id();
        if (ImGui::Selectable(row, current)) ctx.selectEntity(entity);
        if (ctx.selectedEntity.isValid() && ctx.selectedEntity.id() == entity.id()) {
            selected = effect;
            selectedEntity = entity;
        }
    }
    if (effects.empty()) ImGui::TextDisabled("No effect objects in this scene.");
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
    const char* spaces[] = {"World", "Camera"};
    int space = static_cast<int>(selected->space);
    if (ImGui::Combo("Space", &space, spaces, 2)) {
        selected->space = static_cast<u8>(space);
        ctx.isDirty = true;
    }
    const char* qualities[] = {"Performance", "Balanced", "Quality"};
    int quality = static_cast<int>(selected->quality);
    if (ImGui::Combo("Quality", &quality, qualities, 3)) {
        selected->quality = static_cast<u8>(quality);
        ctx.isDirty = true;
    }

    const Effects::EffectKind kind = static_cast<Effects::EffectKind>(selected->kind);
    if (kind == Effects::EffectKind::VolumetricLight) {
        int shape = static_cast<int>(Effects::volumetricShapeOf(*selected));
        if (ImGui::Combo("Format", &shape, volumeShapes, 5)) {
            const Vec3 color = selected->lightColor;
            Effects::configureVolumetricLight(*selected, static_cast<Effects::VolumetricShape>(shape));
            selected->lightColor = color;
            ctx.isDirty = true;
        }
        const Effects::VolumetricShape volume = Effects::volumetricShapeOf(*selected);
        if (volume == Effects::VolumetricShape::Sphere) {
            if (dragFloat("Radius", selected->radius, 0.05f, 0.1f, 40.0f)) ctx.isDirty = true;
        } else {
            if (dragFloat("Length", selected->radius, 0.05f, 0.2f, 40.0f)) ctx.isDirty = true;
            if (volume == Effects::VolumetricShape::Cone) {
                if (dragFloat("Near", selected->startSize, 0.01f, 0.05f, 8.0f)) ctx.isDirty = true;
                if (dragFloat("Far", selected->endSize, 0.01f, 0.05f, 12.0f)) ctx.isDirty = true;
            } else if (volume == Effects::VolumetricShape::Cylinder) {
                if (dragFloat("Diameter", selected->startSize, 0.01f, 0.05f, 8.0f)) {
                    selected->endSize = selected->startSize;
                    ctx.isDirty = true;
                }
            } else {
                if (dragFloat("Width", selected->startSize, 0.01f, 0.05f, 12.0f)) ctx.isDirty = true;
                if (dragFloat("Height", selected->endSize, 0.01f, 0.05f, 12.0f)) ctx.isDirty = true;
            }
            if (volume == Effects::VolumetricShape::Window) {
                u32 columns = 1;
                u32 rows = 1;
                Effects::volumetricGridOf(*selected, columns, rows);
                int columnCount = static_cast<int>(columns);
                int rowCount = static_cast<int>(rows);
                if (ImGui::SliderInt("Columns", &columnCount, 1, 8)) {
                    Effects::setVolumetricGrid(*selected, static_cast<u32>(columnCount), rows);
                    ctx.isDirty = true;
                }
                if (ImGui::SliderInt("Rows", &rowCount, 1, 8)) {
                    Effects::setVolumetricGrid(*selected, static_cast<u32>(columnCount), static_cast<u32>(rowCount));
                    ctx.isDirty = true;
                }
                ImGui::TextDisabled("Aim -Z from the glass toward the floor. Dust in the room makes the shafts read.");
            }
        }
        if (dragFloat("Density", selected->density, 0.005f, 0.0f, 2.0f)) ctx.isDirty = true;
        if (ImGui::SliderFloat("Anisotropy", &selected->anisotropy, 0.0f, 0.9f)) ctx.isDirty = true;
        if (dragFloat("Intensity", selected->intensity, 0.05f, 0.0f, 16.0f)) ctx.isDirty = true;
        if (ImGui::ColorEdit3("Light", &selected->lightColor.x)) ctx.isDirty = true;
    } else if (kind == Effects::EffectKind::Fog) {
        int style = static_cast<int>(selected->pad0 > 3 ? 0 : selected->pad0);
        if (ImGui::Combo("Look", &style, styles, 4)) {
            const auto domain = static_cast<Effects::EffectDomain>(selected->domain);
            Effects::configureFog(*selected, domain, static_cast<Effects::EffectFogStyle>(style));
            ctx.isDirty = true;
        }
        int domain = static_cast<int>(selected->domain);
        const char* domains[] = {"2D and 3D", "2D", "3D"};
        if (ImGui::Combo("Domain", &domain, domains, 3)) {
            selected->domain = static_cast<u8>(domain);
            ctx.isDirty = true;
        }
        if (dragFloat("Radius", selected->radius, 0.05f, 0.1f, 40.0f)) ctx.isDirty = true;
        if (dragFloat("Density", selected->density, 0.005f, 0.0f, 2.0f)) ctx.isDirty = true;
        if (dragFloat("Intensity", selected->intensity, 0.02f, 0.0f, 4.0f)) ctx.isDirty = true;
        if (ImGui::ColorEdit3("Color", &selected->lightColor.x)) ctx.isDirty = true;
    } else if (kind == Effects::EffectKind::ReflectiveSurface) {
        if (ImGui::SliderFloat("Reflection", &selected->reflection, 0.0f, 1.0f)) ctx.isDirty = true;
        if (ImGui::SliderFloat("Roughness", &selected->roughness, 0.02f, 1.0f)) ctx.isDirty = true;
        if (ImGui::SliderFloat("Metallic", &selected->metallic, 0.0f, 1.0f)) ctx.isDirty = true;
    } else if (kind == Effects::EffectKind::MaterialShade) {
        if (ImGui::ColorEdit3("Tint", &selected->tint.x)) ctx.isDirty = true;
        if (dragFloat("Emission", selected->emissionBoost, 0.05f, 0.0f, 8.0f)) ctx.isDirty = true;
        if (dragFloat("Roughness Bias", selected->roughnessBias, 0.01f, -1.0f, 1.0f)) ctx.isDirty = true;
    }

    if (ImGui::Button("Delete Effect")) {
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
