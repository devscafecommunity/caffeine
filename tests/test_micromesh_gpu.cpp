#include "catch.hpp"

#include "../src/caffeine/micromesh/Micromesh.hpp"
#include "../src/caffeine/render/MicromeshGpu.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::RHI;
using namespace Caffeine::Micromesh;
using Caffeine::Render::MicromeshCullParams;
using Caffeine::Render::MicromeshArena;
using Caffeine::Render::MicromeshHzb;
using Caffeine::Render::MicromeshOcclusion;
using Caffeine::Render::MicromeshGpu;
using Caffeine::Render::planesFromViewProj;

// Micromeshes on the GPU: the compute pass that picks the cut and writes the draw arguments, and
// the single indirect draw over every cluster. Needs a GPU (skipped otherwise).

namespace {

Data sphereHierarchy(int subdivisions, std::vector<Vec3>* positionsOut = nullptr) {
    const f32 t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> positions;
    const Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (const Vec3& v : base) positions.push_back(v.normalized());
    std::vector<u32> indices = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2,
                                10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5,
                                2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<u32, u32>, u32> mid;
        auto middle = [&](u32 a, u32 b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            positions.push_back(((positions[a] + positions[b]) * 0.5f).normalized());
            return mid[key] = static_cast<u32>(positions.size() - 1);
        };
        std::vector<u32> next;
        for (size_t i = 0; i < indices.size(); i += 3) {
            const u32 a = indices[i], b = indices[i + 1], c = indices[i + 2];
            const u32 ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        indices = std::move(next);
    }
    if (positionsOut) *positionsOut = positions;
    return build(positions, indices);
}

bool compileGlsl(const std::string& source, const char* stage, std::vector<u8>& out) {
    const auto dir = std::filesystem::temp_directory_path();
    const auto src = dir / (std::string("caffeine_mm_test_") + stage + ".glsl");
    const auto spv = dir / (std::string("caffeine_mm_test_") + stage + ".spv");
    {
        std::ofstream file(src);
        file << source;
    }
    const std::string cmd = std::string("glslc -fshader-stage=") + stage + " \"" + src.string() + "\" -o \"" +
                            spv.string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return false;
    std::ifstream in(spv, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

struct Gpu {
    RenderDevice device;
    MicromeshGpu mesh;
    Data data;
    bool ok = false;

    Gpu() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
        RenderConfig config;
        config.width = 64;
        config.height = 64;
        if (!device.initOffscreen(config)) return;
        if (!mesh.init(&device)) return;
        data = sphereHierarchy(5);
        ok = mesh.upload(data);
    }
    ~Gpu() {
        mesh.shutdown();
        device.shutdown();
    }

    std::vector<u32> cullOnce(const MicromeshCullParams& params) {
        CommandBuffer* cmd = device.beginOffscreen();
        REQUIRE(mesh.cull(cmd, params));
        device.endOffscreen(cmd);
        auto selected = mesh.readSelected();
        device.releaseOffscreenResources();
        return selected;
    }
};

MicromeshCullParams viewOnly(const Vec3& camera, f32 threshold) {
    MicromeshCullParams p;
    p.camera = camera;
    p.threshold = threshold;
    p.frustum = false;
    p.cone = false;
    p.viewDependent = true;
    return p;
}

}  // namespace

TEST_CASE("Micromesh GPU: the cut the compute pass selects is the CPU's cut", "[micromesh][gpu]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    REQUIRE(gpu->mesh.clusterCount() == gpu->data.clusters.size());
    for (f32 distance : {1.5f, 3.0f, 8.0f, 40.0f}) {
        for (f32 threshold : {0.0005f, 0.002f, 0.01f}) {
            const Vec3 camera(distance, 0.2f, 0.1f);
            CutParams cpu;
            cpu.viewDependent = true;
            cpu.camera = camera;
            cpu.threshold = threshold;
            const std::vector<u32> expected = selectCut(gpu->data, cpu);
            const std::vector<u32> got = gpu->cullOnce(viewOnly(camera, threshold));
            INFO("distance " << distance << " threshold " << threshold);
            REQUIRE_FALSE(got.empty());
            CHECK(got == expected);
        }
    }
    // Further away means a coarser cut on the GPU as well.
    const auto nearCut = gpu->cullOnce(viewOnly(Vec3(1.5f, 0, 0), 0.002f));
    const auto farCut = gpu->cullOnce(viewOnly(Vec3(40.0f, 0, 0), 0.002f));
    CHECK(farCut.size() < nearCut.size());
}

TEST_CASE("Micromesh GPU: frustum and back-face culling remove clusters, never add them", "[micromesh][gpu]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const Vec3 camera(2.2f, 0.0f, 0.0f);
    const f32 threshold = 0.0005f;
    const auto all = gpu->cullOnce(viewOnly(camera, threshold));

    // Cone: exactly the CPU's isBackFacing filter.
    MicromeshCullParams cone = viewOnly(camera, threshold);
    cone.cone = true;
    std::vector<u32> expectedCone;
    for (u32 c : all) {
        if (!isBackFacing(gpu->data.clusters[c], camera)) expectedCone.push_back(c);
    }
    const auto gotCone = gpu->cullOnce(cone);
    CHECK(gotCone == expectedCone);
    CHECK(gotCone.size() < all.size());
    CHECK(gotCone.size() > all.size() / 4);

    // Frustum: a narrow view toward +Y sees only the top of the sphere.
    const Mat4 view = Mat4::lookAt(camera, Vec3(0.0f, 0.9f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 proj = Mat4::perspective(0.25f, 1.0f, 0.05f, 100.0f);
    MicromeshCullParams frustum = viewOnly(camera, threshold);
    frustum.frustum = true;
    frustum.planes = planesFromViewProj(proj * view);
    const auto gotFrustum = gpu->cullOnce(frustum);
    CHECK(gotFrustum.size() < all.size());
    CHECK_FALSE(gotFrustum.empty());
    // The same sphere-plane test on the CPU gives the same set.
    std::vector<u32> expected;
    for (u32 c : all) {
        const Sphere& b = gpu->data.clusters[c].bounds;
        bool inside = true;
        for (const Vec4& p : frustum.planes) {
            if (p.x * b.center.x + p.y * b.center.y + p.z * b.center.z + p.w < -b.radius) inside = false;
        }
        if (inside) expected.push_back(c);
    }
    CHECK(gotFrustum == expected);
    // Everything the frustum keeps was in the unculled cut.
    for (u32 c : gotFrustum) CHECK(std::binary_search(all.begin(), all.end(), c));
}

TEST_CASE("Micromesh GPU: one indirect draw renders the selected cut", "[micromesh][gpu]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    std::vector<u8> vs, fs;
    if (!compileGlsl(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; } u;
void main() { gl_Position = u.uViewProj * vec4(aPosition, 1.0); }
)", "vertex", vs) ||
        !compileGlsl(R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 1.0, 1.0, 1.0); }
)", "fragment", fs)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    Shader* vert = gpu->device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    Shader* frag = gpu->device.createShader(fd);
    REQUIRE(vert);
    REQUIRE(frag);
    VertexBufferLayoutDesc layout{0, sizeof(Vec3), false};
    VertexAttributeDesc attr{0, 0, VertexFormat::Float3, 0};
    GraphicsPipelineDesc pd;
    pd.vertexBuffers = &layout;
    pd.numVertexBuffers = 1;
    pd.attributes = &attr;
    pd.numAttributes = 1;
    pd.depthTest = false;
    pd.depthWrite = false;
    Pipeline* pipeline = gpu->device.createGraphicsPipeline(vert, frag, pd);
    TextureDesc td;
    td.width = 64;
    td.height = 64;
    td.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* target = gpu->device.createTexture(td);
    REQUIRE(pipeline);
    REQUIRE(target);

    auto render = [&](const Vec3& camera, f32 threshold) {
        const Mat4 viewProj = Mat4::perspective(0.9f, 1.0f, 0.05f, 100.0f) *
                              Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
        MicromeshCullParams p;
        p.camera = camera;
        p.threshold = threshold;
        p.planes = planesFromViewProj(viewProj);
        CommandBuffer* cmd = gpu->device.beginOffscreen();
        REQUIRE(gpu->mesh.cull(cmd, p));
        RenderPassDesc pass;
        pass.colorTarget = target;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(pipeline);
        cmd->pushUniformData(ShaderStage::Vertex, 0, viewProj.data(), 64);
        gpu->mesh.draw(cmd);
        cmd->endRenderPass();
        gpu->device.endOffscreen(cmd);
        std::vector<u8> pixels;
        gpu->device.readTexture(target, pixels);
        auto selected = gpu->mesh.readSelected();
        gpu->device.releaseOffscreenResources();
        size_t covered = 0;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) covered += pixels[i] > 128 ? 1 : 0;
        return std::make_pair(covered, selected);
    };
    // Near and fine: many clusters. Far and coarse: few. Both fill the middle of the frame and leave
    // the corners empty (the sphere is a disc).
    const auto fine = render(Vec3(2.2f, 0.0f, 0.0f), 0.0f);
    const auto coarse = render(Vec3(2.2f, 0.0f, 0.0f), 0.05f);
    REQUIRE(fine.first > 64u * 64u / 8u);
    REQUIRE(fine.first < 64u * 64u);
    CHECK(fine.second.size() > coarse.second.size());
    CHECK(coarse.first > fine.first / 2);  // the coarse cut still covers most of the disc
    // Zero error budget picks level 0 only: every source cluster that survives culling.
    for (u32 c : fine.second) CHECK(gpu->data.clusters[c].level == 0u);
    // A view-dependent cut mixes levels (near parts stay fine), but a loose budget reaches coarse ones.
    u32 coarseLevels = 0;
    for (u32 c : coarse.second) coarseLevels = std::max(coarseLevels, gpu->data.clusters[c].level);
    CHECK(coarseLevels > 0u);
}

