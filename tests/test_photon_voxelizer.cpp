#include "catch.hpp"

#include "../src/caffeine/render/GpuTextureCache.hpp"
#include "../src/caffeine/render/PhotonCascades.hpp"
#include "../src/caffeine/rhi/CommandBuffer.hpp"
#include "../src/caffeine/rhi/RenderDevice.hpp"

#include <SDL3/SDL.h>
#include <stb/stb_image_write.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::RHI;
using Caffeine::Render::PhotonCascades;
using Caffeine::Render::PhotonBox;
using Caffeine::Render::PhotonGeometry;
using Caffeine::Render::PhotonObject;
using Caffeine::Render::PhotonTriangle;

// Photon's conservative triangle voxeliser (photon_voxelize_tri.comp): the scene's triangles are written
// into the far-field material volume, including the ones the camera never sees. Needs a GPU (skipped
// otherwise).

namespace {

struct Gpu {
    RenderDevice device;
    PhotonCascades photon;
    bool ok = false;

    Gpu() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
        RenderConfig config;
        config.width = 64;
        config.height = 64;
        if (!device.initOffscreen(config)) return;
        if (!photon.init(&device)) return;
        ok = photon.hasTriangleVoxelizer();
    }
    ~Gpu() {
        photon.shutdown(&device);
        device.shutdown();
    }

    /// Runs the voxeliser for a camera and reads the material volume back.
    std::vector<u8> volume(const Vec3& camera) {
        CommandBuffer* cmd = device.beginOffscreen();
        REQUIRE(photon.updateGeometryVoxels(cmd, camera));
        device.endOffscreen(cmd);
        std::vector<u8> pixels;
        REQUIRE(device.readTexture(photon.voxelAlbedo(), pixels));
        device.releaseOffscreenResources();
        return pixels;
    }
};

bool compileCompute(const std::string& source, std::vector<u8>& out) {
    const auto dir = std::filesystem::temp_directory_path();
    const auto src = dir / "caffeine_photon_test.comp";
    const auto spv = dir / "caffeine_photon_test.spv";
    {
        std::ofstream file(src);
        file << source;
    }
    const std::string cmd = "glslc -fshader-stage=compute \"" + src.string() + "\" -o \"" + spv.string() +
                            "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return false;
    std::ifstream in(spv, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

/// Reads a float texture (RGBA16F) back as RGBA8 of `value * scale` through a small compute pass:
/// the RHI reads back 4-byte pixels only.
bool readScaled(Gpu& gpu, Texture* source, u32 width, u32 height, f32 scale, std::vector<u8>& pixels) {
    std::vector<u8> code;
    if (!compileCompute(R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(set = 1, binding = 0, rgba8) uniform image2D uDst;
layout(set = 2, binding = 0) uniform P { vec4 scale; } p;
void main() {
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    if (t.x >= int(p.scale.y) || t.y >= int(p.scale.z)) return;
    vec4 v = texelFetch(uSrc, t, 0);
    imageStore(uDst, t, vec4(clamp(v.rgb * p.scale.x, 0.0, 1.0), 1.0));
}
)", code)) {
        return false;
    }
    ComputePipelineDesc desc;
    desc.code = code.data();
    desc.codeSize = code.size();
    desc.threadCountX = 8;
    desc.threadCountY = 8;
    desc.numSamplers = 1;
    desc.numReadwriteStorageTextures = 1;
    desc.numUniformBuffers = 1;
    ComputePipeline* pipeline = gpu.device.createComputePipeline(desc);
    SamplerDesc sd;
    sd.linearFilter = false;
    Sampler* sampler = gpu.device.createSampler(sd);
    TextureDesc td;
    td.width = width;
    td.height = height;
    td.usage = TextureUsage::Sampler | TextureUsage::ColorTarget | TextureUsage::ComputeWrite;
    Texture* target = gpu.device.createTexture(td);
    if (!pipeline || !sampler || !target) return false;
    const f32 params[4] = {scale, static_cast<f32>(width), static_cast<f32>(height), 0.0f};
    CommandBuffer* cmd = gpu.device.beginOffscreen();
    cmd->beginComputePass(target);
    cmd->bindComputePipeline(pipeline);
    Texture* textures[1] = {source};
    Sampler* samplers[1] = {sampler};
    cmd->bindComputeTextures(textures, samplers, 0, 1);
    cmd->pushComputeUniformData(0, params, sizeof(params));
    cmd->dispatch((width + 7) / 8, (height + 7) / 8, 1);
    cmd->endComputePass();
    gpu.device.endOffscreen(cmd);
    const bool ok = gpu.device.readTexture(target, pixels);
    gpu.device.releaseOffscreenResources();
    gpu.device.destroyTexture(target);
    gpu.device.destroySampler(sampler);
    gpu.device.destroyComputePipeline(pipeline);
    return ok;
}

PhotonTriangle triangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& albedo) {
    PhotonTriangle t{};
    t.p0[0] = a.x; t.p0[1] = a.y; t.p0[2] = a.z;
    t.p1[0] = b.x; t.p1[1] = b.y; t.p1[2] = b.z;
    t.p2[0] = c.x; t.p2[1] = c.y; t.p2[2] = c.z;
    t.albedo[0] = albedo.x; t.albedo[1] = albedo.y; t.albedo[2] = albedo.z;
    return t;
}

constexpr u32 kAtlasWidth = 2048;

/// The texel of voxel (x, y, z) of `level` in the atlas.
usize texel(u32 level, u32 x, u32 y, u32 z) {
    const u32 px = level * 512 + (z % 8) * 64 + x;
    const u32 py = (z / 8) * 64 + y;
    return (static_cast<usize>(py) * kAtlasWidth + px) * 4;
}

}  // namespace

