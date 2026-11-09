#pragma once

#include "core/Types.hpp"
#include "fluid/FluidWorld.hpp"

namespace Caffeine::ECS {
class World;
}

namespace Caffeine::Scene {

/// Steps every NaturalFluidComponent in the world, updates the generated mesh
/// and keeps the runtime simulators alive across frames.
///
/// `editorPreview` is true when the editor is not in play mode. Components with
/// previewInEditor set still advance; the others keep their current frame.
void tickNaturalFluids(ECS::World& world, f32 dt, bool editorPreview);

/// Runtime simulator for one entity, or null when it has no live component.
/// The Doppio plugin uses this to draw the live fluid preview.
const Fluids::FluidWorld* naturalFluidWorldFor(ECS::World& world, u32 entityId);

/// Mutable accessor used by authoring tools to inject drops / impulses.
Fluids::FluidWorld* naturalFluidWorldForEdit(ECS::World& world, u32 entityId);

/// Forces one entity to rebuild its simulators on the next tick.
void resetNaturalFluid(ECS::World& world, u32 entityId);

/// Drops all runtime simulators. Call when a scene is closed or replaced.
void resetNaturalFluidRuntime();

}  // namespace Caffeine::Scene
