// Documentation renders of Micromeshes (hidden test: run with CAFFEINE_DOC_OUT=<dir> and the tag
// "[.micromesh-docs]"; scripts/render-micromesh-docs.sh does it and composes the images and the GIF).
// Frames come from the real GPU path: MicromeshGpu's cull compute pass and one indirect draw. The
// cluster id reaches the shader as gl_InstanceIndex (the cull pass writes it as `firstInstance`).

#include "catch.hpp"

#include "../src/caffeine/micromesh/Micromesh.hpp"
#include "../src/caffeine/render/MicromeshGpu.hpp"

#include <SDL3/SDL.h>
#include <stb/stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::RHI;
using namespace Caffeine::Micromesh;
using Caffeine::Render::MicromeshCullParams;
using Caffeine::Render::MicromeshGpu;
using Caffeine::Render::planesFromViewProj;

namespace {

f32 hash3(i32 x, i32 y, i32 z) {
    u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(y) * 668265263u + static_cast<u32>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<f32>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}

f32 noise3(const Vec3& p) {
    const i32 x = static_cast<i32>(std::floor(p.x)), y = static_cast<i32>(std::floor(p.y)), z = static_cast<i32>(std::floor(p.z));
    const f32 fx = p.x - static_cast<f32>(x), fy = p.y - static_cast<f32>(y), fz = p.z - static_cast<f32>(z);
    auto s = [](f32 t) { return t * t * (3.0f - 2.0f * t); };
    const f32 sx = s(fx), sy = s(fy), sz = s(fz);
    auto mixf = [](f32 a, f32 b, f32 t) { return a + (b - a) * t; };
    f32 c[2][2];
    for (int dy = 0; dy < 2; ++dy) {
        for (int dz = 0; dz < 2; ++dz) {
            c[dy][dz] = mixf(hash3(x, y + dy, z + dz), hash3(x + 1, y + dy, z + dz), sx);
        }
    }
    return mixf(mixf(c[0][0], c[1][0], sy), mixf(c[0][1], c[1][1], sy), sz);
}

/// A rock: a subdivided icosphere pushed in and out by layered noise, so the clusters have a shape worth looking at.
Data rock() {
    const f32 t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> positions;
    const Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (const Vec3& v : base) positions.push_back(v.normalized());
    std::vector<u32> indices = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2,
                                10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5,
                                2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int s = 0; s < 6; ++s) {  // 81 920 triangles
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
    for (Vec3& p : positions) {
        const f32 n = noise3(p * 1.6f) * 0.55f + noise3(p * 3.7f) * 0.28f + noise3(p * 9.0f) * 0.12f;
        p = p * (0.75f + n * 0.5f);
    }
    return build(positions, indices);
}

bool compile(const std::string& source, const char* stage, std::vector<u8>& out) {
    const auto dir = std::filesystem::temp_directory_path();
    const auto src = dir / (std::string("caffeine_mm_docs_") + stage + ".glsl");
    const auto spv = dir / (std::string("caffeine_mm_docs_") + stage + ".spv");
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

}  // namespace

TEST_CASE("Micromesh docs: cluster renders from the GPU path", "[.micromesh-docs]") {
    const char* outDir = std::getenv("CAFFEINE_DOC_OUT");
    if (!outDir) {
        SUCCEED("CAFFEINE_DOC_OUT not set");
        return;
    }
    constexpr u32 W = 640, H = 360;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    RenderDevice device;
    RenderConfig config;
    config.width = W;
    config.height = H;
    REQUIRE(device.initOffscreen(config));
    MicromeshGpu gpu;
    REQUIRE(gpu.init(&device));
    const Data data = rock();
    REQUIRE(gpu.upload(data));

    std::vector<u8> vs, fs;
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; } u;
layout(location = 0) out vec3 vWorld;
layout(location = 1) flat out uint vCluster;
void main() {
    vWorld = aPosition;
    vCluster = uint(gl_InstanceIndex);   // the cull pass wrote the cluster id as firstInstance
    gl_Position = u.uViewProj * vec4(aPosition, 1.0);
}
)", "vertex", vs));
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 vWorld;
layout(location = 1) flat in uint vCluster;
layout(location = 0) out vec4 outColor;
vec3 hsv(float h, float s, float v) {
    vec3 k = vec3(1.0, 2.0 / 3.0, 1.0 / 3.0);
    vec3 p = abs(fract(vec3(h) + k) * 6.0 - 3.0);
    return v * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), s);
}
void main() {
    uint h = vCluster * 2654435761u;
    float hue = float((h >> 8) & 0xFFFFu) / 65535.0;
    vec3 base = hsv(hue, 0.6, 1.0);
    vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    float light = 0.42 + 0.58 * abs(dot(n, normalize(vec3(0.4, 0.8, 0.5))));
    outColor = vec4(base * light, 1.0);
}
)", "fragment", fs));
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    Shader* vert = device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    Shader* frag = device.createShader(fd);
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
    Pipeline* pipeline = device.createGraphicsPipeline(vert, frag, pd);
    TextureDesc cd;
    cd.width = W;
    cd.height = H;
    cd.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* color = device.createTexture(cd);
    TextureDesc dd = cd;
    dd.format = TextureFormat::D32_FLOAT;
    dd.usage = TextureUsage::DepthStencil;
    Texture* depth = device.createTexture(dd);
    REQUIRE(pipeline);
    REQUIRE(color);
    REQUIRE(depth);

    const std::filesystem::path out(outDir);
    std::filesystem::create_directories(out);
    std::ofstream stats(out / "stats.json", std::ios::trunc);
    stats << "{\n  \"sourceTriangles\": " << data.sourceTriangles << ",\n  \"clusters\": " << data.clusters.size()
          << ",\n  \"levels\": " << data.levelCount << ",\n  \"frames\": {\n";
    bool firstEntry = true;

    auto frame = [&](const std::string& name, const Vec3& camera, f32 threshold) {
        const Mat4 viewProj = Mat4::perspective(0.7f, static_cast<f32>(W) / static_cast<f32>(H), 0.05f, 100.0f) *
                              Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
        MicromeshCullParams p;
        p.camera = camera;
        p.threshold = threshold;
        p.planes = planesFromViewProj(viewProj);
        CommandBuffer* cmd = device.beginOffscreen();
        REQUIRE(gpu.cull(cmd, p));
        RenderPassDesc pass;
        pass.colorTarget = color;
        pass.depthTarget = depth;
        pass.clearDepth = true;
        pass.clearColor[0] = 0.09f;
        pass.clearColor[1] = 0.1f;
        pass.clearColor[2] = 0.12f;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(pipeline);
        cmd->pushUniformData(ShaderStage::Vertex, 0, viewProj.data(), 64);
        gpu.draw(cmd);
        cmd->endRenderPass();
        device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(device.readTexture(color, pixels));
        const auto selected = gpu.readSelected();
        device.releaseOffscreenResources();
        u64 triangles = 0;
        u32 maxLevel = 0;
        for (u32 c : selected) {
            triangles += data.clusters[c].triangleCount;
            maxLevel = std::max(maxLevel, data.clusters[c].level);
        }
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        const std::string file = (out / (name + ".png")).string();
        REQUIRE(stbi_write_png(file.c_str(), static_cast<int>(W), static_cast<int>(H), 4, pixels.data(),
                               static_cast<int>(W * 4)) != 0);
        stats << (firstEntry ? "" : ",\n") << "    \"" << name << "\": {\"clusters\": " << selected.size()
              << ", \"triangles\": " << triangles << ", \"maxLevel\": " << maxLevel << "}";
        firstEntry = false;
    };

    // Four budgets at the same distance: the finest cut, then coarser and coarser.
    const Vec3 camera(2.6f, 0.9f, 1.7f);
    frame("cut_0", camera, 0.0f);
    frame("cut_1", camera, 0.004f);
    frame("cut_2", camera, 0.016f);
    frame("cut_3", camera, 0.06f);

    // The same budget while the camera pulls away: the cut coarsens by itself.
    constexpr int N = 72;
    for (int i = 0; i < N; ++i) {
        const f32 a = static_cast<f32>(i) / static_cast<f32>(N);
        const f32 distance = 1.7f + 11.0f * (0.5f - 0.5f * std::cos(a * 6.2831853f));
        const f32 angle = a * 6.2831853f * 0.5f + 0.6f;
        char name[32];
        std::snprintf(name, sizeof(name), "dolly_%03d", i);
        frame(name, Vec3(distance * std::cos(angle), 0.5f * distance * 0.35f + 0.3f, distance * std::sin(angle)), 0.004f);
    }
    stats << "\n  }\n}\n";
    stats.close();
    gpu.shutdown();
    device.shutdown();
}