// ---- instances: one hierarchy, many transforms, one cull dispatch and one indirect draw

namespace {

/// What the cut of instance `transform` must be: the cut of the untransformed mesh seen from the camera
/// brought into the instance's space (a uniform scale cancels in a view-dependent error).
std::vector<u32> expectedInstanceCut(const Data& data, const Mat4& transform, const Vec3& camera, f32 threshold) {
    CutParams cpu;
    cpu.viewDependent = true;
    cpu.camera = transform.inverted().transformPoint(camera);
    cpu.threshold = threshold;
    return selectCut(data, cpu);
}

std::vector<u32> clustersOfInstance(const std::vector<u32>& draws, u32 clusterCount, u32 instance) {
    std::vector<u32> out;
    for (u32 g : draws) {
        if (g / clusterCount == instance) out.push_back(g % clusterCount);
    }
    return out;
}

}  // namespace

TEST_CASE("Micromesh GPU: every instance gets the cut its own transform and distance ask for",
          "[micromesh][gpu][instances]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const std::vector<Mat4> instances = {
        Mat4::identity(),
        Mat4::translation(10.0f, 0.0f, 3.0f) * Mat4::rotationY(0.7f) * Mat4::scale(2.0f),
        Mat4::translation(-3.0f, 1.0f, -8.0f) * Mat4::rotationX(-0.4f) * Mat4::scale(0.5f),
        Mat4::translation(60.0f, 0.0f, 40.0f) * Mat4::rotationY(2.0f) * Mat4::scale(1.5f),
    };
    REQUIRE(gpu->mesh.setInstances(instances));
    REQUIRE(gpu->mesh.instanceCount() == 4u);
    REQUIRE(gpu->mesh.drawCount() == 4u * gpu->mesh.clusterCount());

    const Vec3 camera(4.0f, 1.0f, 6.0f);
    for (f32 threshold : {0.0005f, 0.002f, 0.01f}) {
        const auto draws = gpu->cullOnce(viewOnly(camera, threshold));
        for (u32 i = 0; i < instances.size(); ++i) {
            INFO("instance " << i << " threshold " << threshold);
            const auto expected = expectedInstanceCut(gpu->data, instances[i], camera, threshold);
            REQUIRE_FALSE(expected.empty());
            CHECK(clustersOfInstance(draws, gpu->mesh.clusterCount(), i) == expected);
        }
    }
    // The farthest instance (index 3) is cut coarser than the nearest (index 0), although it is bigger.
    const auto draws = gpu->cullOnce(viewOnly(camera, 0.002f));
    const auto nearCut = clustersOfInstance(draws, gpu->mesh.clusterCount(), 0);
    const auto farCut = clustersOfInstance(draws, gpu->mesh.clusterCount(), 3);
    CHECK(farCut.size() < nearCut.size());
    // The stats add the instances up.
    const auto stats = gpu->mesh.readStats();
    CHECK(stats.visibleClusters == draws.size());
    CHECK(stats.triangles > 0u);
}

