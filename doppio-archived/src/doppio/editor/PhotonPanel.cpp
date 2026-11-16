#include "editor/PhotonPanel.hpp"

#include "caffeine/photon/PhotonBake.hpp"
#include "caffeine/ui/Widgets.hpp"
#include "debug/LogSystem.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "scene/EditorContext.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
namespace {

const char* const kPresetNames[] = {"Off", "Eco", "Low", "Balanced", "High"};
constexpr int kPresetCount = 5;
// Tiers exposed inside "Radiance cascades"; Off is the component's `enabled` flag.
const char* const kQualityNames[] = {"Eco", "Low", "Balanced", "High"};
constexpr ECS::PhotonQuality kQualityValues[] = {
    ECS::PhotonQuality::Eco,
    ECS::PhotonQuality::Low,
    ECS::PhotonQuality::Balanced,
    ECS::PhotonQuality::High,
};
constexpr int kQualityCount = 4;
const char* const kDebugNames[] = {"Lit scene", "GI only", "Fallback only"};
const char* const kResolutionNames[] = {"256", "512", "1024"};

int qualityIndex(ECS::PhotonQuality quality) {
    for (int i = 0; i < kQualityCount; ++i) {
        if (kQualityValues[i] == quality) return i;
    }
    return 2;  // Balanced for Off / unknown values.
}

const char* classify(const ECS::PhotonGIComponent& gi) {
    if (!gi.enabled || gi.quality == ECS::PhotonQuality::Off) return "Off";
    return ECS::photonQualityName(gi.quality);
}

}  // namespace

void PhotonPanel::applyPreset(ECS::PhotonGIComponent& gi, int preset) const {
    switch (preset) {
        case 0:
            gi.enabled = false;
            gi.quality = ECS::PhotonQuality::Off;
            break;
        case 1:  // Eco: lowest cost, target low-end / handheld.
            gi.enabled = true;
            gi.quality = ECS::PhotonQuality::Eco;
            gi.cascades = 4;
            gi.intensity = 0.7f;
            gi.radius = 8.0f;
            gi.thickness = 0.12f;
            gi.skyIntensity = 0.50f;
            gi.ambientIntensity = 0.16f;
            gi.s0 = 16;
            gi.computeSteps = 8;
            break;
        case 2:  // Low: integrated GPUs.
            gi.enabled = true;
            gi.quality = ECS::PhotonQuality::Low;
            gi.cascades = 4;
            gi.intensity = 0.9f;
            gi.radius = 12.0f;
            gi.thickness = 0.10f;
            gi.skyIntensity = 0.60f;
            gi.ambientIntensity = 0.18f;
            gi.s0 = 8;
            gi.computeSteps = 12;
            break;
        case 3:  // Balanced: desktop / console default.
            gi.enabled = true;
            gi.quality = ECS::PhotonQuality::Balanced;
            gi.cascades = 5;
            gi.intensity = 1.1f;
            gi.radius = 14.0f;
            gi.thickness = 0.08f;
            gi.skyIntensity = 0.65f;
            gi.ambientIntensity = 0.20f;
            gi.s0 = 4;
            gi.computeSteps = 16;
            break;
        case 4:  // High: maximum quality of the near field.
            gi.enabled = true;
            gi.quality = ECS::PhotonQuality::High;
            gi.cascades = 6;
            gi.intensity = 1.25f;
            gi.radius = 22.0f;
            gi.thickness = 0.06f;
            gi.skyIntensity = 0.70f;
            gi.ambientIntensity = 0.22f;
            gi.s0 = 2;
            gi.computeSteps = 24;
            break;
        default:
            break;
    }
    gi.tintR = 0.82f;
    gi.tintG = 0.82f;
    gi.tintB = 0.82f;
}

ECS::Entity PhotonPanel::ensureTarget(ECS::World& world, EditorContext& ctx, bool create) {
    if (ctx.selectedEntity.isValid() && world.has<ECS::PhotonGIComponent>(ctx.selectedEntity)) {
        return ctx.selectedEntity;
    }

    ECS::ComponentQuery query;
    query.with<ECS::PhotonGIComponent>();
    ECS::Entity found = ECS::Entity::INVALID;
    world.forEach<ECS::PhotonGIComponent>(query, [&](ECS::Entity entity, ECS::PhotonGIComponent&) {
        if (!found.isValid()) found = entity;
    });
    if (found.isValid()) return found;

    if (ctx.selectedEntity.isValid()) {
        auto& gi = world.add<ECS::PhotonGIComponent>(ctx.selectedEntity);
        applyPreset(gi, 2);
        return ctx.selectedEntity;
    }
    if (!create) return ECS::Entity::INVALID;

    ECS::Entity entity = world.create("Photon GI");
    auto& gi = world.add<ECS::PhotonGIComponent>(entity);
    applyPreset(gi, 2);
    return entity;
}

