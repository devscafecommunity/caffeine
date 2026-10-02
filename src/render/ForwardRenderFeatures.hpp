#pragma once

#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/World.hpp"
#include "render/RenderFeatures.hpp"

namespace Caffeine::Render {

/// Budget-safe defaults when no scene component is present.
RenderFeatureSettings budgetSafeForwardDefaults();

void applyForwardRenderComponent(const ECS::ForwardRenderFeaturesComponent& source,
                                 RenderFeatureSettings& target);

/// Merges enabled ForwardRenderFeaturesComponent entities (last entity wins).
RenderFeatureSettings resolveForwardRenderFeatures(ECS::World& world);

}  // namespace Caffeine::Render
