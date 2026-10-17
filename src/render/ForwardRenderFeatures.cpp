#include "render/ForwardRenderFeatures.hpp"

#include "ecs/ComponentQuery.hpp"

namespace Caffeine::Render {

RenderFeatureSettings budgetSafeForwardDefaults() {
    return RenderFeatureSettings{};
}

void applyForwardRenderComponent(const ECS::ForwardRenderFeaturesComponent& source,
                                 RenderFeatureSettings& target) {
    if (!source.enabled) return;

    target.instancingEnabled = source.instancing.enabled;
    target.maxInstancesPerBatch = source.instancing.maxInstancesPerBatch;

    target.iblEnabled = source.ibl.enabled;
    target.iblDiffuse = source.ibl.diffuse;
    target.iblSpecular = source.ibl.specular;

    target.occlusion =
        source.occlusion.enabled ? OcclusionMode::Coarse : OcclusionMode::Off;
    target.occlusionMaxOccluders = source.occlusion.maxOccluders;
    target.occlusionMinRadius = source.occlusion.minRadius;

    if (source.reflections.enabled) {
        target.reflections = source.reflections.mode;
        target.reflectionPlaneY = source.reflections.planeY;
        target.reflectionIntensity = source.reflections.intensity;
        target.reflectionResolutionScale = source.reflections.resolutionScale;
        target.screenSpaceTrace = source.reflections.screenSpaceTrace;
        target.ssrIntensity = source.reflections.ssrIntensity;
        target.ssrMaxRoughness = source.reflections.ssrMaxRoughness;
        target.ssrMaxSteps = source.reflections.ssrMaxSteps;
        target.ssrMaxDistance = source.reflections.ssrMaxDistance;
        target.probeResolution = source.reflections.probeResolution;
        target.maxReflectionProbes = source.reflections.maxProbes;
        target.expensiveEffectsOnlyWhenSettled = source.reflections.expensiveOnlyWhenSettled;
    } else {
        target.reflections = ReflectionMode::Off;
        target.screenSpaceTrace = false;
    }

    if (source.volumetrics.enabled) {
        target.volumetrics = source.volumetrics.quality;
        target.volumetricDensity = source.volumetrics.density;
        target.volumetricHeight = source.volumetrics.height;
        target.volumetricAnisotropy = source.volumetrics.anisotropy;
        target.volumetricShadows = source.volumetrics.sampleShadows;
    } else {
        target.volumetrics = VolumetricQuality::Off;
    }
}

RenderFeatureSettings resolveForwardRenderFeatures(ECS::World& world) {
    RenderFeatureSettings resolved = budgetSafeForwardDefaults();
    const ECS::ForwardRenderFeaturesComponent* last = nullptr;

    ECS::ComponentQuery query;
    query.with<ECS::ForwardRenderFeaturesComponent>();
    world.forEach<ECS::ForwardRenderFeaturesComponent>(
        query, [&](ECS::Entity, ECS::ForwardRenderFeaturesComponent& component) {
            if (component.enabled) last = &component;
        });

    if (last) applyForwardRenderComponent(*last, resolved);
    return resolved;
}

}  // namespace Caffeine::Render