TEST_CASE("Micromesh GPU: frustum and back-face culling are decided per instance",
          "[micromesh][gpu][instances]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    // Camera at the origin looking down -Z: instance 0 in front, instance 1 behind, instance 2 far to
    // the side, instance 3 in front and rotated half a turn (its cone axes point the other way).
    const std::vector<Mat4> instances = {
        Mat4::translation(0.0f, 0.0f, -6.0f),
        Mat4::translation(0.0f, 0.0f, 6.0f),
        Mat4::translation(40.0f, 0.0f, -6.0f),
        Mat4::translation(0.0f, 0.0f, -12.0f) * Mat4::rotationY(3.14159265f),
    };
    REQUIRE(gpu->mesh.setInstances(instances));
    const Vec3 camera(0.0f, 0.0f, 0.0f);
    const Mat4 viewProj = Mat4::perspective(0.8f, 1.0f, 0.05f, 100.0f) *
                          Mat4::lookAt(camera, Vec3(0, 0, -1), Vec3(0, 1, 0));
    MicromeshCullParams params = viewOnly(camera, 0.0005f);
    params.frustum = true;
    params.planes = planesFromViewProj(viewProj);
    const auto withFrustum = gpu->cullOnce(params);
    const u32 cc = gpu->mesh.clusterCount();
    CHECK_FALSE(clustersOfInstance(withFrustum, cc, 0).empty());
    CHECK(clustersOfInstance(withFrustum, cc, 1).empty());  // behind the camera
    CHECK(clustersOfInstance(withFrustum, cc, 2).empty());  // far outside the side planes
    CHECK_FALSE(clustersOfInstance(withFrustum, cc, 3).empty());

    // The cone: keep only what faces the camera. The sphere is symmetric, so a half turn of instance 3
    // must keep the same number of clusters as instance 0 would at that distance, never zero or all.
    params.frustum = false;
    params.cone = true;
    const auto withCone = gpu->cullOnce(params);
    const auto unculled = gpu->cullOnce(viewOnly(camera, 0.0005f));
    for (u32 i : {0u, 3u}) {
        const auto kept = clustersOfInstance(withCone, cc, i);
        const auto all = clustersOfInstance(unculled, cc, i);
        INFO("instance " << i);
        CHECK(kept.size() < all.size());
        CHECK(kept.size() > all.size() / 4);
        // The cone test is the CPU's isBackFacing in the instance's own space.
        const Mat4 inverse = instances[i].inverted();
        const Vec3 local = inverse.transformPoint(camera);
        std::vector<u32> expected;
        for (u32 c : all) {
            if (!isBackFacing(gpu->data.clusters[c], local)) expected.push_back(c);
        }
        CHECK(kept == expected);
    }
}

