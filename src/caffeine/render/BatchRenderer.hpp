#pragma once

#ifdef CF_HAS_SDL3

#include "RenderTypes.hpp"
#include "Camera2D.hpp"
#include "TextureAtlas.hpp"
#include "../math/Mat4.hpp"
#include "../ecs/Components.hpp"
#include "../rhi/RenderDevice.hpp"
#include "../rhi/CommandBuffer.hpp"
#include "ShaderBytecode.hpp"
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

    ~BatchRenderer() { detachGpu(); }

    BatchRenderer(const BatchRenderer&) = delete;
    BatchRenderer& operator=(const BatchRenderer&) = delete;

    /// Builds the GPU pipeline (batch2d.vert/frag), a clamped sampler and a 1x1 white texture for untextured
    /// triangles. `colorFormat` must match the render pass the batch is drawn into. Returns false when the device
    /// cannot build them (no GPU, or the builtin shaders were not compiled); `endFrame` then draws nothing.
    bool attachGpu(RHI::RenderDevice* device, RHI::TextureFormat colorFormat) {
        detachGpu();
        if (!device || !device->isInitialized()) return false;
        m_device = device;

        RHI::Shader* vertex = createBuiltinShader(device, BuiltinShader::Batch2DVertex, RHI::ShaderStage::Vertex, 1, 0);
        RHI::Shader* fragment = createBuiltinShader(device, BuiltinShader::Batch2DFragment, RHI::ShaderStage::Fragment, 0, 1);
        if (vertex && fragment) {
            RHI::VertexBufferLayoutDesc layout{0, sizeof(GpuVertex), false};
            RHI::VertexAttributeDesc attributes[3] = {
                {0, 0, RHI::VertexFormat::Float3, 0},
                {1, 0, RHI::VertexFormat::Float2, 12},
                {2, 0, RHI::VertexFormat::Float4, 20},
            };
            RHI::GraphicsPipelineDesc desc;
            desc.vertexBuffers = &layout;
            desc.numVertexBuffers = 1;
            desc.attributes = attributes;
            desc.numAttributes = 3;
            desc.colorFormat = colorFormat;
            desc.depthTest = false;
            desc.depthWrite = false;
            desc.blendMode = RHI::BlendMode::Alpha;
            m_pipeline = device->createGraphicsPipeline(vertex, fragment, desc);
        }
        if (vertex) device->destroyShader(vertex);
        if (fragment) device->destroyShader(fragment);

        RHI::SamplerDesc samplerDesc;
        samplerDesc.linearFilter = true;
        samplerDesc.clampToEdge = true;
        m_sampler = device->createSampler(samplerDesc);

        RHI::TextureDesc whiteDesc;
        whiteDesc.width = 1;
        whiteDesc.height = 1;
        whiteDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
        whiteDesc.usage = RHI::TextureUsage::Sampler;
        m_white = device->createTexture(whiteDesc);
        if (m_white) {
            const u8 white[4] = {255, 255, 255, 255};
            device->uploadTexture(m_white, white, 1, 1, 4);
        }
        return gpuReady();
    }

    /// Releases the GPU objects. Safe to call twice, and before `attachGpu`.
    void detachGpu() {
        if (!m_device) return;
        releaseRetired();
        if (m_pipeline) m_device->destroyPipeline(m_pipeline);
        if (m_sampler) m_device->destroySampler(m_sampler);
        if (m_white) m_device->destroyTexture(m_white);
        m_pipeline = nullptr;
        m_sampler = nullptr;
        m_white = nullptr;
    }

    bool gpuReady() const {
        return m_device && m_device->isInitialized() && m_pipeline && m_sampler && m_white;
    }

    void beginFrame() {
        m_pending.clear();
        m_meshes.clear();
        m_textureSlots.clear();
        m_stats = {};
    }

    void setCamera(const Camera2D& camera) {
        m_camera = camera;
        m_viewProjection = camera.viewProjectionMatrix();
    }

    /// The view-projection the GPU draw uses: a 2D camera (setCamera) or a 3D scene camera (proj * view).
    void setViewProjection(const Mat4& viewProjection) {
        m_viewProjection = viewProjection;
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

    /// Submits a textured quad: a unit square centred on the origin, then `worldTransform`. `uv` is
    /// (u0, vTop, u1, vBottom) in texture space. A null `texture` draws the tint alone. Sprites are sorted by
    /// texture, so each texture is one draw.
    void submitTexturedQuad(const Mat4& worldTransform, RHI::Texture* texture, Vec4 uv, u32 tint = 0xFFFFFFFF) {
        SpritePrimitive sp;
        sp.transform = worldTransform;
        sp.uv        = uv;
        sp.tint      = tint;
        sp.texture   = texture;
        sp.sortKey   = buildSortKey(0, textureSlot(texture), worldTransform(2, 3));
        m_pending.push_back(sp);
        m_stats.totalSprites++;
    }

    /// Submits an arbitrary 2D triangle mesh (e.g. a skinned 2D rig). `positions` are in the entity's local
    /// space and go through `transform`. `uvs` run [0, 1] and map into `uvRect` (u0, v0, u1, v1) the same way
    /// sprite quads map their atlas rectangle. Meshes are drawn after sprites, in submission order.
    void submitMesh2D(const Mat4& transform, const std::vector<Vec2>& positions, const std::vector<Vec2>& uvs,
                      const std::vector<u32>& indices, Vec4 uvRect, u32 tint = 0xFFFFFFFF) {
        MeshPrimitive2D mesh;
        mesh.transform = transform;
        mesh.positions = positions;
        mesh.uvs       = uvs;
        mesh.indices   = indices;
        mesh.uvRect    = uvRect;
        mesh.tint      = tint;
        m_meshes.push_back(std::move(mesh));
    }

    /// One indexed draw: `indexCount` indices from `firstIndex`, all with `texture` (null is the white texture).
    struct DrawRun {
        RHI::Texture* texture = nullptr;
        u32           firstIndex = 0;
        u32           indexCount = 0;
    };

    /// CPU side of endFrame: sorted sprite quads, then mesh triangles, grouped into runs by texture. Public so the
    /// output can be tested without a GPU.
    void buildFrame(std::vector<SpriteVertex>& verts, std::vector<u32>& indices, std::vector<DrawRun>& runs) {
        verts.clear();
        indices.clear();
        runs.clear();
        radixSort(m_pending);
        verts.reserve(m_pending.size() * 4);
        indices.reserve(m_pending.size() * 6);

        for (const auto& sp : m_pending) {
            const u32 first = static_cast<u32>(indices.size());
            buildQuad(sp, verts, indices, static_cast<u32>(verts.size()));
            appendRun(runs, sp.texture, first, static_cast<u32>(indices.size()) - first);
        }

        for (const MeshPrimitive2D& mesh : m_meshes) {
            const u32 base = static_cast<u32>(verts.size());
            for (usize i = 0; i < mesh.positions.size(); ++i) {
                const Vec2 p = mesh.positions[i];
                const Vec2 t = i < mesh.uvs.size() ? mesh.uvs[i] : Vec2(0.0f, 0.0f);
                SpriteVertex v;
                v.position = mesh.transform.transformPoint(Vec3(p.x, p.y, 0.0f));
                v.texcoord = Vec2(mesh.uvRect.x + t.x * (mesh.uvRect.z - mesh.uvRect.x),
                                  mesh.uvRect.w + t.y * (mesh.uvRect.y - mesh.uvRect.w));
                v.tint     = mesh.tint;
                verts.push_back(v);
            }
            // Whole triangles only: a triangle that references a missing vertex is dropped.
            const u32 first = static_cast<u32>(indices.size());
            const usize n = mesh.positions.size();
            for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const u32 a = mesh.indices[i];
                const u32 b = mesh.indices[i + 1];
                const u32 c = mesh.indices[i + 2];
                if (a >= n || b >= n || c >= n) continue;
                indices.insert(indices.end(), {base + a, base + b, base + c});
            }
            appendRun(runs, nullptr, first, static_cast<u32>(indices.size()) - first);
        }
    }

    void buildFrame(std::vector<SpriteVertex>& verts, std::vector<u32>& indices) {
        std::vector<DrawRun> runs;
        buildFrame(verts, indices, runs);
    }

    /// Draws the frame: sorted sprites, then meshes, in one indexed draw. The command buffer must be inside a render
    /// pass whose colour format matches attachGpu. Without a GPU (no attachGpu, or a headless device) the frame is
    /// built and counted but nothing is drawn.
    void endFrame(RHI::CommandBuffer* cmd) {
        // Last frame's buffers are released now, when the GPU has finished with them.
        releaseRetired();
        if ((m_pending.empty() && m_meshes.empty()) || !cmd) return;

        std::vector<SpriteVertex> verts;
        std::vector<u32>          indices;
        std::vector<DrawRun>      runs;
        buildFrame(verts, indices, runs);
        m_stats.verticesUploaded = static_cast<u32>(verts.size());
        m_stats.totalBatches     = static_cast<u32>(runs.size());
        if (indices.empty() || !gpuReady()) return;

        std::vector<GpuVertex> gpu(verts.size());
        for (usize i = 0; i < verts.size(); ++i) {
            const SpriteVertex& v = verts[i];
            const u32 t = v.tint;
            gpu[i] = GpuVertex{
                v.position.x, v.position.y, v.position.z,
                v.texcoord.x, v.texcoord.y,
                static_cast<f32>((t >> 24) & 0xFF) / 255.0f,
                static_cast<f32>((t >> 16) & 0xFF) / 255.0f,
                static_cast<f32>((t >> 8) & 0xFF) / 255.0f,
                static_cast<f32>(t & 0xFF) / 255.0f,
            };
        }

        const u64 vertexBytes = static_cast<u64>(gpu.size()) * sizeof(GpuVertex);
        const u64 indexBytes  = static_cast<u64>(indices.size()) * sizeof(u32);
        RHI::Buffer* vertexBuf = m_device->createBuffer({ vertexBytes, "BatchVertices" }, RHI::BufferUsage::Vertex);
        RHI::Buffer* indexBuf  = m_device->createBuffer({ indexBytes, "BatchIndices" }, RHI::BufferUsage::Index);
        if (!vertexBuf || !indexBuf) {
            if (vertexBuf) m_device->destroyBuffer(vertexBuf);
            if (indexBuf) m_device->destroyBuffer(indexBuf);
            return;
        }
        m_device->uploadBuffer(vertexBuf, gpu.data(), vertexBytes);
        m_device->uploadBuffer(indexBuf, indices.data(), indexBytes);

        cmd->bindPipeline(m_pipeline);
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, m_viewProjection.data(), sizeof(Mat4));
        cmd->bindVertexBuffer(vertexBuf);
        cmd->bindIndexBuffer(indexBuf);
        for (const DrawRun& run : runs) {
            cmd->bindTexture(run.texture ? run.texture : m_white, 0, m_sampler);
            cmd->drawIndexed(run.indexCount, run.firstIndex);
        }
        m_stats.drawCalls = static_cast<u32>(runs.size());
        m_retired.push_back(vertexBuf);
        m_retired.push_back(indexBuf);
    }

    struct FrameStats {
        u32 totalSprites     = 0;
        u32 totalBatches     = 0;
        u32 drawCalls        = 0;
        u32 verticesUploaded = 0;
    };

    FrameStats lastFrameStats() const { return m_stats; }

