#pragma once

#include "assets/MeshTypes.hpp"
#include "ecs/MeshComponents.hpp"
#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#endif

namespace Caffeine::Render {

/// CPU-side procedural meshes shared by the GPU scene renderer.
class GpuProceduralMeshes {
public:
    static Assets::Mesh3D* get(ECS::MeshPrimitive primitive);

#ifdef CF_HAS_SDL3
    static void releaseGpuResources(RHI::RenderDevice* device);
#endif

private:
    static void ensureBuilt();
    static Assets::Mesh3D& cube();
    static Assets::Mesh3D& plane();
    static Assets::Mesh3D& sphere();
    static Assets::Mesh3D& cylinder();
};

}  // namespace Caffeine::Render