TEST_CASE("Micromesh GPU: one indirect draw renders every instance where its matrix puts it",
          "[micromesh][gpu][instances]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    REQUIRE(gpu->mesh.setInstances({Mat4::translation(-3.0f, 0.0f, 0.0f), Mat4::translation(3.0f, 0.0f, 0.0f)}));
    std::vector<u8> vs, fs;
    if (!compileGlsl(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 instances[]; };
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; uvec4 params; } u;
void main() {
    uint instance = uint(gl_InstanceIndex) / u.params.x;
    vec4 r0 = instances[instance * 4u], r1 = instances[instance * 4u + 1u], r2 = instances[instance * 4u + 2u];
    vec3 world = vec3(dot(r0.xyz, aPosition) + r0.w, dot(r1.xyz, aPosition) + r1.w, dot(r2.xyz, aPosition) + r2.w);
    gl_Position = u.uViewProj * vec4(world, 1.0);
}
)", "vertex", vs) ||
        !compileGlsl(R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 1.0, 1.0, 1.0); }
)", "fragment", fs)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    vd.numStorageBuffers = 1;
    Shader* vert = gpu->device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    Shader* frag = gpu->device.createShader(fd);
    REQUIRE(vert);
    REQUIRE(frag);
    VertexBufferLayoutDesc layout{0, sizeof(Vec3), false};
    VertexAttributeDesc attr{0, 0, VertexFormat::Float3, 0};
    GraphicsPipelineDesc pd;
    pd.vertexBuffers = &layout;
    pd.numVertexBuffers = 1;
    pd.attributes = &attr;
    pd.numAttributes = 1;
    pd.depthTest = false;
    pd.depthWrite = false;
    Pipeline* pipeline = gpu->device.createGraphicsPipeline(vert, frag, pd);
    TextureDesc td;
    td.width = 64;
    td.height = 64;
    td.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* target = gpu->device.createTexture(td);
    REQUIRE(pipeline);
    REQUIRE(target);

    const Vec3 camera(0.0f, 0.0f, 12.0f);
    struct Push {
        float viewProj[16];
        u32 params[4];
    } push{};
    const Mat4 viewProj = Mat4::perspective(0.9f, 1.0f, 0.05f, 100.0f) *
                          Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
    std::memcpy(push.viewProj, viewProj.data(), sizeof(push.viewProj));
    push.params[0] = gpu->mesh.clusterCount();
    MicromeshCullParams p = viewOnly(camera, 0.0f);
    p.frustum = true;
    p.planes = planesFromViewProj(viewProj);
    CommandBuffer* cmd = gpu->device.beginOffscreen();
    REQUIRE(gpu->mesh.cull(cmd, p));
    RenderPassDesc pass;
    pass.colorTarget = target;
    cmd->beginRenderPass(pass);
    cmd->bindPipeline(pipeline);
    Buffer* storage[1] = {gpu->mesh.instanceBuffer()};
    cmd->bindVertexStorageBuffers(storage, 0, 1);
    cmd->pushUniformData(ShaderStage::Vertex, 0, &push, sizeof(push));
    gpu->mesh.draw(cmd);
    cmd->endRenderPass();
    gpu->device.endOffscreen(cmd);
    std::vector<u8> pixels;
    REQUIRE(gpu->device.readTexture(target, pixels));
    gpu->device.releaseOffscreenResources();

    auto lit = [&](int x0, int x1) {
        size_t n = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = x0; x < x1; ++x) n += pixels[(static_cast<size_t>(y) * 64 + static_cast<size_t>(x)) * 4] > 128 ? 1 : 0;
        }
        return n;
    };
    const size_t left = lit(0, 28), middle = lit(28, 36), right = lit(36, 64);
    CHECK(left > 40u);
    CHECK(right > 40u);
    CHECK(middle == 0u);  // the gap between the two spheres
    // Mirror images of each other: the same cut of the same mesh, shifted by a symmetric amount.
    const double ratio = static_cast<double>(left) / static_cast<double>(right);
    CHECK(ratio > 0.8);
    CHECK(ratio < 1.25);
}