void PhotonPanel::render(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;

    using namespace Caffeine::UIKit;
    if (UIKit::BeginPanel("Photon", &m_open)) {
        PanelHeader("Photon", "Native global illumination", "bluesky");

        ECS::Entity target = ensureTarget(world, ctx, false);
        ECS::PhotonGIComponent* gi = target.isValid()
                                         ? world.get<ECS::PhotonGIComponent>(target)
                                         : nullptr;
        if (!gi) {
            EmptyState("Photon is not enabled",
                       "Create the GI component on the selected entity or a dedicated entity.",
                       "bluesky");
            if (PrimaryButton("Create Photon GI")) {
                target = ensureTarget(world, ctx, true);
                if (target.isValid()) {
                    ctx.selectEntity(target);
                    ctx.markDirty();
                }
            }
            UIKit::EndPanel();
            return;
        }

        if (Toggle("Enabled", &gi->enabled)) ctx.markDirty();
        SameLine();
        TextMuted(classify(*gi));

        if (Section("Presets", true)) {
            if (BeginPropertyGrid("photon_presets")) {
                if (ComboProperty("Quality preset", &m_preset, kPresetNames, kPresetCount)) {
                    applyPreset(*gi, m_preset);
                    ctx.markDirty();
                }
                EndPropertyGrid();
            }
            if (ToolbarButton("Apply", "Apply the selected preset")) {
                applyPreset(*gi, m_preset);
                ctx.markDirty();
            }
            SameLine();
            if (ToolbarButton("Reset", "Reset to Balanced")) {
                m_preset = 3;
                applyPreset(*gi, m_preset);
                ctx.markDirty();
            }
        }

        if (Section("Implementation")) {
            bool changed = false;
            if (BeginPropertyGrid("photon_pipeline")) {
                static const char* const kPathNames[] = {"Screen fallback",
                                                         "Compute C0 (experimental)"};
                int path = gi->path == ECS::PhotonPath::ComputeC0 ? 1 : 0;
                if (ComboProperty("Path", &path, kPathNames, 2)) {
                    gi->path = path == 1 ? ECS::PhotonPath::ComputeC0
                                         : ECS::PhotonPath::ScreenFallback;
                    changed = true;
                }
                int s0 = static_cast<int>(gi->s0);
                if (SliderIntProperty("Probe spacing s0 (px)", &s0, 2, 32)) {
                    gi->s0 = static_cast<u8>(s0);
                    changed = true;
                }
                int steps = static_cast<int>(gi->computeSteps);
                if (SliderIntProperty("Compute trace steps", &steps, 1, 32)) {
                    gi->computeSteps = static_cast<u8>(steps);
                    changed = true;
                }
                EndPropertyGrid();
            }
            TextMuted("Compute: K cascades of 16·4^k world-aligned directions with fused "
                      "trace+merge; C0 resolves SH-L1. Far field: 4-level voxel clipmap + "
                      "probe field (screened fill).");
            if (changed) ctx.markDirty();
        }

        if (Section("Radiance cascades")) {
            bool changed = false;
            if (BeginPropertyGrid("photon_cascades")) {
                int quality = qualityIndex(gi->quality);
                if (ComboProperty("Quality", &quality, kQualityNames, kQualityCount)) {
                    gi->quality = kQualityValues[std::clamp(quality, 0, kQualityCount - 1)];
                    changed = true;
                }
                int cascades = static_cast<int>(gi->cascades);
                if (SliderIntProperty("Cascades", &cascades, 1, 6)) {
                    gi->cascades = static_cast<u8>(cascades);
                    changed = true;
                }
                changed |= SliderFloatProperty("Intensity", &gi->intensity, 0.0f, 8.0f);
                changed |= DragFloatProperty("Radius", &gi->radius, 0.1f, 0.1f, 500.0f);
                changed |= SliderFloatProperty("Thickness", &gi->thickness, 0.0f, 0.5f);
                EndPropertyGrid();
            }
            if (changed) ctx.markDirty();
        }

        if (Section("Far field fallback")) {
            bool changed = false;
            if (BeginPropertyGrid("photon_far")) {
                changed |= SliderFloatProperty("Sky intensity", &gi->skyIntensity, 0.0f, 4.0f);
                changed |= SliderFloatProperty("Ambient", &gi->ambientIntensity, 0.0f, 2.0f);
                changed |= SliderFloatProperty("Tint R", &gi->tintR, 0.0f, 2.0f);
                changed |= SliderFloatProperty("Tint G", &gi->tintG, 0.0f, 2.0f);
                changed |= SliderFloatProperty("Tint B", &gi->tintB, 0.0f, 2.0f);
                int debug = static_cast<int>(gi->debugView);
                if (ComboProperty("Debug", &debug, kDebugNames, 3)) {
                    gi->debugView = static_cast<u8>(debug);
                    changed = true;
                }
                EndPropertyGrid();
            }
            TextMuted("Tint is only used where the G-buffer has no surface data.");
            if (changed) ctx.markDirty();
        }

        if (Section("Baked AO (material only)", true)) {
            TextMuted("Precomputed static ambient occlusion written to the material ORM map. "
                      "This is not transport/GI and runs without RTX.");
            if (BeginPropertyGrid("photon_ao")) {
                ComboProperty("Resolution", &m_aoResolution, kResolutionNames, 3);
                SliderFloatProperty("Rays", &m_aoRays, 1.0f, 64.0f, "%.0f");
                DragFloatProperty("Max distance", &m_aoMaxDistance, 0.1f, 0.05f, 50.0f);
                DragFloatProperty("Bias", &m_aoBias, 0.001f, 0.0f, 0.2f);
                CheckboxProperty("Use GPU compute", &m_useGpu);
                EndPropertyGrid();
            }
            if (!m_gpuAvailable) TextMuted("GPU compute unavailable: CPU fallback will be used.");
            const bool hasSelectedMesh =
                ctx.selectedEntity.isValid() &&
                world.has<ECS::MeshFilterComponent>(ctx.selectedEntity);
            if (!hasSelectedMesh) {
                TextMuted("Select a mesh entity in the Hierarchy before baking.");
            }
            if (ToolbarButton("Bake selected mesh",
                              "Queue an AO bake for the selected mesh")) {
                if (!hasSelectedMesh) {
                    m_status = "Select a mesh entity in the Hierarchy before baking.";
                } else {
                    queueBakeSelected();
                }
            }
            SameLine();
            if (ToolbarButton("Bake scene", "Queue an AO bake for every static mesh")) {
                queueBakeScene();
            }
            TextMuted("Baking runs between frames; the editor remains responsive.");
            if (!m_status.empty()) TextMuted(m_status.c_str());
        }

        SeparatorSpaced();
        TextMuted("Screen fallback: depth gather. Compute: K cascades + fused merge + SH-L1.");
        TextMuted("Far field: 4-level clipmap + probe field, one bounce, screened fill.");
        TextMutedF("Component entity: %u", target.id());
    }
    UIKit::EndPanel();
#endif
}

