#include "catch.hpp"

#include <rhi/CommandBuffer.hpp>
#include <rhi/RenderDevice.hpp>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::RHI;

// GPU-driven rendering building blocks of the RHI: indirect draws, graphics storage buffers and
// compute passes that write draw arguments. They need a real device (the headless one the editor
// uses); the tests skip where there is no GPU or no `glslc` to compile their tiny shaders.

namespace {

bool compileGlsl(const std::string& source, const char* stage, std::vector<u8>& out) {
    const auto dir = std::filesystem::temp_directory_path();
    const auto src = dir / (std::string("caffeine_rhi_test_") + stage + ".glsl");
    const auto spv = dir / (std::string("caffeine_rhi_test_") + stage + ".spv");
    {
        std::ofstream file(src);
        file << source;
    }
    const std::string cmd = std::string("glslc -fshader-stage=") + stage + " \"" + src.string() +
                            "\" -o \"" + spv.string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return false;
    std::ifstream in(spv, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

struct Harness {
    RenderDevice device;
    bool ok = false;
    Shader* vert = nullptr;
    Shader* frag = nullptr;
    Pipeline* pipeline = nullptr;
    Texture* target = nullptr;
    Buffer* verts = nullptr;

    // One big triangle that covers the whole target, read from a storage buffer.
    Harness() {
        RenderConfig config;
        config.width = 64;
        config.height = 64;
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
        if (!device.initOffscreen(config)) return;
        std::vector<u8> vs;
        std::vector<u8> fs;
        if (!compileGlsl(R"(#version 450
layout(std430, set = 0, binding = 0) readonly buffer Verts { vec4 pos[]; };
void main() { gl_Position = pos[gl_VertexIndex]; }
)", "vertex", vs)) return;
        if (!compileGlsl(R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 0.0, 0.0, 1.0); }
)", "fragment", fs)) return;
        ShaderDesc vd;
        vd.code = vs.data();
        vd.codeSize = vs.size();
        vd.stage = ShaderStage::Vertex;
        vd.numStorageBuffers = 1;
        vert = device.createShader(vd);
        ShaderDesc fd;
        fd.code = fs.data();
        fd.codeSize = fs.size();
        fd.stage = ShaderStage::Fragment;
        frag = device.createShader(fd);
        if (!vert || !frag) return;
        GraphicsPipelineDesc desc;
        desc.colorFormat = TextureFormat::R8G8B8A8_UNORM;
        desc.depthTest = false;
        desc.depthWrite = false;
        pipeline = device.createGraphicsPipeline(vert, frag, desc);
        TextureDesc td;
        td.width = 64;
        td.height = 64;
        td.usage = TextureUsage::ColorTarget | TextureUsage::Sampler;
        target = device.createTexture(td);
        const float positions[12] = {-1.0f, -1.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
                                     -1.0f, 3.0f,  0.0f, 1.0f};
        BufferDesc bd;
        bd.size = sizeof(positions);
        verts = device.createBuffer(bd, BufferUsage::GraphicsStorage);
        if (verts) device.uploadBuffer(verts, positions, sizeof(positions));
        ok = pipeline && target && verts;
    }
};

std::vector<u8> centrePixel(RenderDevice& device, Texture* target) {
    std::vector<u8> pixels;
    device.readTexture(target, pixels);
    if (pixels.size() < 64u * 64u * 4u) return {};
    const size_t i = (32u * 64u + 32u) * 4u;
    return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
}

}  // namespace

TEST_CASE("RHI: the device reports what GPU-driven rendering can use", "[rhi][gpu]") {
    RenderDevice device;
    CHECK_FALSE(device.capabilities().indirectDraw);  // nothing before init
    RenderConfig config;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO) || !device.initOffscreen(config)) {
        SUCCEED("no GPU device available");
        return;
    }
    const DeviceCapabilities caps = device.capabilities();
    CHECK(caps.indirectDraw);
    CHECK(caps.indirectDispatch);
    CHECK(caps.graphicsStorageBuffers);
    CHECK(caps.computeStorageWrite);
    CHECK_FALSE(caps.meshShaders);   // SDL3 GPU has none: Micromeshes use hardware indirect draws
    CHECK_FALSE(caps.atomics64);
    CHECK(caps.maxIndirectDrawCount >= 1u);
}