// ---- shading normals: a coarse cut shades like the exact mesh

TEST_CASE("Micromesh GPU: carried normals make a coarse cut shade like the exact mesh, facet normals do not",
          "[micromesh][gpu][normals]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    REQUIRE(gpu->mesh.normals() != nullptr);
    std::vector<u8> vs, fs;
    if (!compileGlsl(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; } u;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
void main() { vWorld = aPosition; vNormal = aNormal; gl_Position = u.uViewProj * vec4(aPosition, 1.0); }
)", "vertex", vs) ||
        !compileGlsl(R"(#version 450
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(set = 3, binding = 0) uniform F { vec4 mode; } f;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 n = f.mode.x > 0.5 ? normalize(cross(dFdx(vWorld), dFdy(vWorld))) : normalize(vNormal);
    if (dot(n, normalize(vec3(0.0, 0.0, 1.0))) < 0.0) n = -n;
    vec3 l = normalize(vec3(0.5, 0.7, 0.6));
    float diffuse = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, normalize(l + vec3(0.0, 0.0, 1.0))), 0.0), 40.0);
    outColor = vec4(vec3(0.12 + 0.7 * diffuse + 0.5 * spec), 1.0);
}
)", "fragment", fs)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    Shader* vert = gpu->device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    fd.numUniformBuffers = 1;
    Shader* frag = gpu->device.createShader(fd);
    REQUIRE(vert);
    REQUIRE(frag);
    VertexBufferLayoutDesc layouts[2] = {{0, sizeof(Vec3), false}, {1, sizeof(Vec3), false}};
    VertexAttributeDesc attrs[2] = {{0, 0, VertexFormat::Float3, 0}, {1, 1, VertexFormat::Float3, 0}};
    GraphicsPipelineDesc pd;
    pd.vertexBuffers = layouts;
    pd.numVertexBuffers = 2;
    pd.attributes = attrs;
    pd.numAttributes = 2;
    pd.depthTest = true;
    pd.depthWrite = true;
    Pipeline* pipeline = gpu->device.createGraphicsPipeline(vert, frag, pd);
    TextureDesc td;
    td.width = 96;
    td.height = 96;
    td.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* target = gpu->device.createTexture(td);
    TextureDesc dd = td;
    dd.format = TextureFormat::D32_FLOAT;
    dd.usage = TextureUsage::DepthStencil;
    Texture* depth = gpu->device.createTexture(dd);
    REQUIRE(pipeline);
    REQUIRE(target);
    REQUIRE(depth);

    const Vec3 camera(0.0f, 0.0f, 3.2f);
    const Mat4 viewProj = Mat4::perspective(0.7f, 1.0f, 0.05f, 100.0f) *
                          Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
    auto render = [&](f32 threshold, f32 facet) {
        MicromeshCullParams p;
        p.camera = camera;
        p.threshold = threshold;
        p.planes = planesFromViewProj(viewProj);
        const f32 mode[4] = {facet, 0, 0, 0};
        CommandBuffer* cmd = gpu->device.beginOffscreen();
        REQUIRE(gpu->mesh.cull(cmd, p));
        RenderPassDesc pass;
        pass.colorTarget = target;
        pass.depthTarget = depth;
        pass.clearDepth = true;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(pipeline);
        cmd->pushUniformData(ShaderStage::Vertex, 0, viewProj.data(), 64);
        cmd->pushUniformData(ShaderStage::Fragment, 0, mode, sizeof(mode));
        gpu->mesh.drawLit(cmd);
        cmd->endRenderPass();
        gpu->device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(gpu->device.readTexture(target, pixels));
        const auto stats = gpu->mesh.readStats();
        gpu->device.releaseOffscreenResources();
        return std::make_pair(pixels, stats);
    };
    auto meanError = [](const std::vector<u8>& a, const std::vector<u8>& b) {
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = 0; i + 3 < a.size(); i += 4) {
            if (a[i] == 0 && b[i] == 0) continue;  // background in both
            sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            ++n;
        }
        return n ? sum / static_cast<double>(n) : 0.0;
    };

    const auto reference = render(0.0f, 0.0f);
    REQUIRE(reference.second.triangles > 1000u);
    for (f32 threshold : {0.016f, 0.06f}) {
        const auto smooth = render(threshold, 0.0f);
        const auto facet = render(threshold, 1.0f);
        INFO("threshold " << threshold);
        // It really is a coarser cut.
        CHECK(smooth.second.triangles * 3 < reference.second.triangles);
        const double smoothError = meanError(smooth.first, reference.first);
        const double facetError = meanError(facet.first, reference.first);
        INFO("smooth " << smoothError << " facet " << facetError);
        CHECK(smoothError < 6.0);                 // within 6 of 255 on average
        CHECK(smoothError * 2.0 < facetError);    // facets are far worse than carried normals
    }
}