// A field of rocks through the instanced GPU path: one hierarchy, 196 transforms, one cull dispatch and
// one indirect draw per frame. Writes field_*.png and field.json (visible triangles with the cut and
// with the exact mesh) for scripts/compose_micromesh_docs.py. Run with the tag "[.micromesh-field]".
TEST_CASE("Micromesh docs: a rock field from the instanced GPU path", "[.micromesh-field]") {
    const char* outDir = std::getenv("CAFFEINE_DOC_OUT");
    if (!outDir) {
        SUCCEED("CAFFEINE_DOC_OUT not set");
        return;
    }
    constexpr u32 W = 640, H = 360;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    RenderDevice device;
    RenderConfig config;
    config.width = W;
    config.height = H;
    REQUIRE(device.initOffscreen(config));
    MicromeshGpu gpu;
    REQUIRE(gpu.init(&device));
    const Data data = rock();
    REQUIRE(gpu.upload(data));

    // 14 x 14 rocks, jittered, random size and orientation, resting on the ground plane y = 0.
    constexpr int N = 14;
    std::vector<Mat4> instances;
    for (int ix = 0; ix < N; ++ix) {
        for (int iz = 0; iz < N; ++iz) {
            const f32 jx = hash3(ix, 3, iz) - 0.5f, jz = hash3(ix, 5, iz) - 0.5f;
            const f32 s = 0.8f + 0.9f * hash3(ix, 7, iz);
            const f32 x = (static_cast<f32>(ix) - (N - 1) * 0.5f) * 4.6f + jx * 2.6f;
            const f32 z = (static_cast<f32>(iz) - (N - 1) * 0.5f) * 4.6f + jz * 2.6f;
            instances.push_back(Mat4::translation(x, 0.55f * s, z) * Mat4::rotationY(6.2831853f * hash3(ix, 11, iz)) *
                                Mat4::rotationX(0.9f * (hash3(ix, 13, iz) - 0.5f)) *
                                Mat4::scale(s, s * (0.75f + 0.4f * hash3(ix, 17, iz)), s));
        }
    }
    // The scale of a matrix with a non-uniform scale is ambiguous: use a uniform one.
    for (Mat4& m : instances) {
        const f32 sx = Vec3(m(0, 0), m(1, 0), m(2, 0)).length();
        const f32 sy = Vec3(m(0, 1), m(1, 1), m(2, 1)).length();
        const f32 sz = Vec3(m(0, 2), m(1, 2), m(2, 2)).length();
        const f32 uniform = (sx + sy + sz) / 3.0f;
        for (usize r = 0; r < 3; ++r) {
            m(r, 0) *= uniform / sx;
            m(r, 1) *= uniform / sy;
            m(r, 2) *= uniform / sz;
        }
    }
    REQUIRE(gpu.setInstances(instances));

    std::vector<u8> vs, fs, gvs, gfs;
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 instances[]; };
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; uvec4 params; vec4 camera; } u;
layout(location = 0) out vec3 vWorld;
layout(location = 1) flat out uint vCluster;
layout(location = 2) flat out uint vInstance;
void main() {
    uint g = uint(gl_InstanceIndex);          // instance * clusterCount + cluster, written by the cull pass
    uint instance = g / u.params.x;
    vec4 r0 = instances[instance * 4u], r1 = instances[instance * 4u + 1u], r2 = instances[instance * 4u + 2u];
    vWorld = vec3(dot(r0.xyz, aPosition) + r0.w, dot(r1.xyz, aPosition) + r1.w, dot(r2.xyz, aPosition) + r2.w);
    vCluster = g % u.params.x;
    vInstance = instance;
    gl_Position = u.uViewProj * vec4(vWorld, 1.0);
}
)", "vertex", vs));
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 vWorld;
layout(location = 1) flat in uint vCluster;
layout(location = 2) flat in uint vInstance;
layout(set = 3, binding = 0) uniform F { uvec4 mode; vec4 camera; } f;
layout(location = 0) out vec4 outColor;
vec3 hsv(float h, float s, float v) {
    vec3 k = vec3(1.0, 2.0 / 3.0, 1.0 / 3.0);
    vec3 p = abs(fract(vec3(h) + k) * 6.0 - 3.0);
    return v * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), s);
}
float hash(uint x) { x = (x ^ 61u) ^ (x >> 16); x *= 9u; x ^= x >> 4; x *= 0x27d4eb2du; x ^= x >> 15; return float(x & 0xFFFFu) / 65535.0; }
void main() {
    vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    if (dot(n, f.camera.xyz - vWorld) < 0.0) n = -n;
    float light = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.5, 0.8, 0.35))), 0.0);
    vec3 base;
    if (f.mode.x == 1u) {
        base = hsv(hash(vCluster + vInstance * 7919u), 0.6, 1.0);
    } else {
        float t = hash(vInstance);
        base = mix(vec3(0.42, 0.40, 0.37), vec3(0.58, 0.52, 0.43), t) * (0.85 + 0.3 * hash(vInstance * 31u + 5u));
    }
    vec3 color = base * light;
    float fog = 1.0 - exp(-length(vWorld - f.camera.xyz) * 0.012);
    outColor = vec4(mix(color, vec3(0.66, 0.72, 0.80), fog), 1.0);
}
)", "fragment", fs));
    // The ground: two triangles generated in the vertex shader.
    REQUIRE(compile(R"(#version 450
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; uvec4 params; vec4 camera; } u;
layout(location = 0) out vec3 vWorld;
void main() {
    vec2 c[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vWorld = vec3(c[gl_VertexIndex].x * 400.0, 0.0, c[gl_VertexIndex].y * 400.0);
    gl_Position = u.uViewProj * vec4(vWorld, 1.0);
}
)", "vertex", gvs));
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 vWorld;
layout(set = 3, binding = 0) uniform F { uvec4 mode; vec4 camera; } f;
layout(location = 0) out vec4 outColor;
float h2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h2(i), h2(i + vec2(1, 0)), f.x), mix(h2(i + vec2(0, 1)), h2(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    float n = vnoise(vWorld.xz * 2.3) * 0.3 + vnoise(vWorld.xz * 0.45) * 0.4 + vnoise(vWorld.xz * 0.09) * 0.3;
    vec3 color = mix(vec3(0.30, 0.27, 0.22), vec3(0.37, 0.34, 0.27), n);
    if (f.mode.x == 1u) color = vec3(0.12, 0.13, 0.15);
    float fog = 1.0 - exp(-length(vWorld - f.camera.xyz) * 0.012);
    outColor = vec4(mix(color, vec3(0.66, 0.72, 0.80), fog), 1.0);
}
)", "fragment", gfs));
    auto makeShader = [&](const std::vector<u8>& code, ShaderStage stage, u32 uniforms, u32 storage) {
        ShaderDesc d;
        d.code = code.data();
        d.codeSize = code.size();
        d.stage = stage;
        d.numUniformBuffers = uniforms;
        d.numStorageBuffers = storage;
        return device.createShader(d);
    };
    Shader* vert = makeShader(vs, ShaderStage::Vertex, 1, 1);
    Shader* frag = makeShader(fs, ShaderStage::Fragment, 1, 0);
    Shader* gvert = makeShader(gvs, ShaderStage::Vertex, 1, 0);
    Shader* gfrag = makeShader(gfs, ShaderStage::Fragment, 1, 0);
    REQUIRE(vert);
    REQUIRE(frag);
    REQUIRE(gvert);
    REQUIRE(gfrag);
    VertexBufferLayoutDesc layout{0, sizeof(Vec3), false};
    VertexAttributeDesc attr{0, 0, VertexFormat::Float3, 0};
    GraphicsPipelineDesc pd;
    pd.vertexBuffers = &layout;
    pd.numVertexBuffers = 1;
    pd.attributes = &attr;
    pd.numAttributes = 1;
    pd.depthTest = true;
    pd.depthWrite = true;
    Pipeline* pipeline = device.createGraphicsPipeline(vert, frag, pd);
    GraphicsPipelineDesc gd;
    gd.depthTest = true;
    gd.depthWrite = true;
    Pipeline* ground = device.createGraphicsPipeline(gvert, gfrag, gd);
    TextureDesc cd;
    cd.width = W;
    cd.height = H;
    cd.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* color = device.createTexture(cd);
    TextureDesc dd = cd;
    dd.format = TextureFormat::D32_FLOAT;
    dd.usage = TextureUsage::DepthStencil;
    Texture* depth = device.createTexture(dd);
    REQUIRE(pipeline);
    REQUIRE(ground);
    REQUIRE(color);
    REQUIRE(depth);

    const std::filesystem::path out(outDir);
    std::filesystem::create_directories(out);
    std::ofstream stats(out / "field.json", std::ios::trunc);
    stats << "{\n  \"instances\": " << instances.size() << ",\n  \"sourceTriangles\": " << data.sourceTriangles
          << ",\n  \"clusters\": " << data.clusters.size() << ",\n  \"frames\": {\n";
    bool firstEntry = true;

    struct VertexPush {
        float viewProj[16];
        u32 params[4];
        float camera[4];
    };
    struct FragPush {
        u32 mode[4];
        float camera[4];
    };

    auto frame = [&](const std::string& name, const Vec3& camera, const Vec3& target, f32 threshold, u32 mode) {
        const Mat4 viewProj = Mat4::perspective(0.7f, static_cast<f32>(W) / static_cast<f32>(H), 0.05f, 400.0f) *
                              Mat4::lookAt(camera, target, Vec3(0, 1, 0));
        MicromeshCullParams p;
        p.camera = camera;
        p.planes = planesFromViewProj(viewProj);

        // What the exact mesh would draw (same frustum and back-face culling, zero error budget).
        p.threshold = 0.0f;
        CommandBuffer* exactCmd = device.beginOffscreen();
        REQUIRE(gpu.cull(exactCmd, p));
        device.endOffscreen(exactCmd);
        const MicromeshGpu::Stats exact = gpu.readStats();
        device.releaseOffscreenResources();

        p.threshold = threshold;
        VertexPush vp{};
        std::memcpy(vp.viewProj, viewProj.data(), sizeof(vp.viewProj));
        vp.params[0] = gpu.clusterCount();
        vp.camera[0] = camera.x;
        vp.camera[1] = camera.y;
        vp.camera[2] = camera.z;
        FragPush fp{};
        fp.mode[0] = mode;
        fp.camera[0] = camera.x;
        fp.camera[1] = camera.y;
        fp.camera[2] = camera.z;
        CommandBuffer* cmd = device.beginOffscreen();
        REQUIRE(gpu.cull(cmd, p));
        RenderPassDesc pass;
        pass.colorTarget = color;
        pass.depthTarget = depth;
        pass.clearDepth = true;
        pass.clearColor[0] = 0.66f;
        pass.clearColor[1] = 0.72f;
        pass.clearColor[2] = 0.80f;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(ground);
        cmd->pushUniformData(ShaderStage::Vertex, 0, &vp, sizeof(vp));
        cmd->pushUniformData(ShaderStage::Fragment, 0, &fp, sizeof(fp));
        cmd->draw(6);
        cmd->bindPipeline(pipeline);
        Buffer* storage[1] = {gpu.instanceBuffer()};
        cmd->bindVertexStorageBuffers(storage, 0, 1);
        cmd->pushUniformData(ShaderStage::Vertex, 0, &vp, sizeof(vp));
        cmd->pushUniformData(ShaderStage::Fragment, 0, &fp, sizeof(fp));
        gpu.draw(cmd);
        cmd->endRenderPass();
        device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(device.readTexture(color, pixels));
        const MicromeshGpu::Stats cut = gpu.readStats();
        device.releaseOffscreenResources();
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        const std::string file = (out / (name + ".png")).string();
        REQUIRE(stbi_write_png(file.c_str(), static_cast<int>(W), static_cast<int>(H), 4, pixels.data(),
                               static_cast<int>(W * 4)) != 0);
        stats << (firstEntry ? "" : ",\n") << "    \"" << name << "\": {\"clusters\": " << cut.visibleClusters
              << ", \"triangles\": " << cut.triangles << ", \"exactClusters\": " << exact.visibleClusters
              << ", \"exactTriangles\": " << exact.triangles << "}";
        firstEntry = false;
        // With a real view in front of it the cut draws far less than the exact mesh. (Nearly empty
        // frames can show a coarse cluster whose bigger bounds still touch the frustum.)
        if (threshold > 0.0f && exact.triangles > 1000000) CHECK(cut.triangles * 4 < exact.triangles);
    };

    // A flight along the field: low, looking ahead, swaying a little.
    constexpr int frames = 96;
    for (int i = 0; i < frames; ++i) {
        const f32 a = static_cast<f32>(i) / static_cast<f32>(frames - 1);
        const f32 z = 36.0f - 50.0f * a;
        const f32 x = 5.0f * std::sin(a * 6.2831853f * 1.5f);
        const Vec3 camera(x, 2.4f + 0.5f * std::sin(a * 6.2831853f * 3.0f), z);
        const Vec3 target(x * 0.5f, 1.2f, z - 14.0f);
        char name[32];
        std::snprintf(name, sizeof(name), "field_%03d", i);
        frame(name, camera, target, 0.004f, 0);
    }
    // Two stills from above and from the ground: shaded, and with a colour per cluster.
    const Vec3 high(0.0f, 24.0f, 36.0f);
    frame("still_shaded", high, Vec3(0, 0, 0), 0.004f, 0);
    frame("still_clusters", high, Vec3(0, 0, 0), 0.004f, 1);
    frame("still_exact", high, Vec3(0, 0, 0), 0.0f, 0);
    stats << "\n  }\n}\n";
    stats.close();
    gpu.shutdown();
    device.shutdown();
}