TEST_CASE("The triangle voxeliser fills the voxels a triangle touches", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    // A 2 m x 2 m floor at y = 1 (two triangles), in front of a camera at the origin.
    const Vec3 colour(0.8f, 0.2f, 0.1f);
    std::vector<PhotonTriangle> floor = {
        triangle({-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, colour),
        triangle({-1, 1, -1}, {1, 1, 1}, {-1, 1, 1}, colour),
    };
    REQUIRE_FALSE(gpu->photon.hasGeometry());
    REQUIRE(gpu->photon.setGeometry(1, floor));
    REQUIRE(gpu->photon.hasGeometry());

    const Vec3 camera(0, 0, 0);
    const std::vector<u8> pixels = gpu->volume(camera);
    REQUIRE(pixels.size() >= static_cast<usize>(kAtlasWidth) * 512 * 4);

    const Vec3 origin = gpu->photon.farClipOrigin(0);
    const f32 cell = gpu->photon.farCellSize(0);
    auto voxelAt = [&](const Vec3& p) {
        const u32 x = static_cast<u32>(std::floor((p.x - origin.x) / cell));
        const u32 y = static_cast<u32>(std::floor((p.y - origin.y) / cell));
        const u32 z = static_cast<u32>(std::floor((p.z - origin.z) / cell));
        return texel(0, x, y, z);
    };

    // Inside the floor: coverage 1 and the triangle's colour.
    for (const Vec3 p : {Vec3(0.1f, 1.0f, 0.1f), Vec3(-0.8f, 1.0f, 0.7f), Vec3(0.7f, 1.0f, -0.7f)}) {
        const usize i = voxelAt(p);
        INFO("point " << p.x << " " << p.y << " " << p.z);
        CHECK(pixels[i + 3] == 255);
        CHECK(pixels[i + 0] == Approx(204).margin(2));
        CHECK(pixels[i + 1] == Approx(51).margin(2));
        CHECK(pixels[i + 2] == Approx(26).margin(2));
    }
    // Away from the floor: empty (above it, below it, beside it).
    for (const Vec3 p : {Vec3(0.1f, 2.5f, 0.1f), Vec3(0.1f, -0.5f, 0.1f), Vec3(3.0f, 1.0f, 0.1f)}) {
        CHECK(pixels[voxelAt(p) + 3] == 0);
    }
    // The coarser levels hold it too (the cell of level 3 is 2 m wide: the floor is inside one).
    const Vec3 origin3 = gpu->photon.farClipOrigin(3);
    const f32 cell3 = gpu->photon.farCellSize(3);
    const u32 x3 = static_cast<u32>(std::floor((0.0f - origin3.x) / cell3));
    const u32 y3 = static_cast<u32>(std::floor((1.0f - origin3.y) / cell3));
    const u32 z3 = static_cast<u32>(std::floor((0.0f - origin3.z) / cell3));
    CHECK(pixels[texel(3, x3, y3, z3) + 3] == 255);
}

TEST_CASE("A triangle behind the camera, out of its view, is in the volume", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    // A wall 3 m behind a camera that looks the other way: no screen would ever see it.
    const Vec3 colour(0.1f, 0.6f, 0.2f);
    REQUIRE(gpu->photon.setGeometry(
        7, {triangle({-2, 0, 3}, {2, 0, 3}, {2, 3, 3}, colour), triangle({-2, 0, 3}, {2, 3, 3}, {-2, 3, 3}, colour)}));
    const std::vector<u8> pixels = gpu->volume(Vec3(0, 1, 0));
    const Vec3 origin = gpu->photon.farClipOrigin(0);
    const f32 cell = gpu->photon.farCellSize(0);
    const u32 x = static_cast<u32>(std::floor((0.0f - origin.x) / cell));
    const u32 y = static_cast<u32>(std::floor((1.5f - origin.y) / cell));
    const u32 z = static_cast<u32>(std::floor((3.0f - origin.z) / cell));
    CHECK(pixels[texel(0, x, y, z) + 3] == 255);
}

TEST_CASE("A level is rebuilt when its clipmap moves and the old voxels are cleared", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const Vec3 colour(0.5f, 0.5f, 0.5f);
    REQUIRE(gpu->photon.setGeometry(
        3, {triangle({-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, colour), triangle({-1, 0, -1}, {1, 0, 1}, {-1, 0, 1}, colour)}));
    auto count = [](const std::vector<u8>& pixels) {
        usize n = 0;
        for (u32 y = 0; y < 512; ++y) {
            for (u32 x = 0; x < 512; ++x) {  // level 0 only
                n += pixels[(static_cast<usize>(y) * kAtlasWidth + x) * 4 + 3] == 255 ? 1 : 0;
            }
        }
        return n;
    };
    const usize near = count(gpu->volume(Vec3(0, 1, 0)));
    CHECK(near > 30);  // 2 m x 2 m of 0.25 m cells: at least 8 x 8
    // A camera 500 m away: the floor is far outside the level-0 volume (16 m), which is cleared.
    CHECK(count(gpu->volume(Vec3(500, 1, 0))) == 0);
    // And back again.
    CHECK(count(gpu->volume(Vec3(0, 1, 0))) == near);
}

TEST_CASE("The injection lights a wall with the sun and keeps it out of its own shadow", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    // A red wall facing -z at z = 2 (two triangles), the sun travelling toward +z and down: it faces the sun.
    const Vec3 colour(0.8f, 0.2f, 0.1f);
    REQUIRE(gpu->photon.setGeometry(
        5, {triangle({-2, 0, 2}, {2, 0, 2}, {2, 3, 2}, colour), triangle({-2, 0, 2}, {2, 3, 2}, {-2, 3, 2}, colour)}));
    const Vec3 camera(0, 1, 0);
    {
        // Create the far-field targets, empty them, then voxelise into the empty volume.
        CommandBuffer* cmd = gpu->device.beginOffscreen();
        REQUIRE(gpu->photon.updateGeometryVoxels(cmd, camera));
        gpu->photon.clearFarTargets(cmd);
        gpu->device.endOffscreen(cmd);
        gpu->device.releaseOffscreenResources();
    }
    Render::PhotonLights lights;
    lights.directionalCount = 1;
    lights.directionalDir[0] = Vec4(0.0f, -0.5f, 0.866f, 0.0f);
    lights.directionalColor[0] = Vec4(2.0f, 2.0f, 2.0f, 0.0f);
    gpu->photon.setLights(lights);
    {
        CommandBuffer* cmd = gpu->device.beginOffscreen();
        REQUIRE(gpu->photon.updateGeometryVoxels(cmd, camera));
        REQUIRE(gpu->photon.injectRadiance(cmd, 0.0f));
        gpu->device.endOffscreen(cmd);
        gpu->device.releaseOffscreenResources();
    }
    std::vector<u8> z;
    if (!readScaled(*gpu, gpu->photon.voxelRadiance(2), 4096, 512, 0.25f, z)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    const Vec3 origin = gpu->photon.farClipOrigin(0);
    const f32 cell = gpu->photon.farCellSize(0);
    // The voxel just in front of the wall (z in [1.75, 2.0)), its -z face (side 1), at x = 0, y = 1.
    const u32 x = static_cast<u32>(std::floor((0.0f - origin.x) / cell));
    const u32 y = static_cast<u32>(std::floor((1.0f - origin.y) / cell));
    const u32 zi = static_cast<u32>(std::floor((1.9f - origin.z) / cell));
    const u32 ax = (zi % 8) * 64 + x;
    const u32 ay = (zi / 8) * 64 + y;
    const usize face = (static_cast<usize>(ay) * 4096 + 2 * ax + 1) * 4;
    // B = albedo * radiance * N.L = (0.8, 0.2, 0.1) * 2 * 0.866, shown at a quarter scale.
    const f32 expected = 2.0f * 0.866f * 0.25f * 255.0f;
    INFO("texel " << int(z[face]) << " " << int(z[face + 1]) << " " << int(z[face + 2]));
    CHECK(z[face + 0] == Approx(0.8f * expected).margin(10));
    CHECK(z[face + 1] == Approx(0.2f * expected).margin(10));
    CHECK(z[face + 2] == Approx(0.1f * expected).margin(10));
}

namespace {

/// Axis-aligned box as twelve triangles.
void addBox(std::vector<PhotonTriangle>& out, const Vec3& lo, const Vec3& hi, const Vec3& colour) {
    const Vec3 v[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                       {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int f[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
                          {3, 2, 6}, {3, 6, 7}, {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};
    for (auto& t : f) out.push_back(triangle(v[t[0]], v[t[1]], v[t[2]], colour));
}

/// Voxelises `triangles` for a camera, empties the far field, voxelises again, injects `lights` and `sky`, and
/// traces the probes until their history has settled.
void settle(Gpu& gpu, u64 hash, const std::vector<PhotonTriangle>& triangles, const Vec3& camera,
            const Render::PhotonLights& lights, f32 sky) {
    REQUIRE(gpu.photon.setGeometry(hash, triangles));
    {
        CommandBuffer* cmd = gpu.device.beginOffscreen();
        REQUIRE(gpu.photon.updateGeometryVoxels(cmd, camera));
        gpu.photon.clearFarTargets(cmd);
        gpu.device.endOffscreen(cmd);
        gpu.device.releaseOffscreenResources();
    }
    gpu.photon.setLights(lights);
    CommandBuffer* cmd = gpu.device.beginOffscreen();
    REQUIRE(gpu.photon.updateGeometryVoxels(cmd, camera));
    for (u32 frame = 0; frame < 40; ++frame) {
        REQUIRE(gpu.photon.injectRadiance(cmd, sky));
        REQUIRE(gpu.photon.traceProbes(cmd, frame, sky));
    }
    gpu.device.endOffscreen(cmd);
    gpu.device.releaseOffscreenResources();
}

Render::PhotonLights sunFromBehindTheWall() {
    Render::PhotonLights lights;
    lights.directionalCount = 1;
    lights.directionalDir[0] = Vec4(0.0f, -0.77f, 0.64f, 0.0f);  // travels toward +z and down
    lights.directionalColor[0] = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
    return lights;
}

}  // namespace

TEST_CASE("A far-field probe in front of a sunlit red wall sees it in red", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    std::vector<PhotonTriangle> tris;
    addBox(tris, {-5.0f, 0.0f, 3.0f}, {5.0f, 5.0f, 3.2f}, {0.8f, 0.2f, 0.1f});
    settle(*gpu, 8, tris, Vec3(0, 1.25f, 2.25f), sunFromBehindTheWall(), 0.0f);
    std::vector<u8> atlas;
    if (!readScaled(*gpu, gpu->photon.probeRadiance(), 640, 2560, 0.5f, atlas)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    // The level-0 probe at about (0, 1, 2), a metre in front of the wall: toward +z (the middle of its tile)
    // the wall fills the view, red, at the strength of its lit face times cos^2 (0.8 x 0.64 x 0.5 scale).
    const Vec3 origin = gpu->photon.farProbeOrigin(0);
    const f32 spacing = gpu->photon.farProbeSpacing(0);
    const u32 qx = static_cast<u32>(std::floor((0.0f - origin.x) / spacing));
    const u32 qy = static_cast<u32>(std::floor((1.0f - origin.y) / spacing));
    const u32 qz = static_cast<u32>(std::floor((2.0f - origin.z) / spacing));
    const u32 baseX = qx * 10;
    const u32 baseY = (qz * 16 + qy) * 10;
    int red = 0, green = 0;
    for (u32 ty = 4; ty <= 5; ++ty) {
        for (u32 tx = 4; tx <= 5; ++tx) {
            const usize i = (static_cast<usize>(baseY + ty) * 640 + (baseX + tx)) * 4;
            red = std::max<int>(red, atlas[i]);
            green = std::max<int>(green, atlas[i + 1]);
        }
    }
    INFO("probe (" << qx << "," << qy << "," << qz << ") toward +z: red " << red << " green " << green);
    CHECK(red > 40);
    CHECK(red > green * 3);  // albedo (0.8, 0.2, 0.1)
}

TEST_CASE("The probes inside a closed room do not see the sky", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const Vec3 white(0.8f, 0.8f, 0.8f);
    std::vector<PhotonTriangle> tris;
    addBox(tris, {-3.2f, -0.2f, -3.2f}, {3.2f, 0.0f, 3.2f}, white);   // floor
    addBox(tris, {-3.2f, 3.0f, -3.2f}, {3.2f, 3.2f, 3.2f}, white);    // ceiling
    addBox(tris, {-3.2f, 0.0f, -3.2f}, {3.2f, 3.0f, -3.0f}, white);   // back
    addBox(tris, {-3.2f, 0.0f, 3.0f}, {3.2f, 3.0f, 3.2f}, white);     // front
    addBox(tris, {-3.2f, 0.0f, -3.0f}, {-3.0f, 3.0f, 3.0f}, white);   // left
    addBox(tris, {3.0f, 0.0f, -3.0f}, {3.2f, 3.0f, 3.0f}, white);     // right
    // The sky at 0.65 and a sun that only reaches the roof.
    settle(*gpu, 9, tris, Vec3(0, 1.8f, 2.5f), sunFromBehindTheWall(), 0.65f);
    std::vector<u8> atlas;
    if (!readScaled(*gpu, gpu->photon.probeRadiance(), 640, 2560, 1.0f, atlas)) {
        SUCCEED("glslc unavailable: skipped");
        return;
    }
    const Vec3 origin = gpu->photon.farProbeOrigin(0);
    const f32 spacing = gpu->photon.farProbeSpacing(0);
    double sum = 0;
    int probes = 0;
    for (u32 qz = 0; qz < 16; ++qz) {
        for (u32 qy = 0; qy < 16; ++qy) {
            for (u32 qx = 0; qx < 16; ++qx) {
                const Vec3 q(origin.x + (qx + 0.5f) * spacing, origin.y + (qy + 0.5f) * spacing,
                             origin.z + (qz + 0.5f) * spacing);
                if (q.x < -2.8f || q.x > 2.8f || q.y < 0.3f || q.y > 2.7f || q.z < -2.8f || q.z > 2.8f) continue;
                double tile = 0;
                for (u32 ty = 1; ty < 9; ++ty) {
                    for (u32 tx = 1; tx < 9; ++tx) {
                        const usize i = (static_cast<usize>((qz * 16 + qy) * 10 + ty) * 640 + (qx * 10 + tx)) * 4;
                        tile += atlas[i + 2];  // blue: the sky is bluest
                    }
                }
                sum += tile / 64.0;
                ++probes;
            }
        }
    }
    INFO("probes in the room " << probes << ", mean blue " << (probes ? sum / probes : 0));
    CHECK(probes > 10);
    CHECK(sum / std::max(probes, 1) < 6.0);
}

TEST_CASE("The mean colour of a picture stands in for a textured surface", "[photon][voxelizer]") {
    // Half the picture is (200, 100, 50), half is (0, 100, 150), all opaque.
    const auto path = std::filesystem::temp_directory_path() / "caffeine_photon_average.png";
    std::vector<u8> pixels(8 * 8 * 4);
    for (int i = 0; i < 64; ++i) {
        const bool left = (i % 8) < 4;
        pixels[i * 4 + 0] = left ? 200 : 0;
        pixels[i * 4 + 1] = 100;
        pixels[i * 4 + 2] = left ? 50 : 150;
        pixels[i * 4 + 3] = 255;
    }
    REQUIRE(stbi_write_png(path.string().c_str(), 8, 8, 4, pixels.data(), 8 * 4));
    Vec3 average(0, 0, 0);
    REQUIRE(Render::GpuTextureCache::instance().averageColor(path.string(), "", average));
    // The mean of the linear values, not of the sRGB bytes.
    auto linear = [](f32 v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    CHECK(average.x == Approx(0.5f * linear(200.0f / 255.0f)).margin(0.01f));
    CHECK(average.y == Approx(linear(100.0f / 255.0f)).margin(0.01f));
    CHECK(average.z == Approx(0.5f * (linear(50.0f / 255.0f) + linear(150.0f / 255.0f))).margin(0.01f));
    Vec3 none;
    CHECK_FALSE(Render::GpuTextureCache::instance().averageColor("/no/such/picture.png", "", none));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

namespace {

/// One object of a geometry set: its triangles, its bounds and a hash that changes when it moves.
void addObject(PhotonGeometry& geometry, u64 key, u64 hash, const std::vector<PhotonTriangle>& triangles,
               const Vec3& lo, const Vec3& hi) {
    PhotonObject object;
    object.key = key;
    object.hash = hash;
    object.first = static_cast<u32>(geometry.triangles.size());
    object.count = static_cast<u32>(triangles.size());
    object.lo = lo;
    object.hi = hi;
    geometry.triangles.insert(geometry.triangles.end(), triangles.begin(), triangles.end());
    geometry.objects.push_back(object);
}

std::vector<PhotonTriangle> boxTriangles(const Vec3& lo, const Vec3& hi, const Vec3& colour) {
    std::vector<PhotonTriangle> out;
    addBox(out, lo, hi, colour);
    return out;
}

usize covered(const std::vector<u8>& pixels) {
    usize n = 0;
    for (usize i = 3; i < pixels.size(); i += 4) n += pixels[i] == 255 ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("A moving object revoxelises its own box and leaves the other levels alone", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const Vec3 camera(0, 1, 0);
    const Vec3 grey(0.5f, 0.5f, 0.5f);
    const std::vector<PhotonTriangle> floor = boxTriangles({-4, -0.1f, -4}, {4, 0.0f, 4}, grey);
    auto scene = [&](f32 x, u64 hash) {
        PhotonGeometry g;
        addObject(g, 1, 11, floor, {-4, -0.1f, -4}, {4, 0.0f, 4});
        const Vec3 lo(x - 0.3f, 0.0f, 1.0f), hi(x + 0.3f, 0.6f, 1.6f);
        addObject(g, 2, hash, boxTriangles(lo, hi, {0.9f, 0.1f, 0.1f}), lo, hi);
        return g;
    };

    // The first set builds every level.
    REQUIRE(gpu->photon.setGeometry(scene(-1.0f, 21), {}, true));
    const std::vector<u8> first = gpu->volume(camera);
    CHECK(gpu->photon.stats().lastLevelMask == 0xFu);
    CHECK(gpu->photon.stats().objects == 2);
    const usize before = covered(first);
    REQUIRE(before > 0);

    // Nothing changed: the next update has nothing to do.
    CHECK(gpu->volume(camera) == first);
    CHECK(gpu->photon.stats().lastLevelMask == 0u);
    CHECK(gpu->photon.stats().lastRegions == 0u);

    // The box moves 2 m: only the boxes where it was and where it is get cleared and voxelised.
    const PhotonBox was{{-1.3f, 0.0f, 1.0f}, {-0.7f, 0.6f, 1.6f}};
    const PhotonBox now{{0.7f, 0.0f, 1.0f}, {1.3f, 0.6f, 1.6f}};
    REQUIRE(gpu->photon.setGeometry(scene(1.0f, 22), {was, now}, false));
    const std::vector<u8> moved = gpu->volume(camera);
    CHECK(gpu->photon.stats().lastLevelMask == 0u);
    CHECK(gpu->photon.stats().lastRegions == 2u);
    CHECK(moved != first);

    // The result is what a rebuild from scratch gives, voxel for voxel.
    gpu->photon.requestFullRebuild();
    const std::vector<u8> scratch = gpu->volume(camera);
    CHECK(gpu->photon.stats().lastLevelMask == 0xFu);
    CHECK(moved == scratch);
    CHECK(covered(moved) > 0);

    // And so does moving it back.
    REQUIRE(gpu->photon.setGeometry(scene(-1.0f, 21), {was, now}, false));
    CHECK(gpu->volume(camera) == first);
}

TEST_CASE("Past the triangle cap the volume holds nothing and says so", "[photon][voxelizer]") {
    auto gpu = std::make_unique<Gpu>();
    if (!gpu->ok) {
        SUCCEED("no GPU: skipped");
        return;
    }
    const Vec3 camera(0, 1, 0);
    PhotonGeometry g;
    addObject(g, 1, 5, boxTriangles({-1, 0, -1}, {1, 1, 1}, {0.5f, 0.5f, 0.5f}), {-1, 0, -1}, {1, 1, 1});
    REQUIRE(gpu->photon.setGeometry(g, {}, true));
    CHECK(covered(gpu->volume(camera)) > 0);
    CHECK(gpu->photon.stats().overBudget == 0u);

    // Over budget the caller sends objects and bounds but no triangles.
    g.triangles.clear();
    g.overBudget = true;
    REQUIRE(gpu->photon.setGeometry(g, {}, true));
    CHECK(covered(gpu->volume(camera)) == 0);
    CHECK(gpu->photon.stats().overBudget == 1u);
    CHECK(gpu->photon.stats().triangles == 0u);
}