// ---- the scene path: compacted cuts of many instances, one view at a time, into one arena

TEST_CASE("Micromesh GPU: the compacted scene cut keeps exactly the clusters and triangles of the per-cluster cut",
          "[micromesh][gpu][compact]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    // Instances with different scales (one non-uniform), rotations and distances.
    const std::vector<Mat4> instances = {
        Mat4::identity(),
        Mat4::translation(1.0f, 0.0f, 0.5f) * Mat4::rotationY(0.4f) * Mat4::scale(1.6f),
        Mat4::translation(-3.0f, 0.5f, -4.0f) * Mat4::rotationX(0.8f) * Mat4::scale(0.7f),
        Mat4::translation(2.0f, -1.0f, -12.0f) * Mat4::scale(1.4f, 0.8f, 1.1f),  // non-uniform
    };
    REQUIRE(gpu->mesh.setInstances(instances));
    REQUIRE(gpu->mesh.setSceneInstances(instances, 3));
    MicromeshArena arena;
    REQUIRE(arena.init(&gpu->device, 1u << 20));

    u32 coarsest = ~0u, finest = 0;
    for (const Vec3& camera : {Vec3(3.0f, 0.5f, 5.0f), Vec3(-6.0f, 2.0f, 9.0f), Vec3(1.0f, 0.0f, 40.0f)}) {
        for (f32 threshold : {0.0f, 0.0008f, 0.004f, 0.02f}) {
            const Mat4 viewProj = Mat4::perspective(0.8f, 1.0f, 0.05f, 200.0f) *
                                  Mat4::lookAt(camera, Vec3(0.0f, 0.0f, 0.0f), Vec3(0, 1, 0));
            MicromeshCullParams p;
            p.camera = camera;
            p.threshold = threshold;
            p.planes = planesFromViewProj(viewProj);
            // The non-uniform instance has no valid cone test: compare without it on both paths.
            p.cone = false;
            CommandBuffer* a = gpu->device.beginOffscreen();
            REQUIRE(gpu->mesh.cull(a, p));
            gpu->device.endOffscreen(a);
            const auto expected = gpu->mesh.readStats();
            gpu->device.releaseOffscreenResources();

            CommandBuffer* b = gpu->device.beginOffscreen();
            REQUIRE(arena.beginFrame(b));
            REQUIRE(gpu->mesh.cullScene(b, p, arena, 1));  // a view that is not the first: slot offsets
            gpu->device.endOffscreen(b);
            const auto got = gpu->mesh.readSceneView(1);
            const u32 used = arena.readUsed();
            gpu->device.releaseOffscreenResources();
            INFO("camera " << camera.x << "," << camera.y << "," << camera.z << " threshold " << threshold);
            REQUIRE(expected.triangles > 0u);
            CHECK(got.visibleClusters == expected.visibleClusters);
            CHECK(got.triangles == expected.triangles);
            CHECK(got.overflowed == 0u);
            CHECK(used == 1u + 3u * static_cast<u32>(expected.triangles));  // the arena holds exactly the cut
            coarsest = std::min<u32>(coarsest, static_cast<u32>(got.triangles));
            finest = std::max<u32>(finest, static_cast<u32>(got.triangles));
        }
    }
    CHECK(coarsest < finest);  // the cases really span different cuts
    arena.shutdown();
}

