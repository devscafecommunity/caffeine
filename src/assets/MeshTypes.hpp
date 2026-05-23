#pragma once
#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "containers/FixedString.hpp"
#include <vector>

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#endif

namespace Caffeine::Assets {
using namespace Caffeine;

struct Color {
    f32 r, g, b, a = 1.0f;
    
    static Color white() { return Color{1.0f, 1.0f, 1.0f, 1.0f}; }
    static Color black() { return Color{0.0f, 0.0f, 0.0f, 1.0f}; }
};

struct Rect3D {
    Vec3 min, max;
    
    Vec3 center() const { 
        return Vec3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f);
    }
    
    Vec3 extents() const {
        return Vec3((max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f);
    }
};

struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texcoord;
    Vec4 tangent;
};

struct SubMesh {
    u32 indexOffset = 0;
    u32 indexCount = 0;
    u32 materialIndex = 0;
    FixedString<64> name;
};

struct Mesh3D {
    std::vector<Vertex3D> vertices;
    std::vector<u32> indices;
    std::vector<SubMesh> subMeshes;
    Rect3D bounds;
    u32 lodCount = 1;
    
    std::vector<u8> baseColorTexture;
    u32 textureWidth = 0;
    u32 textureHeight = 0;
    
#ifdef CF_HAS_SDL3
    RHI::Buffer* vertexBuffer = nullptr;
    RHI::Buffer* indexBuffer = nullptr;
#endif
};

struct Material3D {
    FixedString<64> name;
    Color albedoColor = Color::white();
    f32 roughness = 0.5f;
    f32 metallic = 0.0f;
    
#ifdef CF_HAS_SDL3
    RHI::Texture* albedoTexture = nullptr;
    RHI::Texture* normalTexture = nullptr;
    RHI::Texture* roughnessTexture = nullptr;
    RHI::Shader* shader = nullptr;
#endif
};

struct MeshRenderer {
    FixedString<128> meshPath;
    Mesh3D* mesh = nullptr;
    Material3D* material = nullptr;
    bool castShadows = true;
    bool receiveShadows = true;
};

}  // namespace Caffeine::Assets
