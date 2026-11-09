// ============================================================================
// @file    MinZPyramid.hpp
// @brief   Photon near-field Min-Z pyramid (conservative linear-depth mip chain)
// @note    Part of render/ — prerequisite P0 of the Photon architecture.
//
//  The cascade tracer needs to answer "what is the closest surface along this
//  screen-space ray?" without a depth-buffer march. The spec (§5.2) defines a
//  Min-Z pyramid: an R32F mip chain where level 0 holds the linear view depth
//  of every pixel and every coarser level holds the minimum of its 2×2
//  footprint. Background pixels store a large sentinel so they never occlude.
//
//  Generation is deliberately portable:
//    level 0  : scene depth (D32) → linear depth, 1 tap per pixel
//    level m  : copy level m-1 into a scratch texture, then 2×2 min into mip m
//  The scratch copy keeps the source and destination subresources disjoint, so
//  the pass is valid on every SDL_GPU backend (no same-texture feedback).
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "math/Mat4.hpp"
#include "rhi/RenderDevice.hpp"

namespace Caffeine::Render {

/// Camera-dependent inputs for one Min-Z generation.
///
/// `proj` carries the elements of the projection matrix used by the scene
/// pass in the same layout as the other post shaders:
///   proj[0] = P00, proj[1] = P11, proj[2] = P22, proj[3] = P23
/// and `proj32` = P32 (-1 for perspective, 0 for orthographic).
struct MinZParams {
    f32 proj[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    f32 proj32  = -1.0f;
    /// Depth written for pixels that hit the far plane / sky. Kept finite so
    /// later arithmetic (logs, reciprocals) never produces NaN.
    f32 skyDepth = 1.0e30f;

    /// Extracts the elements above from a projection matrix.
    static MinZParams fromProjection(const Mat4& projection);
};

// ============================================================================
// @brief  Owns the Min-Z texture, its scratch staging texture, the reduction
//         pipeline and the sampler. One instance per view / viewport.
// ============================================================================
class MinZPyramid {
public:
    MinZPyramid() = default;
    ~MinZPyramid() = default;

    MinZPyramid(const MinZPyramid&) = delete;
    MinZPyramid& operator=(const MinZPyramid&) = delete;

    /// Creates (or recreates) the pyramid for `width × height`. Safe to call
    /// with the same size: it becomes a no-op. Returns false when the device is
    /// not initialised or a resource could not be created.
    bool init(RHI::RenderDevice* device, u32 width, u32 height);
    /// Releases every GPU resource. Safe to call more than once.
    void shutdown(RHI::RenderDevice* device);

    bool isReady() const { return m_minZ != nullptr && m_scratch != nullptr && m_pipeline != nullptr; }

    /// Records the full reduction of `sceneDepth` into the pyramid. Must be
    /// called outside a render pass; leaves no pass active. No-op when the
    /// resources are not ready.
    void generate(RHI::CommandBuffer* cmd, RHI::Texture* sceneDepth, const MinZParams& params);

    /// Full mip chain containing linear view depth; `Sampler` usage, so the
    /// cascade tracer can sample any level.
    RHI::Texture* texture() const { return m_minZ; }
    /// Number of mip levels in `texture()` (0 before init).
    u32 mipCount() const { return m_mips; }
    u32 width() const { return m_width; }
    u32 height() const { return m_height; }
    /// Number of mip levels for a `width × height` chain (includes level 0).
    static u32 mipCountFor(u32 width, u32 height);

private:
    void releaseResources(RHI::RenderDevice* device);

    RHI::RenderDevice* m_device  = nullptr;
    RHI::Texture*      m_minZ    = nullptr;
    RHI::Texture*      m_scratch = nullptr;
    RHI::Pipeline*     m_pipeline = nullptr;
    RHI::Sampler*      m_sampler  = nullptr;
    u32 m_width  = 0;
    u32 m_height = 0;
    u32 m_mips   = 0;
};

}  // namespace Caffeine::Render