void PhotonPanel::processPendingBake(ECS::World& world, EditorContext& ctx) {
    if (!m_bakeSelectedPending && !m_bakeScenePending) return;

    Photon::BakeSettings settings;
    settings.resolution = m_aoResolution == 0 ? 256u : m_aoResolution == 2 ? 1024u : 512u;
    settings.rays = static_cast<u32>(std::clamp(static_cast<int>(m_aoRays), 1, 64));
    settings.maxDistance = std::max(m_aoMaxDistance, 0.01f);
    settings.bias = std::max(m_aoBias, 0.0f);
    settings.useGpu = m_useGpu && m_gpuAvailable;
    settings.outputDirectory =
        (std::filesystem::path(m_processedDirectory) / "ao").generic_string();

    std::filesystem::path root = ctx.projectRootPath;
    if (root.empty() && !ctx.currentScenePath.empty()) {
        root = std::filesystem::path(ctx.currentScenePath).parent_path().parent_path();
    }
    if (root.empty()) root = std::filesystem::current_path();

    void* device = settings.useGpu ? m_renderDevice : nullptr;
    char buffer[320];

    if (m_bakeSelectedPending) {
        m_bakeSelectedPending = false;
        if (!ctx.selectedEntity.isValid()) {
            m_status = "Select a mesh entity before baking.";
        } else {
            const auto result = Photon::bakeAmbientOcclusion(
                world, ctx.selectedEntity, settings, root.string(), device);
            if (result.ok) {
                std::snprintf(buffer, sizeof(buffer),
                              "Baked AO (%s): %u/%u texels. ORM: %s",
                              result.usedGpu ? "GPU" : "CPU", result.coveredTexels,
                              settings.resolution * settings.resolution, result.ormPath.c_str());
                m_status = buffer;
                ctx.markDirty();
                ctx.assetBrowserDirty = true;
            } else {
                m_status = "Bake failed: " + result.error;
                Debug::LogSystem::instance().log(Debug::LogLevel::Error, "Photon", "%s",
                                                 m_status.c_str());
            }
        }
    }

    if (m_bakeScenePending) {
        m_bakeScenePending = false;
        std::vector<ECS::Entity> targets;
        ECS::ComponentQuery query;
        query.with<ECS::MeshFilterComponent>();
        world.forEach<ECS::MeshFilterComponent>(
            query, [&](ECS::Entity entity, ECS::MeshFilterComponent&) { targets.push_back(entity); });

        u32 baked = 0;
        u32 failed = 0;
        bool anyGpu = false;
        std::string firstError;
        for (ECS::Entity entity : targets) {
            const auto result = Photon::bakeAmbientOcclusion(
                world, entity, settings, root.string(), device);
            if (result.ok) {
                ++baked;
                anyGpu = anyGpu || result.usedGpu;
            } else {
                ++failed;
                const std::string message =
                    "entity " + std::to_string(entity.id()) + ": " + result.error;
                Debug::LogSystem::instance().log(Debug::LogLevel::Error, "Photon", "%s",
                                                 message.c_str());
                if (firstError.empty()) firstError = message;
            }
        }
        std::snprintf(buffer, sizeof(buffer), "Baked %u mesh(es), %u failed (%s).", baked, failed,
                      anyGpu ? "GPU" : "CPU");
        m_status = buffer;
        if (!firstError.empty()) m_status += " First error: " + firstError;
        ctx.markDirty();
        ctx.assetBrowserDirty = true;
    }
}

}  // namespace Caffeine::Editor