TEST_CASE("RHI: indirect draw arguments decide what is drawn", "[rhi][gpu]") {
    auto h = std::make_unique<Harness>();
    if (!h->ok) {
        SUCCEED("no GPU, glslc or pipeline support: skipped");
        return;
    }
    auto drawWith = [&](u32 instances) {
        const IndirectDrawArgs args{3, instances, 0, 0};
        BufferDesc bd;
        bd.size = sizeof(args);
        Buffer* buffer = h->device.createBuffer(bd, BufferUsage::Indirect);
        REQUIRE(buffer != nullptr);
        REQUIRE(h->device.uploadBuffer(buffer, &args, sizeof(args)));
        CommandBuffer* cmd = h->device.beginOffscreen();
        RenderPassDesc pass;
        pass.colorTarget = h->target;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(h->pipeline);
        Buffer* storage[1] = {h->verts};
        cmd->bindVertexStorageBuffers(storage, 0, 1);
        cmd->drawIndirect(buffer, 0, 1);
        cmd->endRenderPass();
        h->device.endOffscreen(cmd);
        auto pixel = centrePixel(h->device, h->target);
        h->device.releaseOffscreenResources();
        h->device.destroyBuffer(buffer);
        return pixel;
    };
    const auto drawn = drawWith(1);
    REQUIRE(drawn.size() == 4);
    CHECK(drawn[0] == 255);  // the triangle was drawn
    CHECK(drawn[1] == 0);
    const auto none = drawWith(0);
    REQUIRE(none.size() == 4);
    CHECK(none[0] == 0);     // zero instances in the arguments: nothing is drawn
}

TEST_CASE("RHI: a compute pass writes the draw arguments (GPU culling in miniature)", "[rhi][gpu]") {
    auto h = std::make_unique<Harness>();
    if (!h->ok) {
        SUCCEED("no GPU, glslc or pipeline support: skipped");
        return;
    }
    std::vector<u8> cs;
    if (!compileGlsl(R"(#version 450
layout(local_size_x = 1) in;
layout(std430, set = 0, binding = 0) readonly buffer Flags { uint flags[]; };
layout(std430, set = 1, binding = 0) buffer Args { uint args[]; };
void main() {
    args[0] = 3u;                          // vertexCount
    args[1] = flags[0] != 0u ? 1u : 0u;    // instanceCount: visible or culled
    args[2] = 0u;
    args[3] = 0u;
}
)", "compute", cs)) {
        SUCCEED("glslc cannot compile compute shaders here: skipped");
        return;
    }
    ComputePipelineDesc cd;
    cd.code = cs.data();
    cd.codeSize = cs.size();
    cd.threadCountX = 1;
    cd.threadCountY = 1;
    cd.threadCountZ = 1;
    cd.numReadonlyStorageBuffers = 1;
    cd.numReadwriteStorageBuffers = 1;
    cd.numUniformBuffers = 0;
    ComputePipeline* compute = h->device.createComputePipeline(cd);
    REQUIRE(compute != nullptr);

    auto run = [&](u32 visible) {
        BufferDesc flagsDesc;
        flagsDesc.size = sizeof(u32);
        Buffer* flags = h->device.createBuffer(flagsDesc, BufferUsage::StorageRead);
        BufferDesc argsDesc;
        argsDesc.size = sizeof(IndirectDrawArgs);
        Buffer* args = h->device.createBuffer(argsDesc, BufferUsage::IndirectWrite);
        REQUIRE(flags);
        REQUIRE(args);
        REQUIRE(h->device.uploadBuffer(flags, &visible, sizeof(visible)));
        CommandBuffer* cmd = h->device.beginOffscreen();
        Buffer* rw[1] = {args};
        cmd->beginComputePass(nullptr, 0, rw, 1);
        cmd->bindComputePipeline(compute);
        Buffer* ro[1] = {flags};
        cmd->bindComputeStorageBuffers(ro, 0, 1);
        cmd->dispatch(1, 1, 1);
        cmd->endComputePass();
        RenderPassDesc pass;
        pass.colorTarget = h->target;
        cmd->beginRenderPass(pass);
        cmd->bindPipeline(h->pipeline);
        Buffer* storage[1] = {h->verts};
        cmd->bindVertexStorageBuffers(storage, 0, 1);
        cmd->drawIndirect(args, 0, 1);
        cmd->endRenderPass();
        h->device.endOffscreen(cmd);
        auto pixel = centrePixel(h->device, h->target);
        std::vector<u8> written;
        h->device.readBuffer(args, written);
        h->device.releaseOffscreenResources();
        h->device.destroyBuffer(flags);
        h->device.destroyBuffer(args);
        return std::make_pair(pixel, written);
    };
    const auto [visiblePixel, visibleArgs] = run(1);
    REQUIRE(visiblePixel.size() == 4);
    CHECK(visiblePixel[0] == 255);
    REQUIRE(visibleArgs.size() == sizeof(IndirectDrawArgs));
    IndirectDrawArgs read{};
    std::memcpy(&read, visibleArgs.data(), sizeof(read));
    CHECK(read.vertexCount == 3u);
    CHECK(read.instanceCount == 1u);
    const auto [culledPixel, culledArgs] = run(0);
    REQUIRE(culledPixel.size() == 4);
    CHECK(culledPixel[0] == 0);  // the compute pass culled it
    std::memcpy(&read, culledArgs.data(), sizeof(read));
    CHECK(read.instanceCount == 0u);
}