// Shading normals: the same four cuts drawn with the normals the build carried through the collapses
// and with per-triangle (facet) normals, and the mean error of each against the exact mesh. Writes
// normals_{smooth,facet}_<i>.png and normals.json. Run with the tag "[.micromesh-normals]".
TEST_CASE("Micromesh docs: carried normals against facet normals", "[.micromesh-normals]") {
    const char* outDir = std::getenv("CAFFEINE_DOC_OUT");
    if (!outDir) {
        SUCCEED("CAFFEINE_DOC_OUT not set");
        return;
    }
    constexpr u32 W = 480, H = 480;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    RenderDevice device;
    RenderConfig config;
    config.width = W;
    config.height = H;
    REQUIRE(device.initOffscreen(config));
    MicromeshGpu gpu;
    REQUIRE(gpu.init(&device));
    const Data data = rock();
    REQUIRE(gpu.upload(data));
    REQUIRE(gpu.normals() != nullptr);

    std::vector<u8> vs, fs;
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; } u;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
void main() { vWorld = aPosition; vNormal = aNormal; gl_Position = u.uViewProj * vec4(aPosition, 1.0); }
)", "vertex", vs));
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(set = 3, binding = 0) uniform F { vec4 mode; vec4 camera; } f;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 n = f.mode.x > 0.5 ? normalize(cross(dFdx(vWorld), dFdy(vWorld))) : normalize(vNormal);
    vec3 v = normalize(f.camera.xyz - vWorld);
    if (dot(n, v) < 0.0) n = -n;
    vec3 l = normalize(vec3(0.5, 0.8, 0.35));
    float diffuse = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, normalize(l + v)), 0.0), 36.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 base = vec3(0.50, 0.46, 0.40);
    vec3 color = base * (0.16 + 0.85 * diffuse) + vec3(0.35) * spec + vec3(0.10, 0.13, 0.18) * rim;
    outColor = vec4(color, 1.0);
}
)", "fragment", fs));
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    Shader* vert = device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    fd.numUniformBuffers = 1;
    Shader* frag = device.createShader(fd);
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
    Pipeline* pipeline = device.createGraphicsPipeline(vert, frag, pd);
    TextureDesc cd;
    cd.width = W;
    cd.height = H;
    cd.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* color = device.createTexture(cd);
    TextureDesc dd = cd;
    dd.format = TextureFormat::D32_FLOAT;
    dd.usage = TextureUsage::DepthStencil;
    Texture* depth = device.createTexture(dd);
    REQUIRE(pipeline);
    REQUIRE(color);
    REQUIRE(depth);

    const std::filesystem::path out(outDir);
    std::filesystem::create_directories(out);
    const Vec3 camera(2.6f, 0.9f, 1.7f);
    const Mat4 viewProj = Mat4::perspective(0.62f, 1.0f, 0.05f, 100.0f) *
                          Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
    auto render = [&](f32 threshold, f32 facet, const std::string& file, MicromeshGpu::Stats& statsOut) {
        MicromeshCullParams p;
        p.camera = camera;
        p.threshold = threshold;
        p.planes = planesFromViewProj(viewProj);
        const f32 mode[8] = {facet, 0, 0, 0, camera.x, camera.y, camera.z, 0};
        CommandBuffer* cmd = device.beginOffscreen();
        REQUIRE(gpu.cull(cmd, p));
        RenderPassDesc pass;
        pass.colorTarget = color;
        pass.depthTarget = depth;
        pass.clearDepth = true;
        pass.clearColor[0] = 0.09f;
        pass.clearColor[1] = 0.10f;
        pass.clearColor[2] = 0.12f;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(pipeline);
        cmd->pushUniformData(ShaderStage::Vertex, 0, viewProj.data(), 64);
        cmd->pushUniformData(ShaderStage::Fragment, 0, mode, sizeof(mode));
        gpu.drawLit(cmd);
        cmd->endRenderPass();
        device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(device.readTexture(color, pixels));
        statsOut = gpu.readStats();
        device.releaseOffscreenResources();
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        REQUIRE(stbi_write_png((out / file).string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, pixels.data(),
                               static_cast<int>(W * 4)) != 0);
        return pixels;
    };
    auto meanError = [](const std::vector<u8>& a, const std::vector<u8>& b) {
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = 0; i + 3 < a.size(); i += 4) {
            const bool bgA = a[i] == 23 && a[i + 1] == 26 && a[i + 2] == 31;
            const bool bgB = b[i] == 23 && b[i + 1] == 26 && b[i + 2] == 31;
            if (bgA && bgB) continue;
            sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) +
                   std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1])) +
                   std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
            ++n;
        }
        return n ? sum / (3.0 * static_cast<double>(n)) : 0.0;
    };

    MicromeshGpu::Stats exactStats;
    const auto reference = render(0.0f, 0.0f, "normals_smooth_0.png", exactStats);
    render(0.0f, 1.0f, "normals_facet_0.png", exactStats);
    std::ofstream json(out / "normals.json", std::ios::trunc);
    json << "{\n  \"sourceTriangles\": " << data.sourceTriangles << ",\n  \"cuts\": [\n";
    json << "    {\"threshold\": 0, \"triangles\": " << exactStats.triangles << ", \"smoothError\": 0, \"facetError\": 0}";
    const f32 thresholds[3] = {0.004f, 0.016f, 0.06f};
    for (int i = 0; i < 3; ++i) {
        MicromeshGpu::Stats s;
        const auto smooth = render(thresholds[i], 0.0f, "normals_smooth_" + std::to_string(i + 1) + ".png", s);
        MicromeshGpu::Stats sf;
        const auto facet = render(thresholds[i], 1.0f, "normals_facet_" + std::to_string(i + 1) + ".png", sf);
        const double es = meanError(smooth, reference), ef = meanError(facet, reference);
        json << ",\n    {\"threshold\": " << thresholds[i] << ", \"triangles\": " << s.triangles
             << ", \"smoothError\": " << es << ", \"facetError\": " << ef << "}";
        CHECK(es < ef);
    }
    json << "\n  ]\n}\n";
    json.close();
    gpu.shutdown();
    device.shutdown();
}