TEST_CASE("Micromesh GPU: a cut that does not fit in the arena draws nothing and says so",
          "[micromesh][gpu][compact]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const std::vector<Mat4> instances = {Mat4::identity(), Mat4::translation(0.5f, 0.0f, 0.0f), Mat4::translation(-0.5f, 0.2f, 0.0f)};
    REQUIRE(gpu->mesh.setSceneInstances(instances, 1));
    MicromeshCullParams p = viewOnly(Vec3(0, 0, 4.0f), 0.0f);
    p.frustum = false;
    // One exact cut of the sphere is 20 480 triangles (61 440 indices): an arena of 100 000 holds one instance,
    // not three.
    MicromeshArena arena;
    REQUIRE(arena.init(&gpu->device, 100000));
    CommandBuffer* cmd = gpu->device.beginOffscreen();
    REQUIRE(arena.beginFrame(cmd));
    REQUIRE(gpu->mesh.cullScene(cmd, p, arena, 0));
    gpu->device.endOffscreen(cmd);
    const auto stats = gpu->mesh.readSceneView(0);
    const u32 used = arena.readUsed();
    gpu->device.releaseOffscreenResources();
    CHECK(stats.overflowed == 2u);         // the first instance fits, the other two do not
    CHECK(stats.triangles == 20480u);      // and exactly that one is drawn
    CHECK(stats.triangles * 3u + 1u <= arena.capacity());  // nothing it draws lies outside the arena
    (void)used;
    arena.shutdown();
}

// ---- occlusion: the farthest-depth pyramid of a depth buffer hides what lies behind it

