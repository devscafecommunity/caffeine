#include "render/MicromeshGpu.hpp"

#include "render/ShaderBytecode.hpp"

#include <algorithm>
#include <cstring>

namespace Caffeine::Render {
namespace {

struct CullUBO {
    float planes[6][4];
    float camera[4];
    u32 params[4];
};
static_assert(sizeof(CullUBO) == 96 + 16 + 16, "CullUBO must match the GLSL std140 block");

/// The scene path's block (micromesh_compact.comp): the same plus the view's first slot and the arena size.
struct SceneCullUBO {
    float planes[6][4];
    float camera[4];
    u32 params[4];  // x clusters, y flags, z instances, w mode
    u32 slots[4];   // x first slot of the view, y arena capacity
    float prevView[16];
    float prevProj[4];
    u32 hzbInfo[4];  // x width, y height, z levels
    u32 hzbOffsets[16];
};
static_assert(sizeof(SceneCullUBO) == 96 + 16 + 16 + 16 + 64 + 16 + 16 + 64, "SceneCullUBO must match the GLSL std140 block");

float bitsToFloat(u32 bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

/// Four vec4 per instance: the first three rows of the matrix, then (largest scale, 1 when uniform, 1 when the
/// instance takes part in occlusion culling, 0).
void appendInstanceRows(std::vector<float>& rows, const Mat4& m, bool occlusion = false) {
    for (int r = 0; r < 3; ++r) {
        rows.insert(rows.end(), {m(static_cast<usize>(r), 0), m(static_cast<usize>(r), 1),
                                 m(static_cast<usize>(r), 2), m(static_cast<usize>(r), 3)});
    }
    const f32 sx = Vec3(m(0, 0), m(1, 0), m(2, 0)).length();
    const f32 sy = Vec3(m(0, 1), m(1, 1), m(2, 1)).length();
    const f32 sz = Vec3(m(0, 2), m(1, 2), m(2, 2)).length();
    const f32 largest = std::max(sx, std::max(sy, sz));
    const f32 smallest = std::min(sx, std::min(sy, sz));
    const bool uniform = largest <= 0.0f || (largest - smallest) <= 0.02f * largest;
    rows.insert(rows.end(), {largest, uniform ? 1.0f : 0.0f, occlusion ? 1.0f : 0.0f, 0.0f});
}

void fillPlanes(float (&dst)[6][4], const MicromeshCullParams& params) {
    for (int i = 0; i < 6; ++i) {
        dst[i][0] = params.planes[static_cast<size_t>(i)].x;
        dst[i][1] = params.planes[static_cast<size_t>(i)].y;
        dst[i][2] = params.planes[static_cast<size_t>(i)].z;
        dst[i][3] = params.planes[static_cast<size_t>(i)].w;
    }
}

RHI::ComputePipeline* createPipeline(RHI::RenderDevice* device, BuiltinShader shader, u32 threads, u32 readonly,
                                     u32 readwrite, u32 uniforms, u32 samplers = 0, u32 threadsY = 1) {
    const RHI::ShaderBytecodeFormat format = detectShaderFormat(device);
    const ShaderBytecodeView bytecode = getBuiltinShaderBytecode(shader, format);
    if (!bytecode.data || bytecode.size == 0) return nullptr;
    RHI::ComputePipelineDesc desc;
    desc.code = bytecode.data;
    desc.codeSize = bytecode.size;
    desc.entryPoint = bytecode.entryPoint;
    desc.format = format;
    desc.threadCountX = threads;
    desc.threadCountY = threadsY;
    desc.threadCountZ = 1;
    desc.numSamplers = samplers;
    desc.numReadonlyStorageBuffers = readonly;
    desc.numReadwriteStorageBuffers = readwrite;
    desc.numUniformBuffers = uniforms;
    return device->createComputePipeline(desc);
}

}  // namespace

std::array<Vec4, 6> planesFromViewProj(const Mat4& m) {
    // clip = M * p, so a point is inside when -w <= x,y,z <= w (z >= -w is the conservative near).
    auto row = [&](int r) { return Vec4(m(r, 0), m(r, 1), m(r, 2), m(r, 3)); };
    const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    std::array<Vec4, 6> planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    for (Vec4& p : planes) {
        const f32 len = Vec3(p.x, p.y, p.z).length();
        if (len > 0.0f) p = Vec4(p.x / len, p.y / len, p.z / len, p.w / len);
    }
    return planes;
}

// ---------------------------------------------------------------------------------------------- arena

bool MicromeshArena::init(RHI::RenderDevice* device, u32 capacityIndices) {
    shutdown();
    if (!device || capacityIndices < 16) return false;
    const RHI::DeviceCapabilities caps = device->capabilities();
    if (!caps.computeStorageWrite) return false;
    m_reset = createPipeline(device, BuiltinShader::MicromeshReset, 1, 0, 1, 0);
    RHI::BufferDesc desc;
    desc.size = static_cast<size_t>(capacityIndices) * sizeof(u32);
    m_indices = device->createBuffer(desc, RHI::BufferUsage::IndexWrite);
    if (!m_reset || !m_indices) {
        if (m_reset) device->destroyComputePipeline(m_reset);
        if (m_indices) device->destroyBuffer(m_indices);
        m_reset = nullptr;
        m_indices = nullptr;
        return false;
    }
    m_device = device;
    m_capacity = capacityIndices;
    return true;
}

void MicromeshArena::shutdown() {
    if (m_device) {
        if (m_reset) m_device->destroyComputePipeline(m_reset);
        if (m_indices) m_device->destroyBuffer(m_indices);
    }
    m_reset = nullptr;
    m_indices = nullptr;
    m_device = nullptr;
    m_capacity = 0;
}

bool MicromeshArena::beginFrame(RHI::CommandBuffer* cmd) {
    if (!cmd || !m_reset || !m_indices) return false;
    RHI::Buffer* rw[1] = {m_indices};
    cmd->beginComputePass(nullptr, 0, rw, 1);
    cmd->bindComputePipeline(m_reset);
    cmd->dispatch(1, 1, 1);
    cmd->endComputePass();
    return true;
}

u32 MicromeshArena::readUsed() {
    std::vector<u8> bytes;
    if (!m_device || !m_indices || !m_device->readBuffer(m_indices, bytes, sizeof(u32)) || bytes.size() < sizeof(u32)) {
        return 0;
    }
    u32 used = 0;
    std::memcpy(&used, bytes.data(), sizeof(u32));
    return used;
}

// ---------------------------------------------------------------------------------------------- hzb

bool MicromeshHzb::init(RHI::RenderDevice* device, u32 width, u32 height) {
    if (m_device == device && m_buffer && m_width == width && m_height == height) return true;
    shutdown();
    if (!device || width == 0 || height == 0) return false;
    if (!device->capabilities().computeStorageWrite) return false;
    m_pipeline = createPipeline(device, BuiltinShader::MicromeshHzb, 8, 0, 1, 1, 1, 8);
    if (!m_pipeline) return false;
    u32 total = 0;
    u32 w = width, h = height;
    for (u32 level = 0; level < 16; ++level) {
        m_offsets[level] = total;
        total += w * h;
        m_levels = level + 1;
        if (w == 1 && h == 1) break;
        w = std::max(1u, (w + 1u) / 2u);
        h = std::max(1u, (h + 1u) / 2u);
    }
    RHI::BufferDesc desc;
    desc.size = static_cast<size_t>(total) * sizeof(float);
    m_buffer = device->createBuffer(desc, RHI::BufferUsage::StorageReadWrite);
    if (!m_buffer) {
        device->destroyComputePipeline(m_pipeline);
        m_pipeline = nullptr;
        m_levels = 0;
        return false;
    }
    m_device = device;
    m_width = width;
    m_height = height;
    return true;
}

void MicromeshHzb::shutdown() {
    if (m_device) {
        if (m_pipeline) m_device->destroyComputePipeline(m_pipeline);
        if (m_buffer) m_device->destroyBuffer(m_buffer);
    }
    m_pipeline = nullptr;
    m_buffer = nullptr;
    m_device = nullptr;
    m_width = m_height = m_levels = 0;
}

bool MicromeshHzb::build(RHI::CommandBuffer* cmd, RHI::Texture* depth, RHI::Sampler* sampler) {
    if (!cmd || !ready() || !depth || !sampler) return false;
    u32 srcW = m_width, srcH = m_height;
    for (u32 level = 0; level < m_levels; ++level) {
        const u32 dstW = level == 0 ? m_width : std::max(1u, (srcW + 1u) / 2u);
        const u32 dstH = level == 0 ? m_height : std::max(1u, (srcH + 1u) / 2u);
        struct {
            u32 sizes[4];
            u32 offsets[4];
        } ubo{};
        ubo.sizes[0] = srcW;
        ubo.sizes[1] = srcH;
        ubo.sizes[2] = dstW;
        ubo.sizes[3] = dstH;
        ubo.offsets[0] = level == 0 ? 0u : m_offsets[level - 1];
        ubo.offsets[1] = m_offsets[level];
        ubo.offsets[2] = level == 0 ? 0u : 1u;
        RHI::Buffer* rw[1] = {m_buffer};
        cmd->beginComputePass(nullptr, 0, rw, 1);
        cmd->bindComputePipeline(m_pipeline);
        cmd->bindComputeTexture(depth, 0, sampler);
        cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
        cmd->dispatch((dstW + 7u) / 8u, (dstH + 7u) / 8u, 1);
        cmd->endComputePass();
        srcW = dstW;
        srcH = dstH;
    }
    return true;
}

f32 MicromeshHzb::readTexel(u32 level, u32 x, u32 y) {
    std::vector<u8> bytes;
    if (!ready() || level >= m_levels || !m_device->readBuffer(m_buffer, bytes)) return -1.0f;
    u32 w = m_width;
    for (u32 l = 0; l < level; ++l) w = std::max(1u, (w + 1u) / 2u);
    const size_t at = (static_cast<size_t>(m_offsets[level]) + static_cast<size_t>(y) * w + x) * sizeof(float);
    if (at + sizeof(float) > bytes.size()) return -1.0f;
    float value;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
}

// ---------------------------------------------------------------------------------------------- mesh

bool MicromeshGpu::init(RHI::RenderDevice* device) {
    shutdown();
    if (!device) return false;
    const RHI::DeviceCapabilities caps = device->capabilities();
    if (!caps.indirectDraw || !caps.graphicsStorageBuffers || !caps.computeStorageWrite) return false;
    m_pipeline = createPipeline(device, BuiltinShader::MicromeshCull, 64, 2, 1, 1);
    if (!m_pipeline) return false;
    m_device = device;
    // The scene path is optional: the per-cluster path above works without it.
    m_compactPipeline = createPipeline(device, BuiltinShader::MicromeshCompact, 64, 4, 2, 1);
    return true;
}

void MicromeshGpu::clear() {
    if (!m_device) return;
    for (RHI::Buffer** b : {&m_args, &m_instances, &m_instancesGfx, &m_sceneArgs, &m_clusters, &m_positions,
                            &m_normals, &m_uvs, &m_tangents, &m_colors, &m_indices, &m_indicesStorage}) {
        if (*b) m_device->destroyBuffer(*b);
        *b = nullptr;
    }
    m_sceneRows.clear();
    m_clusterCount = 0;
    m_instanceCount = 0;
    m_sceneInstances = 0;
    m_sceneViews = 0;
    m_sourceTriangles = 0;
}

void MicromeshGpu::shutdown() {
    clear();
    if (m_device) {
        if (m_pipeline) m_device->destroyComputePipeline(m_pipeline);
        if (m_compactPipeline) m_device->destroyComputePipeline(m_compactPipeline);
    }
    m_pipeline = nullptr;
    m_compactPipeline = nullptr;
    m_device = nullptr;
}

bool MicromeshGpu::upload(const Micromesh::Data& data) {
    if (!m_device || !m_pipeline || data.clusters.empty()) return false;
    clear();

    // One index buffer for every cluster: global vertex indices, cluster after cluster.
    std::vector<u32> indices;
    std::vector<float> clusters;
    clusters.reserve(data.clusters.size() * 20);
    for (u32 i = 0; i < data.clusters.size(); ++i) {
        const Micromesh::Cluster& c = data.clusters[i];
        const u32 first = static_cast<u32>(indices.size());
        Micromesh::appendTriangles(data, i, indices);
        const u32 count = static_cast<u32>(indices.size()) - first;
        const float row[20] = {
            c.bounds.center.x, c.bounds.center.y, c.bounds.center.z, c.bounds.radius,
            c.lodSphere.center.x, c.lodSphere.center.y, c.lodSphere.center.z, c.lodSphere.radius,
            c.parentLodSphere.center.x, c.parentLodSphere.center.y, c.parentLodSphere.center.z,
            c.parentLodSphere.radius,
            c.coneAxis.x, c.coneAxis.y, c.coneAxis.z, c.coneCutoff,
            c.error, c.parentError, bitsToFloat(first), bitsToFloat(count)};
        clusters.insert(clusters.end(), row, row + 20);
    }

    RHI::BufferDesc desc;
    desc.size = clusters.size() * sizeof(float);
    m_clusters = m_device->createBuffer(desc, RHI::BufferUsage::StorageRead);
    desc.size = data.positions.size() * sizeof(Vec3);
    m_positions = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
    desc.size = data.normals.size() * sizeof(Vec3);
    if (desc.size > 0) m_normals = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
    desc.size = data.uvs.size() * sizeof(Vec2);
    if (desc.size > 0) m_uvs = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
    desc.size = data.tangents.size() * sizeof(Vec4);
    if (desc.size > 0) m_tangents = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
    desc.size = data.colors.size() * sizeof(Vec4);
    if (desc.size > 0) m_colors = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
    desc.size = indices.size() * sizeof(u32);
    m_indicesStorage = m_compactPipeline ? m_device->createBuffer(desc, RHI::BufferUsage::StorageRead) : nullptr;
    m_indices = m_device->createBuffer(desc, RHI::BufferUsage::Index);
    if (!m_clusters || !m_positions || !m_indices ||
        (m_compactPipeline && (!m_indicesStorage ||
                               !m_device->uploadBuffer(m_indicesStorage, indices.data(), indices.size() * sizeof(u32)))) ||
        !m_device->uploadBuffer(m_clusters, clusters.data(), clusters.size() * sizeof(float)) ||
        !m_device->uploadBuffer(m_positions, data.positions.data(), data.positions.size() * sizeof(Vec3)) ||
        (data.normals.size() == data.positions.size() &&
         !(m_normals && m_device->uploadBuffer(m_normals, data.normals.data(), data.normals.size() * sizeof(Vec3)))) ||
        (!data.uvs.empty() &&
         !(m_uvs && m_device->uploadBuffer(m_uvs, data.uvs.data(), data.uvs.size() * sizeof(Vec2)))) ||
        (!data.tangents.empty() &&
         !(m_tangents && m_device->uploadBuffer(m_tangents, data.tangents.data(), data.tangents.size() * sizeof(Vec4)))) ||
        (!data.colors.empty() &&
         !(m_colors && m_device->uploadBuffer(m_colors, data.colors.data(), data.colors.size() * sizeof(Vec4)))) ||
        !m_device->uploadBuffer(m_indices, indices.data(), indices.size() * sizeof(u32))) {
        clear();
        return false;
    }
    m_clusterCount = static_cast<u32>(data.clusters.size());
    m_sourceTriangles = data.sourceTriangles;
    if (!setInstances({Mat4::identity()})) {
        clear();
        return false;
    }
    return true;
}

bool MicromeshGpu::setInstances(const std::vector<Mat4>& transforms) {
    if (!m_device || m_clusterCount == 0 || transforms.empty()) return false;
    std::vector<float> rows;
    rows.reserve(transforms.size() * 16);
    for (const Mat4& m : transforms) appendInstanceRows(rows, m);
    const u64 total = static_cast<u64>(transforms.size()) * m_clusterCount;
    if (total > 0xFFFFFFFFull / 5u) return false;
    for (RHI::Buffer** b : {&m_args, &m_instances, &m_instancesGfx}) {
        if (*b) m_device->destroyBuffer(*b);
        *b = nullptr;
    }
    RHI::BufferDesc desc;
    desc.size = rows.size() * sizeof(float);
    m_instances = m_device->createBuffer(desc, RHI::BufferUsage::StorageRead);
    m_instancesGfx = m_device->createBuffer(desc, RHI::BufferUsage::GraphicsStorage);
    desc.size = static_cast<size_t>(total) * 5 * sizeof(u32);
    m_args = m_device->createBuffer(desc, RHI::BufferUsage::IndirectWrite);
    if (!m_instances || !m_instancesGfx || !m_args ||
        !m_device->uploadBuffer(m_instances, rows.data(), rows.size() * sizeof(float)) ||
        !m_device->uploadBuffer(m_instancesGfx, rows.data(), rows.size() * sizeof(float))) {
        for (RHI::Buffer** b : {&m_args, &m_instances, &m_instancesGfx}) {
            if (*b) m_device->destroyBuffer(*b);
            *b = nullptr;
        }
        m_instanceCount = 0;
        return false;
    }
    m_instanceCount = static_cast<u32>(transforms.size());
    return true;
}

bool MicromeshGpu::cull(RHI::CommandBuffer* cmd, const MicromeshCullParams& params) {
    if (!cmd || !m_pipeline || m_clusterCount == 0 || m_instanceCount == 0) return false;
    CullUBO ubo{};
    fillPlanes(ubo.planes, params);
    ubo.camera[0] = params.camera.x;
    ubo.camera[1] = params.camera.y;
    ubo.camera[2] = params.camera.z;
    ubo.camera[3] = params.threshold;
    ubo.params[0] = m_clusterCount;
    ubo.params[2] = m_instanceCount;
    ubo.params[1] = (params.frustum ? 1u : 0u) | (params.cone ? 2u : 0u) | (params.viewDependent ? 4u : 0u);

    RHI::Buffer* rw[1] = {m_args};
    cmd->beginComputePass(nullptr, 0, rw, 1);
    cmd->bindComputePipeline(m_pipeline);
    RHI::Buffer* ro[2] = {m_clusters, m_instances};
    cmd->bindComputeStorageBuffers(ro, 0, 2);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch((drawCount() + 63u) / 64u, 1, 1);
    cmd->endComputePass();
    return true;
}

void MicromeshGpu::draw(RHI::CommandBuffer* cmd) const {
    if (!cmd || m_clusterCount == 0 || m_instanceCount == 0) return;
    cmd->bindVertexBuffer(m_positions, 0, 0);
    cmd->bindIndexBuffer(m_indices);
    cmd->drawIndexedIndirect(m_args, 0, drawCount());
}

void MicromeshGpu::drawLit(RHI::CommandBuffer* cmd) const {
    if (!cmd || m_clusterCount == 0 || m_instanceCount == 0 || !m_normals) return;
    cmd->bindVertexBuffer(m_positions, 0, 0);
    cmd->bindVertexBuffer(m_normals, 1, 0);
    cmd->bindIndexBuffer(m_indices);
    cmd->drawIndexedIndirect(m_args, 0, drawCount());
}

void MicromeshGpu::drawTextured(RHI::CommandBuffer* cmd) const {
    if (!cmd || m_clusterCount == 0 || m_instanceCount == 0 || !m_normals || !m_uvs) return;
    cmd->bindVertexBuffer(m_positions, 0, 0);
    cmd->bindVertexBuffer(m_normals, 1, 0);
    cmd->bindVertexBuffer(m_uvs, 2, 0);
    cmd->bindIndexBuffer(m_indices);
    cmd->drawIndexedIndirect(m_args, 0, drawCount());
}

std::vector<u32> MicromeshGpu::readSelected() {
    std::vector<u32> selected;
    std::vector<u8> bytes;
    if (!m_device || !m_args || !m_device->readBuffer(m_args, bytes)) return selected;
    for (u32 i = 0; i < drawCount(); ++i) {
        u32 indexCount = 0;
        std::memcpy(&indexCount, bytes.data() + static_cast<size_t>(i) * 20, sizeof(u32));
        if (indexCount > 0) selected.push_back(i);
    }
    return selected;
}

MicromeshGpu::Stats MicromeshGpu::readStats() {
    Stats stats;
    std::vector<u8> bytes;
    if (!m_device || !m_args || !m_device->readBuffer(m_args, bytes)) return stats;
    for (u32 i = 0; i < drawCount(); ++i) {
        u32 indexCount = 0;
        std::memcpy(&indexCount, bytes.data() + static_cast<size_t>(i) * 20, sizeof(u32));
        if (indexCount > 0) {
            ++stats.visibleClusters;
            stats.triangles += indexCount / 3u;
        }
    }
    return stats;
}

// ------------------------------------------------------------------------------------------ scene path

bool MicromeshGpu::setSceneInstances(const std::vector<Mat4>& transforms, u32 viewCount,
                                     const std::vector<u8>& occlusion) {
    if (!m_device || !m_compactPipeline || m_clusterCount == 0 || transforms.empty() || viewCount == 0 ||
        !m_indicesStorage) {
        return false;
    }
    std::vector<float> rows;
    rows.reserve(transforms.size() * 16);
    for (size_t i = 0; i < transforms.size(); ++i) {
        appendInstanceRows(rows, transforms[i], i < occlusion.size() && occlusion[i] != 0);
    }
    const u32 count = static_cast<u32>(transforms.size());
    const bool sameShape = m_instances && m_instancesGfx && m_sceneArgs && m_sceneInstances == count &&
                           m_sceneViews == viewCount;
    if (sameShape) {
        if (rows == m_sceneRows) return true;
        if (!m_device->uploadBuffer(m_instances, rows.data(), rows.size() * sizeof(float)) ||
            !m_device->uploadBuffer(m_instancesGfx, rows.data(), rows.size() * sizeof(float))) {
            return false;
        }
        m_sceneRows = std::move(rows);
        return true;
    }
    for (RHI::Buffer** b : {&m_instances, &m_instancesGfx, &m_sceneArgs}) {
        if (*b) m_device->destroyBuffer(*b);
        *b = nullptr;
    }
    m_sceneRows.clear();
    m_sceneInstances = 0;
    m_sceneViews = 0;
    RHI::BufferDesc desc;
    desc.size = rows.size() * sizeof(float);
    m_instances = m_device->createBuffer(desc, RHI::BufferUsage::StorageRead);
    m_instancesGfx = m_device->createBuffer(desc, RHI::BufferUsage::GraphicsStorage);
    desc.size = static_cast<size_t>(count) * viewCount * kSlotUints * sizeof(u32);
    m_sceneArgs = m_device->createBuffer(desc, RHI::BufferUsage::IndirectWrite);
    if (!m_instances || !m_instancesGfx || !m_sceneArgs ||
        !m_device->uploadBuffer(m_instances, rows.data(), rows.size() * sizeof(float)) ||
        !m_device->uploadBuffer(m_instancesGfx, rows.data(), rows.size() * sizeof(float))) {
        for (RHI::Buffer** b : {&m_instances, &m_instancesGfx, &m_sceneArgs}) {
            if (*b) m_device->destroyBuffer(*b);
            *b = nullptr;
        }
        return false;
    }
    m_sceneRows = std::move(rows);
    m_sceneInstances = count;
    m_sceneViews = viewCount;
    return true;
}

bool MicromeshGpu::cullScene(RHI::CommandBuffer* cmd, const MicromeshCullParams& params, MicromeshArena& arena,
                             u32 view) {
    if (!cmd || !m_compactPipeline || !arena.ready() || m_sceneInstances == 0 || view >= m_sceneViews) return false;
    SceneCullUBO ubo{};
    fillPlanes(ubo.planes, params);
    ubo.camera[0] = params.camera.x;
    ubo.camera[1] = params.camera.y;
    ubo.camera[2] = params.camera.z;
    ubo.camera[3] = params.threshold;
    ubo.params[0] = m_clusterCount;
    ubo.params[1] = (params.frustum ? 1u : 0u) | (params.cone ? 2u : 0u) | (params.viewDependent ? 4u : 0u);
    ubo.params[2] = m_sceneInstances;
    ubo.slots[0] = view * m_sceneInstances;
    ubo.slots[1] = arena.capacity();
    RHI::Buffer* hzbBuffer = m_clusters;  // a stand-in the shader never reads when occlusion is off
    if (params.occlusion && params.occlusion->hzb && params.occlusion->hzb->ready()) {
        const MicromeshOcclusion& o = *params.occlusion;
        ubo.params[1] |= 8u;
        std::memcpy(ubo.prevView, o.prevView.data(), sizeof(ubo.prevView));
        ubo.prevProj[0] = o.p00;
        ubo.prevProj[1] = o.p11;
        ubo.prevProj[2] = o.p22;
        ubo.prevProj[3] = o.p23;
        ubo.hzbInfo[0] = o.hzb->width();
        ubo.hzbInfo[1] = o.hzb->height();
        ubo.hzbInfo[2] = o.hzb->levels();
        for (u32 l = 0; l < o.hzb->levels() && l < 16; ++l) ubo.hzbOffsets[l] = o.hzb->levelOffset(l);
        hzbBuffer = o.hzb->buffer();
    }

    const u32 perInstance = (m_sceneInstances + 63u) / 64u;
    const u32 perCluster = (m_sceneInstances * m_clusterCount + 63u) / 64u;
    // reset (3), count (0), reserve (1), copy (2): separate compute passes, which orders them.
    const struct {
        u32 mode;
        u32 groups;
    } steps[4] = {{3u, perInstance}, {0u, perCluster}, {1u, perInstance}, {2u, perCluster}};
    for (const auto& step : steps) {
        ubo.params[3] = step.mode;
        RHI::Buffer* rw[2] = {m_sceneArgs, arena.indices()};
        cmd->beginComputePass(nullptr, 0, rw, 2);
        cmd->bindComputePipeline(m_compactPipeline);
        RHI::Buffer* ro[4] = {m_clusters, m_instances, m_indicesStorage, hzbBuffer};
        cmd->bindComputeStorageBuffers(ro, 0, 4);
        cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
        cmd->dispatch(step.groups, 1, 1);
        cmd->endComputePass();
    }
    return true;
}

void MicromeshGpu::drawSceneSlot(RHI::CommandBuffer* cmd, const MicromeshArena& arena, u32 view, u32 instance) const {
    if (!cmd || !hasSceneAttributes() || !arena.ready() || view >= m_sceneViews || instance >= m_sceneInstances) return;
    cmd->bindVertexBuffer(m_positions, 0, 0);
    cmd->bindVertexBuffer(m_normals, 1, 0);
    cmd->bindVertexBuffer(m_uvs, 2, 0);
    cmd->bindVertexBuffer(m_tangents, 3, 0);
    cmd->bindVertexBuffer(m_colors, 4, 0);
    cmd->bindIndexBuffer(arena.indices());
    const u32 slot = view * m_sceneInstances + instance;
    cmd->drawIndexedIndirect(m_sceneArgs, slot * kSlotUints * static_cast<u32>(sizeof(u32)), 1);
}

void MicromeshGpu::drawSceneSlotPositions(RHI::CommandBuffer* cmd, const MicromeshArena& arena, u32 view,
                                          u32 instance) const {
    if (!cmd || !m_positions || !arena.ready() || view >= m_sceneViews || instance >= m_sceneInstances) return;
    cmd->bindVertexBuffer(m_positions, 0, 0);
    cmd->bindIndexBuffer(arena.indices());
    const u32 slot = view * m_sceneInstances + instance;
    cmd->drawIndexedIndirect(m_sceneArgs, slot * kSlotUints * static_cast<u32>(sizeof(u32)), 1);
}

MicromeshGpu::SceneViewStats MicromeshGpu::readSceneView(u32 view) {
    SceneViewStats stats;
    std::vector<u8> bytes;
    if (!m_device || !m_sceneArgs || view >= m_sceneViews || !m_device->readBuffer(m_sceneArgs, bytes)) return stats;
    for (u32 i = 0; i < m_sceneInstances; ++i) {
        const size_t at = (static_cast<size_t>(view) * m_sceneInstances + i) * kSlotUints * sizeof(u32);
        u32 slot[kSlotUints];
        std::memcpy(slot, bytes.data() + at, sizeof(slot));
        stats.visibleClusters += slot[5];
        stats.triangles += slot[0] / 3u;
        stats.overflowed += slot[7] != 0 ? 1u : 0u;
    }
    return stats;
}

}  // namespace Caffeine::Render
