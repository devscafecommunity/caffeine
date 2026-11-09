#pragma once

#ifdef CF_HAS_SDL3

#include "RenderTypes.hpp"
#include "Camera2D.hpp"
#include "TextureAtlas.hpp"
#include "../math/Mat4.hpp"
#include "../ecs/Components.hpp"
#include "../rhi/RenderDevice.hpp"
#include "../rhi/CommandBuffer.hpp"
#include <vector>
#include <algorithm>
#include <cstring>

namespace Caffeine::Render {

using namespace Caffeine;

class BatchRenderer {
public:
    static constexpr u32 MAX_SPRITES_PER_BATCH = 32768;
    static constexpr u32 MAX_VERTICES          = MAX_SPRITES_PER_BATCH * 4;
    static constexpr u32 MAX_INDICES           = MAX_SPRITES_PER_BATCH * 6;

    explicit BatchRenderer(RHI::RenderDevice* device)
        : m_device(device)
    {}

    ~BatchRenderer() = default;

    void beginFrame() {
        m_pending.clear();
        m_stats = {};
    }

    void setCamera(const Camera2D& camera) {
        m_camera = camera;
    }

    void setAtlas(const TextureAtlas* atlas) {
        m_atlas = atlas;
    }

    void submitSprite(const Mat4& worldTransform, const ECS::Sprite& sprite) {
        SpritePrimitive sp;
        sp.transform = worldTransform;
        sp.tint      = 0xFFFFFFFF;
        sp.uv        = (m_atlas && m_atlas->isPacked())
                           ? m_atlas->getUV(sprite.name.c_str())
                           : Vec4{ 0.0f, 0.0f, 1.0f, 1.0f };

        const f32 depth = worldTransform(2, 3);
        sp.sortKey = buildSortKey(0, 0, depth);

        m_pending.push_back(sp);
        m_stats.totalSprites++;
    }

    void endFrame(RHI::CommandBuffer* cmd) {
        if (m_pending.empty() || !cmd) return;

        radixSort(m_pending);

        std::vector<SpriteVertex> verts;
        std::vector<u32>          indices;
        verts.reserve(m_pending.size() * 4);
        indices.reserve(m_pending.size() * 6);

        for (const auto& sp : m_pending) {
            buildQuad(sp, verts, indices, static_cast<u32>(verts.size() / 4 * 4));
        }

        m_stats.verticesUploaded = static_cast<u32>(verts.size());
        m_stats.totalBatches     = 1;
        m_stats.drawCalls        = 1;

        const Mat4 vp = m_camera.viewProjectionMatrix();
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, vp.data(), sizeof(Mat4));

        if (m_device && m_device->isInitialized()) {
            RHI::Buffer* vertexBuf = m_device->createBuffer(
                { static_cast<u64>(verts.size() * sizeof(SpriteVertex)), "BatchVertices" },
                RHI::BufferUsage::Vertex
            );
            RHI::Buffer* indexBuf = m_device->createBuffer(
                { static_cast<u64>(indices.size() * sizeof(u32)), "BatchIndices" },
                RHI::BufferUsage::Index
            );

            if (vertexBuf && indexBuf) {
                cmd->bindVertexBuffer(vertexBuf);
                cmd->bindIndexBuffer(indexBuf);
                cmd->drawIndexed(static_cast<u32>(indices.size()));
                m_device->destroyBuffer(vertexBuf);
                m_device->destroyBuffer(indexBuf);
            }
        }
    }

    struct FrameStats {
        u32 totalSprites     = 0;
        u32 totalBatches     = 0;
        u32 drawCalls        = 0;
        u32 verticesUploaded = 0;
    };

    FrameStats lastFrameStats() const { return m_stats; }

private:
    struct SpritePrimitive {
        Mat4 transform;
        Vec4 uv;
        u32  tint;
        u32  sortKey;
    };

    static u32 buildSortKey(i32 layer, u32 textureId, f32 depth) {
        const u32 layerBits  = (static_cast<u32>(layer + 128) & 0xFF) << 24;
        const u32 texBits    = (textureId & 0xFFF) << 12;
        const f32 normDepth  = depth < 0.0f ? 0.0f : (depth > 1.0f ? 1.0f : depth);
        const u32 depthBits  = static_cast<u32>(normDepth * 0xFFFu) & 0xFFF;
        return layerBits | texBits | depthBits;
    }

    static void radixSort(std::vector<SpritePrimitive>& sprites) {
        const usize n = sprites.size();
        if (n <= 1) return;

        std::vector<SpritePrimitive> temp(n);

        for (int shift = 0; shift < 32; shift += 8) {
            u32 count[256] = {};
            for (const auto& s : sprites) {
                count[(s.sortKey >> shift) & 0xFF]++;
            }
            u32 prefix = 0;
            u32 prefixes[256];
            for (int i = 0; i < 256; ++i) {
                prefixes[i] = prefix;
                prefix += count[i];
            }
            for (const auto& s : sprites) {
                temp[prefixes[(s.sortKey >> shift) & 0xFF]++] = s;
            }
            sprites.swap(temp);
        }
    }

    static void buildQuad(const SpritePrimitive& sp,
                          std::vector<SpriteVertex>& verts,
                          std::vector<u32>&          indices,
                          u32                        baseVertex) {
        const Vec3 corners[4] = {
            {-0.5f, -0.5f, 0.0f},
            { 0.5f, -0.5f, 0.0f},
            { 0.5f,  0.5f, 0.0f},
            {-0.5f,  0.5f, 0.0f},
        };
        const Vec2 uvCorners[4] = {
            {sp.uv.x, sp.uv.w},
            {sp.uv.z, sp.uv.w},
            {sp.uv.z, sp.uv.y},
            {sp.uv.x, sp.uv.y},
        };
        for (int i = 0; i < 4; ++i) {
            verts.push_back({ sp.transform.transformPoint(corners[i]), uvCorners[i], sp.tint });
        }
        indices.push_back(baseVertex + 0);
        indices.push_back(baseVertex + 1);
        indices.push_back(baseVertex + 2);
        indices.push_back(baseVertex + 0);
        indices.push_back(baseVertex + 2);
        indices.push_back(baseVertex + 3);
    }

    RHI::RenderDevice*          m_device = nullptr;
    const TextureAtlas*          m_atlas  = nullptr;
    Camera2D                     m_camera;
    std::vector<SpritePrimitive> m_pending;
    FrameStats                   m_stats;
};

}  // namespace Caffeine::Render

#endif  // CF_HAS_SDL3