private:
    /// Vertex as the GPU reads it: Float3 position, Float2 texcoord, Float4 colour (the tint unpacked). 36 bytes.
    struct GpuVertex {
        f32 x, y, z;
        f32 u, v;
        f32 r, g, b, a;
    };
    static_assert(sizeof(GpuVertex) == 36, "GpuVertex must match the pipeline's attribute offsets");

    void releaseRetired() {
        if (m_device) {
            for (RHI::Buffer* buffer : m_retired) m_device->destroyBuffer(buffer);
        }
        m_retired.clear();
    }

    struct SpritePrimitive {
        Mat4 transform;
        Vec4 uv;
        u32  tint;
        u32  sortKey;
        RHI::Texture* texture = nullptr;
    };

    /// Id a texture sorts under (1-based; 0 is the white texture). Past 4095 textures share the last id: the runs
    /// stay correct, they just break less often in the sort.
    u32 textureSlot(RHI::Texture* texture) {
        if (!texture) return 0;
        for (usize i = 0; i < m_textureSlots.size(); ++i) {
            if (m_textureSlots[i] == texture) return static_cast<u32>(i + 1);
        }
        if (m_textureSlots.size() >= 0xFFFu) return 0xFFFu;
        m_textureSlots.push_back(texture);
        return static_cast<u32>(m_textureSlots.size());
    }

    static void appendRun(std::vector<DrawRun>& runs, RHI::Texture* texture, u32 first, u32 count) {
        if (count == 0) return;
        if (!runs.empty() && runs.back().texture == texture) {
            runs.back().indexCount += count;
            return;
        }
        runs.push_back(DrawRun{texture, first, count});
    }

    struct MeshPrimitive2D {
        Mat4              transform;
        std::vector<Vec2> positions;
        std::vector<Vec2> uvs;
        std::vector<u32>  indices;
        Vec4              uvRect {0.0f, 0.0f, 1.0f, 1.0f};
        u32               tint = 0xFFFFFFFF;
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
    Mat4                         m_viewProjection = Mat4::identity();
    RHI::Pipeline*               m_pipeline = nullptr;
    RHI::Sampler*                m_sampler = nullptr;
    RHI::Texture*                m_white = nullptr;
    std::vector<RHI::Buffer*>    m_retired;
    std::vector<RHI::Texture*>   m_textureSlots;
    std::vector<SpritePrimitive> m_pending;
    std::vector<MeshPrimitive2D> m_meshes;
    FrameStats                   m_stats;
};

}  // namespace Caffeine::Render

#endif  // CF_HAS_SDL3
