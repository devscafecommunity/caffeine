#include "caffeine/ui/UI.hpp"
#include "editor/EffectEditorPanel.hpp"

#include "editor/EffectObjectFactory.hpp"
#include "effects/EffectSystem.hpp"
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
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_open) return;
    ECS::World* world = ctx.activeWorld;
    Panel root("Effects", "Volumetric lights, fog and surface effects", "effects", &m_open);
    UIContext ui;
    if (!world) {
        root.emplace<Label>("Open a scene to edit effects.", TextTone::Muted);
        ui.draw(root);
        return;
    }

    const char* volumeShapes[] = {"Sphere", "Cone", "Box", "Cylinder", "Window"};
    const char* fogStyles[] = {"Fog", "Dust", "Mist", "Smoke"};
    const char* domains[] = {"2D and 3D", "2D", "3D"};
    const char* qualities[] = {"Performance", "Balanced", "Quality"};

    auto& create = root.emplace<Card>("create");
    create.emplace<Label>("Create", TextTone::Accent).setHeading(true);
    auto& createGrid = create.emplace<PropertyGrid>("effect_create");
    createGrid.typedRow<ChoiceProperty>(
        "Volume format", m_volumeShape,
        std::vector<std::string>{volumeShapes[0], volumeShapes[1], volumeShapes[2],
                                 volumeShapes[3], volumeShapes[4]});
    createGrid.typedRow<ChoiceProperty>(
        "Fog style", m_fogStyle,
        std::vector<std::string>{fogStyles[0], fogStyles[1], fogStyles[2], fogStyles[3]});
    auto& createRow = create.emplace<Row>("effect_create_actions");
    createRow.emplace<Button>("Create Volumetric", ButtonVariant::Primary)
        .setOnClick([&] {
            Effects::EffectComponent effect;
            const auto shape = static_cast<Effects::VolumetricShape>(m_volumeShape);
            Effects::configureVolumetricLight(effect, shape);
            char name[64];
            std::snprintf(name, sizeof(name), "Volumetric %s", volumeShapes[m_volumeShape]);
            spawnEffectObject(*world, ctx, name, effect, false);
        });
    createRow.emplace<Button>("Create 3D Fog")
        .setOnClick([&] {
            Effects::EffectComponent effect;
            const auto style = static_cast<Effects::EffectFogStyle>(m_fogStyle);
            Effects::configureFog(effect, Effects::EffectDomain::ThreeD, style);
            char name[64];
            std::snprintf(name, sizeof(name), "3D %s", fogStyles[m_fogStyle]);
            spawnEffectObject(*world, ctx, name, effect, false);
        });
    createRow.emplace<Button>("Create 2D Fog")
        .setOnClick([&] {
            Effects::EffectComponent effect;
            const auto style = static_cast<Effects::EffectFogStyle>(m_fogStyle);
            Effects::configureFog(effect, Effects::EffectDomain::TwoD, style);
            char name[64];
            std::snprintf(name, sizeof(name), "2D %s", fogStyles[m_fogStyle]);
            spawnEffectObject(*world, ctx, name, effect, true);
        });
    createRow.emplace<Button>("Reflective Surface")
        .setOnClick([&] {
            Effects::EffectComponent effect;
            effect.kind = static_cast<u8>(Effects::EffectKind::ReflectiveSurface);
            effect.reflection = 0.85f;
            effect.roughness = 0.12f;
            effect.metallic = 0.85f;
            effect.domain = static_cast<u8>(Effects::EffectDomain::ThreeD);
            spawnEffectObject(*world, ctx, "Reflective Surface", effect, false);
        });
    createRow.emplace<Button>("Material Shade")
        .setOnClick([&] {
            Effects::EffectComponent effect;
            effect.kind = static_cast<u8>(Effects::EffectKind::MaterialShade);
            effect.domain = static_cast<u8>(Effects::EffectDomain::Both);
            spawnEffectObject(*world, ctx, "Material Shade", effect, false);
        });
    create.emplace<Elements::Paragraph>(
        "caffeine.effects.volumetricShape(entity, shape, columns, rows)\n"
        "shape 0 sphere, 1 cone, 2 box, 3 cylinder, 4 window. The volume is the mesh.\n"
        "caffeine.effects.fog(entity, domain, style)  style 0 fog, 1 dust, 2 mist, 3 smoke");

    std::vector<ECS::Entity> effects;
    ECS::ComponentQuery query;
    query.with<Effects::EffectComponent>();
    world->forEach<Effects::EffectComponent>(
        query, [&](ECS::Entity entity, Effects::EffectComponent& effect) {
            if (static_cast<Effects::EffectKind>(effect.kind) !=
                Effects::EffectKind::Particles) {
                effects.push_back(entity);
            }
        });

    auto& listCard = root.emplace<Card>("effects");
    listCard.emplace<Label>("Effects", TextTone::Accent).setHeading(true);
    auto& list = listCard.emplace<ListBox>("effect_list");
    list.setHeight(150.0f);
    for (ECS::Entity entity : effects) {
        Effects::EffectComponent* effect = world->get<Effects::EffectComponent>(entity);
        if (!effect) continue;
        const bool current =
            ctx.selectedEntity.isValid() && ctx.selectedEntity.id() == entity.id();
        std::string label = std::string(effectLabel(*effect)) + "  " +
                            getEntityName(*world, entity);
        auto& item = list.emplace<ListItem>(label, current);
        item.setId("effect_" + std::to_string(entity.id()));
        item.setOnClick([&ctx, entity] { ctx.selectEntity(entity); });
    }
    if (effects.empty()) {
        listCard.emplace<Label>("No effect objects in this scene.", TextTone::Muted);
    }

    Effects::EffectComponent* selected = nullptr;
    ECS::Entity selectedEntity = ECS::Entity::INVALID;
    if (ctx.selectedEntity.isValid()) {
        Effects::EffectComponent* effect =
            world->get<Effects::EffectComponent>(ctx.selectedEntity);
        if (effect && static_cast<Effects::EffectKind>(effect->kind) !=
                          Effects::EffectKind::Particles) {
            selected = effect;
            selectedEntity = ctx.selectedEntity;
        }
    }

    if (!selected || !selectedEntity.isValid()) {
        ui.draw(root);
        return;
    }

    const Effects::EffectKind kind = static_cast<Effects::EffectKind>(selected->kind);
    if (kind == Effects::EffectKind::VolumetricLight) {
        Effects::ensureVolumetricMesh(*world, selectedEntity, *selected);
        Effects::pullVolumetricSizeFromMesh(*world, selectedEntity, *selected);
    }
    const Effects::EffectComponent before = *selected;

    bool enabled = selected->enabled != 0;
    int space = static_cast<int>(selected->space);
    int quality = static_cast<int>(selected->quality);
    int shape = static_cast<int>(Effects::volumetricShapeOf(*selected));
    int fogStyle = static_cast<int>(selected->pad0 > 3 ? 0 : selected->pad0);
    int fogDomain = static_cast<int>(selected->domain);
    float coneRadius = selected->endSize * 0.5f;
    bool deleted = false;

    auto& card = root.emplace<Card>("selected_effect");
    card.emplace<Label>(getEntityName(*world, selectedEntity), TextTone::Accent)
        .setHeading(true);
    auto& grid = card.emplace<PropertyGrid>("effect_properties");
    grid.typedRow<BoolProperty>("Enabled", enabled)
        .setOnChange([&](bool value) {
            selected->enabled = value ? 1 : 0;
            ctx.markDirty();
        });
    grid.typedRow<ChoiceProperty>(
        "Space", space, std::vector<std::string>{"World", "Camera"})
        .setOnChange([&](int value) {
            selected->space = static_cast<u8>(value);
            ctx.markDirty();
        });
    grid.typedRow<ChoiceProperty>(
        "Quality", quality,
        std::vector<std::string>{qualities[0], qualities[1], qualities[2]})
        .setOnChange([&](int value) {
            selected->quality = static_cast<u8>(value);
            ctx.markDirty();
        });

    if (kind == Effects::EffectKind::VolumetricLight) {
        auto pushSize = [&] {
            Effects::pushVolumetricSizeToMesh(*world, selectedEntity, *selected);
        };
        grid.typedRow<ChoiceProperty>(
            "Format", shape,
            std::vector<std::string>{volumeShapes[0], volumeShapes[1], volumeShapes[2],
                                     volumeShapes[3], volumeShapes[4]})
            .setOnChange([&](int value) {
                const Vec3 color = selected->lightColor;
                Effects::configureVolumetricLight(
                    *selected, static_cast<Effects::VolumetricShape>(value));
                selected->lightColor = color;
                Effects::adoptVolumetricMesh(*world, selectedEntity, *selected);
                ctx.markDirty();
            });
        card.emplace<Label>("The volume is the mesh. Move, rotate and scale it.",
                            TextTone::Muted);
        const Effects::VolumetricShape volume = Effects::volumetricShapeOf(*selected);
        if (volume == Effects::VolumetricShape::Sphere) {
            grid.typedRow<FloatProperty>("Radius", selected->radius, 0.1f, 40.0f, 0.05f,
                                         "%.2f")
                .setOnChange([&](float) { pushSize(); ctx.markDirty(); });
        } else {
            grid.typedRow<FloatProperty>("Length", selected->radius, 0.2f, 40.0f, 0.05f,
                                         "%.2f")
                .setOnChange([&](float) { pushSize(); ctx.markDirty(); });
            if (volume == Effects::VolumetricShape::Cone) {
                grid.typedRow<FloatProperty>("Radius", coneRadius, 0.05f, 12.0f, 0.01f,
                                             "%.2f")
                    .setOnChange([&](float value) {
                        selected->endSize = value * 2.0f;
                        pushSize();
                        ctx.markDirty();
                    });
            } else if (volume == Effects::VolumetricShape::Cylinder) {
                grid.typedRow<FloatProperty>("Diameter", selected->startSize, 0.05f, 8.0f,
                                             0.01f, "%.2f")
                    .setOnChange([&](float value) {
                        selected->endSize = value;
                        pushSize();
                        ctx.markDirty();
                    });
            } else {
                grid.typedRow<FloatProperty>("Width", selected->startSize, 0.05f, 12.0f,
                                             0.01f, "%.2f")
                    .setOnChange([&](float) { pushSize(); ctx.markDirty(); });
                grid.typedRow<FloatProperty>("Height", selected->endSize, 0.05f, 12.0f,
                                             0.01f, "%.2f")
                    .setOnChange([&](float) { pushSize(); ctx.markDirty(); });
            }
        }
        grid.typedRow<FloatProperty>("Density", selected->density, 0.0f, 2.0f, 0.005f,
                                     "%.3f");
        grid.typedRow<FloatProperty>("Anisotropy", selected->anisotropy, 0.0f, 0.9f, 0.01f,
                                     "%.2f", true);
        grid.typedRow<FloatProperty>("Intensity", selected->intensity, 0.0f, 16.0f, 0.05f,
                                     "%.2f");
        grid.typedRow<ColorFieldProperty>("Light", &selected->lightColor.x);
    } else if (kind == Effects::EffectKind::Fog) {
        grid.typedRow<ChoiceProperty>(
            "Look", fogStyle,
            std::vector<std::string>{fogStyles[0], fogStyles[1], fogStyles[2],
                                     fogStyles[3]})
            .setOnChange([&](int value) {
                const auto domain = static_cast<Effects::EffectDomain>(selected->domain);
                Effects::configureFog(*selected, domain,
                                      static_cast<Effects::EffectFogStyle>(value));
                ctx.markDirty();
            });
        grid.typedRow<ChoiceProperty>(
            "Domain", fogDomain,
            std::vector<std::string>{domains[0], domains[1], domains[2]})
            .setOnChange([&](int value) {
                selected->domain = static_cast<u8>(value);
                ctx.markDirty();
            });
        grid.typedRow<FloatProperty>("Radius", selected->radius, 0.1f, 40.0f, 0.05f,
                                     "%.2f");
        grid.typedRow<FloatProperty>("Density", selected->density, 0.0f, 2.0f, 0.005f,
                                     "%.3f");
        grid.typedRow<FloatProperty>("Intensity", selected->intensity, 0.0f, 4.0f, 0.02f,
                                     "%.2f");
        grid.typedRow<ColorFieldProperty>("Color", &selected->lightColor.x);
    } else if (kind == Effects::EffectKind::ReflectiveSurface) {
        grid.typedRow<FloatProperty>("Reflection", selected->reflection, 0.0f, 1.0f, 0.01f,
                                     "%.2f", true);
        grid.typedRow<FloatProperty>("Roughness", selected->roughness, 0.02f, 1.0f, 0.01f,
                                     "%.2f", true);
        grid.typedRow<FloatProperty>("Metallic", selected->metallic, 0.0f, 1.0f, 0.01f,
                                     "%.2f", true);
    } else if (kind == Effects::EffectKind::MaterialShade) {
        grid.typedRow<ColorFieldProperty>("Tint", &selected->tint.x);
        grid.typedRow<FloatProperty>("Emission", selected->emissionBoost, 0.0f, 8.0f, 0.05f,
                                     "%.2f");
        grid.typedRow<FloatProperty>("Roughness Bias", selected->roughnessBias, -1.0f, 1.0f,
                                     0.01f, "%.2f");
    }

    card.emplace<Button>("Delete Effect", ButtonVariant::Danger)
        .setOnClick([&] {
            ctx.beginUndo(EditorCommand::RemoveEntity, selectedEntity.id(), *world);
            world->destroy(selectedEntity);
            ctx.markDirty();
            ctx.endUndo(*world);
            deleted = true;
        });

    ui.draw(root);
    if (deleted) return;
    if (std::memcmp(&before, selected, sizeof(Effects::EffectComponent)) != 0) {
        ctx.markDirty();
    }
#else
    (void)ctx;
#endif
}

}  // namespace Caffeine::Editor
