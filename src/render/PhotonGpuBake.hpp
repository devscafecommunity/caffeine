#pragma once

#include "core/Types.hpp"
#include "caffeine/plugin/PluginAPI.hpp"

#include <vector>

namespace Caffeine::RHI {
class RenderDevice;
}

namespace Caffeine::Render {

/// GPU compute AO bake. Returns false when compute is unavailable.
bool gpuBakeAmbientOcclusion(RHI::RenderDevice* device,
                             const Editor::PluginAOBakeDesc& desc,
                             std::vector<f32>& outAO);

}  // namespace Caffeine::Render