// Texture coordinates: a UV-mapped rock (a latitude / longitude sphere with a real seam, pushed in and
// out by noise) drawn at four cuts with (1) the carried normals, (2) the same plus a bump map evaluated
// from the UVs, and (3) a UV checker. Writes uv_{plain,bump,checker}_<i>.png and uv.json. Run with the
// tag "[.micromesh-uv]".
TEST_CASE("Micromesh docs: texture coordinates through the cuts", "[.micromesh-uv]") {
    const char* outDir = std::getenv("CAFFEINE_DOC_OUT");
    if (!outDir) {
        SUCCEED("CAFFEINE_DOC_OUT not set");
        return;
    }
    constexpr u32 W = 480, H = 480;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    RenderDevice device;
    RenderConfig config;
    config.width = W;
    config.height = H;
    REQUIRE(device.initOffscreen(config));
    MicromeshGpu gpu;
    REQUIRE(gpu.init(&device));

    // 256 x 160 quads, the first and last column duplicated: the u = 0 / 1 seam is a real seam.
    constexpr int segments = 256, rings = 160;
    constexpr f32 tau = 6.2831853f;
    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    for (int j = 0; j <= rings; ++j) {
        for (int i = 0; i <= segments; ++i) {
            const f32 u = static_cast<f32>(i) / segments, v = static_cast<f32>(j) / rings;
            const f32 phi = u * tau, theta = v * 3.14159265f;
            const Vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            const f32 n = noise3(dir * 1.6f) * 0.55f + noise3(dir * 3.7f) * 0.28f + noise3(dir * 9.0f) * 0.12f;
            positions.push_back(dir * (0.75f + n * 0.5f));
            uvs.push_back(Vec2(u, v));
        }
    }
    std::vector<u32> indices;
    auto at = [&](int i, int j) { return static_cast<u32>(j * (segments + 1) + i); };
    for (int j = 0; j < rings; ++j) {
        for (int i = 0; i < segments; ++i) {
            indices.insert(indices.end(), {at(i, j), at(i + 1, j), at(i, j + 1), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    }
    const Data data = build(positions, indices, uvs);
    REQUIRE(gpu.upload(data));
    REQUIRE(gpu.hasUvs());

    std::vector<u8> vs, fs;
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(set = 1, binding = 0) uniform U { mat4 uViewProj; } u;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
void main() { vWorld = aPosition; vNormal = aNormal; vUv = aUv; gl_Position = u.uViewProj * vec4(aPosition, 1.0); }
)", "vertex", vs));
    REQUIRE(compile(R"(#version 450
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(set = 3, binding = 0) uniform F { vec4 mode; vec4 camera; } f;
layout(location = 0) out vec4 outColor;
float hash(vec2 p) {
    uvec2 q = uvec2(ivec2(p));
    uint h = q.x * 1597334673u ^ q.y * 3812015801u;
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    h ^= h >> 16;
    return float(h & 0xFFFFFFu) / 16777215.0;
}
// Value noise that wraps in x with the given period, so the texture has no seam of its own.
float vnoise(vec2 p, float period) {
    vec2 i = floor(p), fr = fract(p);
    fr = fr * fr * (3.0 - 2.0 * fr);
    float x0 = mod(i.x, period), x1 = mod(i.x + 1.0, period);
    return mix(mix(hash(vec2(x0, i.y)), hash(vec2(x1, i.y)), fr.x), mix(hash(vec2(x0, i.y + 1.0)), hash(vec2(x1, i.y + 1.0)), fr.x), fr.y);
}
float height(vec2 uv) {
    vec2 p = vec2(uv.x * 48.0, uv.y * 30.0);
    return vnoise(p, 48.0) * 0.55 + vnoise(p * 2.0, 96.0) * 0.3 + vnoise(p * 4.0, 192.0) * 0.15;
}
void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(f.camera.xyz - vWorld);
    vec3 base = vec3(0.50, 0.46, 0.40);
    if (f.mode.x > 0.5 && f.mode.x < 1.5) {
        // Derivative bump mapping: no tangents needed, so it works on any cut.
        vec3 dpdx = dFdx(vWorld), dpdy = dFdy(vWorld);
        float h = height(vUv);
        float dhdx = dFdx(h), dhdy = dFdy(h);
        vec3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);
        float det = dot(dpdx, r1);
        vec3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
        n = normalize(abs(det) * n - 0.045 * grad);
        base *= 0.8 + 0.4 * h;
    } else if (f.mode.x > 1.5) {
        vec2 c = floor(vUv * vec2(24.0, 12.0));
        float checker = mod(c.x + c.y, 2.0);
        base = mix(vec3(0.85, 0.35, 0.25), vec3(0.93, 0.93, 0.88), checker);
        // The seam (u = 0 / 1) in blue, the first rows in green.
        if (vUv.x < 0.004 || vUv.x > 0.996) base = vec3(0.1, 0.35, 0.95);
    }
    vec3 l = normalize(vec3(0.5, 0.8, 0.35));
    float diffuse = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, normalize(l + v)), 0.0), 36.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    outColor = vec4(base * (0.16 + 0.85 * diffuse) + vec3(0.3) * spec + vec3(0.10, 0.13, 0.18) * rim, 1.0);
}
)", "fragment", fs));
    ShaderDesc vd;
    vd.code = vs.data();
    vd.codeSize = vs.size();
    vd.stage = ShaderStage::Vertex;
    vd.numUniformBuffers = 1;
    Shader* vert = device.createShader(vd);
    ShaderDesc fd;
    fd.code = fs.data();
    fd.codeSize = fs.size();
    fd.stage = ShaderStage::Fragment;
    fd.numUniformBuffers = 1;
    Shader* frag = device.createShader(fd);
    REQUIRE(vert);
    REQUIRE(frag);
    VertexBufferLayoutDesc layouts[3] = {{0, sizeof(Vec3), false}, {1, sizeof(Vec3), false}, {2, sizeof(Vec2), false}};
    VertexAttributeDesc attrs[3] = {{0, 0, VertexFormat::Float3, 0}, {1, 1, VertexFormat::Float3, 0}, {2, 2, VertexFormat::Float2, 0}};
    GraphicsPipelineDesc pd;
    pd.vertexBuffers = layouts;
    pd.numVertexBuffers = 3;
    pd.attributes = attrs;
    pd.numAttributes = 3;
    pd.depthTest = true;
    pd.depthWrite = true;
    Pipeline* pipeline = device.createGraphicsPipeline(vert, frag, pd);
    TextureDesc cd;
    cd.width = W;
    cd.height = H;
    cd.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
    Texture* color = device.createTexture(cd);
    TextureDesc dd = cd;
    dd.format = TextureFormat::D32_FLOAT;
    dd.usage = TextureUsage::DepthStencil;
    Texture* depth = device.createTexture(dd);
    REQUIRE(pipeline);
    REQUIRE(color);
    REQUIRE(depth);

    const std::filesystem::path out(outDir);
    std::filesystem::create_directories(out);
    const Vec3 camera(2.6f, 0.5f, 1.2f);
    const Mat4 viewProj = Mat4::perspective(0.62f, 1.0f, 0.05f, 100.0f) *
                          Mat4::lookAt(camera, Vec3(0, 0, 0), Vec3(0, 1, 0));
    auto render = [&](f32 threshold, f32 mode, const std::string& file, u64& triangles) {
        MicromeshCullParams p;
        p.camera = camera;
        p.threshold = threshold;
        p.planes = planesFromViewProj(viewProj);
        const f32 fragment[8] = {mode, 0, 0, 0, camera.x, camera.y, camera.z, 0};
        CommandBuffer* cmd = device.beginOffscreen();
        REQUIRE(gpu.cull(cmd, p));
        RenderPassDesc pass;
        pass.colorTarget = color;
        pass.depthTarget = depth;
        pass.clearDepth = true;
        pass.clearColor[0] = 0.09f;
        pass.clearColor[1] = 0.10f;
        pass.clearColor[2] = 0.12f;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(pipeline);
        cmd->pushUniformData(ShaderStage::Vertex, 0, viewProj.data(), 64);
        cmd->pushUniformData(ShaderStage::Fragment, 0, fragment, sizeof(fragment));
        gpu.drawTextured(cmd);
        cmd->endRenderPass();
        device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(device.readTexture(color, pixels));
        triangles = gpu.readStats().triangles;
        device.releaseOffscreenResources();
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        REQUIRE(stbi_write_png((out / file).string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, pixels.data(),
                               static_cast<int>(W * 4)) != 0);
        return pixels;
    };
    auto meanError = [](const std::vector<u8>& a, const std::vector<u8>& b) {
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = 0; i + 3 < a.size(); i += 4) {
            const bool bgA = a[i] == 23 && a[i + 1] == 26 && a[i + 2] == 31;
            const bool bgB = b[i] == 23 && b[i + 1] == 26 && b[i + 2] == 31;
            if (bgA && bgB) continue;
            sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) +
                   std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1])) +
                   std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
            ++n;
        }
        return n ? sum / (3.0 * static_cast<double>(n)) : 0.0;
    };

    const f32 thresholds[4] = {0.0f, 0.004f, 0.016f, 0.06f};
    u64 triangles[4] = {};
    std::vector<u8> bumpImages[4], plainImages[4], checkerImages[4];
    for (int i = 0; i < 4; ++i) {
        u64 t = 0;
        plainImages[i] = render(thresholds[i], 0.0f, "uv_plain_" + std::to_string(i) + ".png", triangles[i]);
        bumpImages[i] = render(thresholds[i], 1.0f, "uv_bump_" + std::to_string(i) + ".png", t);
        checkerImages[i] = render(thresholds[i], 2.0f, "uv_checker_" + std::to_string(i) + ".png", t);
    }
    std::ofstream json(out / "uv.json", std::ios::trunc);
    json << "{\n  \"sourceTriangles\": " << data.sourceTriangles << ",\n  \"cuts\": [\n";
    for (int i = 0; i < 4; ++i) {
        // Against the exact mesh with the bump map: what the plain carried normals miss, and what
        // the bump map evaluated on the carried UVs gets back.
        const double plain = meanError(plainImages[i], bumpImages[0]);
        const double bump = meanError(bumpImages[i], bumpImages[0]);
        const double checker = meanError(checkerImages[i], checkerImages[0]);
        json << (i ? ",\n" : "") << "    {\"threshold\": " << thresholds[i] << ", \"triangles\": " << triangles[i]
             << ", \"plainError\": " << plain << ", \"bumpError\": " << bump << ", \"checkerError\": " << checker << "}";
        if (i > 0) {
            CHECK(bump < plain);       // the UVs carried through the cut bring the detail back
            CHECK(checker < 14.0);     // and the checker barely moves: no texture swimming, no seam gap
        }
    }
    json << "\n  ]\n}\n";
    json.close();
    gpu.shutdown();
    device.shutdown();
}