TEST_CASE("Micromesh GPU: clusters hidden behind a wall in the depth buffer are culled, the rest are not",
          "[micromesh][gpu][occlusion]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    // A wall: two triangles at z = 5, one metre either side... four metres across, seen from z = 10.
    std::vector<u8> vs, fs;
    if (!compileGlsl(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; } u;
void main() { gl_Position = u.uViewProj * vec4(aPosition, 1.0); }
)", "vertex", vs) ||
        !compileGlsl(R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0); }
)", "fragment", fs)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    Shader* vert = gpu->device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    Shader* frag = gpu->device.createShader(fd);
    REQUIRE(vert);
    REQUIRE(frag);
    VertexBufferLayoutDesc layout{0, sizeof(Vec3), false};
    VertexAttributeDesc attr{0, 0, VertexFormat::Float3, 0};
    GraphicsPipelineDesc pd;
    pd.vertexBuffers = &layout;
    pd.numVertexBuffers = 1;
    pd.attributes = &attr;
    pd.numAttributes = 1;
    pd.depthTest = true;
    pd.depthWrite = true;
    Pipeline* pipeline = gpu->device.createGraphicsPipeline(vert, frag, pd);
    const Vec3 quad[6] = {{-2, -2, 5}, {2, -2, 5}, {2, 2, 5}, {-2, -2, 5}, {2, 2, 5}, {-2, 2, 5}};
    BufferDesc bd;
    bd.size = sizeof(quad);
    Buffer* wall = gpu->device.createBuffer(bd, BufferUsage::Vertex);
    REQUIRE(wall);
    REQUIRE(gpu->device.uploadBuffer(wall, quad, sizeof(quad)));
    TextureDesc cd;
    cd.width = 128;
    cd.height = 128;
    cd.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* color = gpu->device.createTexture(cd);
    TextureDesc dd = cd;
    dd.format = TextureFormat::D32_FLOAT;
    dd.usage = TextureUsage::DepthStencil | TextureUsage::Sampler;
    Texture* depth = gpu->device.createTexture(dd);
    SamplerDesc sd;
    Sampler* sampler = gpu->device.createSampler(sd);
    REQUIRE(pipeline);
    REQUIRE(color);
    REQUIRE(depth);
    REQUIRE(sampler);

    const Vec3 camera(0.0f, 0.0f, 10.0f);
    const Mat4 view = Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
    const Mat4 proj = Mat4::perspective(0.9f, 1.0f, 0.1f, 100.0f);
    const Mat4 viewProj = proj * view;

    // Instances (the sphere has radius 1): hidden behind the wall, beside it, and in front of it.
    const std::vector<Mat4> instances = {Mat4::translation(0.0f, 0.0f, 0.0f), Mat4::translation(6.0f, 0.0f, 0.0f),
                                         Mat4::translation(0.0f, 0.0f, 7.0f)};
    REQUIRE(gpu->mesh.setSceneInstances(instances, 1, {1, 1, 1}));
    MicromeshArena arena;
    REQUIRE(arena.init(&gpu->device, 1u << 20));
    MicromeshHzb hzb;
    REQUIRE(hzb.init(&gpu->device, 128, 128));
    CHECK(hzb.levels() == 8u);  // 128 .. 1

    auto cullAll = [&](bool occlusion) {
        MicromeshOcclusion o;
        o.hzb = &hzb;
        o.prevView = view;
        o.p00 = proj(0, 0);
        o.p11 = proj(1, 1);
        o.p22 = proj(2, 2);
        o.p23 = proj(2, 3);
        MicromeshCullParams p = viewOnly(camera, 0.0f);
        p.frustum = false;
        p.cone = false;
        p.viewDependent = false;
        p.threshold = 0.0f;
        if (occlusion) p.occlusion = &o;
        CommandBuffer* cmd = gpu->device.beginOffscreen();
        RenderPassDesc pass;
        pass.colorTarget = color;
        pass.depthTarget = depth;
        pass.clearDepth = true;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(pipeline);
        cmd->pushUniformData(ShaderStage::Vertex, 0, viewProj.data(), 64);
        cmd->bindVertexBuffer(wall, 0, 0);
        cmd->draw(6);
        cmd->endRenderPass();
        REQUIRE(hzb.build(cmd, depth, sampler));
        REQUIRE(arena.beginFrame(cmd));
        REQUIRE(gpu->mesh.cullScene(cmd, p, arena, 0));
        gpu->device.endOffscreen(cmd);
        const auto stats = gpu->mesh.readSceneView(0);
        gpu->device.releaseOffscreenResources();
        return stats;
    };

    const auto without = cullAll(false);
    const auto with = cullAll(true);
    CHECK(hzb.readTexel(0, 4, 4) == Approx(1.0f).margin(1e-6f));   // a corner: sky
    CHECK(hzb.readTexel(0, 64, 64) < 0.99f);                        // the middle: the wall
    CHECK(hzb.readTexel(hzb.levels() - 1, 0, 0) == Approx(1.0f).margin(1e-6f));  // the top level keeps the farthest
    REQUIRE(without.triangles > 0u);
    // Three instances, each ~20 000 triangles at level 0: the hidden one disappears, the others stay.
    CHECK(with.triangles < without.triangles);
    CHECK(with.triangles * 3 > without.triangles);                  // two of three remain
    CHECK(with.triangles * 3 < without.triangles * 2 + without.triangles / 4);
    CHECK(with.overflowed == 0u);
    hzb.shutdown();
    arena.shutdown();
}
