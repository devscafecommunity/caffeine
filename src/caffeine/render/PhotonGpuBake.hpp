#pragma once

#include "core/Types.hpp"
#include <vector>

namespace Caffeine::RHI {
class RenderDevice;
}

namespace Caffeine::Render {

struct AOBakeDesc {
    const float* triangles = nullptr;
    unsigned int triangleCount = 0;
    const float* positions = nullptr;
    const float* normals = nullptr;
    unsigned int texelCount = 0;
    unsigned int resolution = 0;
    unsigned int rays = 16;
    float maxDistance = 2.5f;
    float bias = 0.02f;
};

/// GPU compute AO bake. Returns false when compute is unavailable.
bool gpuBakeAmbientOcclusion(RHI::RenderDevice* device, const AOBakeDesc& desc,
                             std::vector<f32>& outAO);

}  // namespace Caffeine::Render
