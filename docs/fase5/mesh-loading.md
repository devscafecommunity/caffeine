# 🗿 Mesh Loading

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Assets`  
> **Arquivo:** `src/assets/MeshLoader.hpp`  
> **Status:** 📅 Planejado  
> **RFs:** RF5.2, RF5.3

---

## Visão Geral

Carregamento de malhas 3D nos formatos `.obj` e `.gltf`, convertendo para o formato interno `.caf` (Mesh). Shaders HLSL (Windows Direct3D) / GLSL (Linux/macOS OpenGL via SDL_GPU).

---

## API Planejada

```cpp
namespace Caffeine::Assets {

// ============================================================================
// @brief  Formato de vértice 3D (48 bytes — SIMD-aligned a 32 bytes com padding).
//
//  offset  0: Vec3  position  (12 bytes)
//  offset 12: Vec3  normal    (12 bytes)
//  offset 24: Vec2  texcoord  ( 8 bytes)
//  offset 32: Vec4  tangent   (16 bytes)  — para normal mapping
//                              TOTAL: 48 bytes (align 16)
// ============================================================================
struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texcoord;
    Vec4 tangent;   // xyz = tangent, w = bitangent sign
};

// ============================================================================
// @brief  Sub-mesh — parte de uma mesh com material próprio.
// ============================================================================
struct SubMesh {
    u32           indexOffset;   // primeiro índice no index buffer
    u32           indexCount;
    u32           materialIndex;
    FixedString<64> name;
};

// ============================================================================
// @brief  Mesh 3D carregada.
// ============================================================================
struct Mesh3D {
    std::vector<Vertex3D> vertices;
    std::vector<u32>       indices;
    std::vector<SubMesh>  subMeshes;
    Rect3D                 bounds;      // AABB para frustum culling
    u32                    lodCount = 1;  // LODs futuros

    // GPU buffers (criados após upload)
    RHI::Buffer* vertexBuffer = nullptr;
    RHI::Buffer* indexBuffer  = nullptr;
};

// ============================================================================
// @brief  Material 3D.
// ============================================================================
struct Material3D {
    FixedString<64>  name;
    RHI::Texture*    albedoTexture   = nullptr;
    RHI::Texture*    normalTexture   = nullptr;
    RHI::Texture*    roughnessTexture = nullptr;
    Color             albedoColor    = Color::WHITE;
    f32               roughness      = 0.5f;
    f32               metallic       = 0.0f;
    RHI::Shader*      shader         = nullptr;
};

// ============================================================================
// @brief  Loader de meshes .obj e .gltf.
//
//  Fluxo:
//  1. Carregar .obj/.gltf → Mesh3D (vertices, indices)
//  2. Converter para .caf (AssetType::Mesh) via Asset Pipeline (Fase 6)
//  3. Em runtime: BlobLoader carrega .caf → upload GPU
// ============================================================================
class MeshLoader {
public:
    explicit MeshLoader(RHI::RenderDevice* device,
                        AssetManager* assets);

    // Carregamento assíncrono (preferido em runtime)
    AssetHandle<Mesh3D> loadAsync(const char* cafPath);

    // Carregamento síncrono (para ferramentas, não para runtime)
    Mesh3D* loadOBJ(const char* objPath);
    Mesh3D* loadGLTF(const char* gltfPath);

    // Upload da mesh para GPU (chama após loadOBJ/loadGLTF)
    void uploadToGPU(Mesh3D* mesh);

private:
    RHI::RenderDevice* m_device;
    AssetManager*       m_assets;
};

}  // namespace Caffeine::Assets

// ============================================================================
// @brief  Sistema de renderização de meshes 3D.
// ============================================================================
namespace Caffeine::Render {

class MeshSystem : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32  priority() const override { return 900; }   // antes do render flush
    const char* name() const override { return "MeshSystem"; }

private:
    void submitMesh(const Assets::Mesh3D& mesh,
                    const Math::Mat4& worldMatrix,
                    RHI::CommandBuffer* cmd);
};

}  // namespace Caffeine::Render
```

---

## Shader System (RF5.3)

```cpp
namespace Caffeine::Render {

// ============================================================================
// @brief  Compilação e gerenciamento de shaders.
//
//  Plataforma:
//  - Windows:    HLSL → compilado com dxc → SPIR-V via SDL_GPU
//  - Linux/macOS: GLSL → compilado com glslc → SPIR-V via SDL_GPU
// ============================================================================
class ShaderSystem {
public:
    RHI::Shader* loadVertex(const char* path);
    RHI::Shader* loadFragment(const char* path);
    RHI::Pipeline* createPipeline(RHI::Shader* vert,
                                   RHI::Shader* frag,
                                   const RHI::PipelineDesc& desc);

    // Hot-reload de shaders (dev mode)
    void reloadShader(const char* path);
    void pollChanges();
};

}  // namespace Caffeine::Render
```

---

## Componente ECS para Mesh

```cpp
namespace Caffeine::Components {

struct MeshRenderer {
    FixedString<128>        meshPath;
    Assets::AssetHandle<Assets::Mesh3D> mesh;
    Assets::Material3D*     material = nullptr;
    bool                    castShadows = true;
    bool                    receiveShadows = true;
};

}  // namespace Caffeine::Components
```

---

## Exemplos de Uso

```cpp
// ── Carregar mesh ─────────────────────────────────────────────
Caffeine::Assets::MeshLoader meshLoader(&device, &assets);
auto meshHandle = meshLoader.loadAsync("meshes/player.caf");

// ── Criar entidade 3D ─────────────────────────────────────────
Entity player3D = world.create("Player3D");
world.add<Position3D>(player3D, {0, 0, 0});
world.add<Rotation3D>(player3D);
world.add<Scale3D>(player3D, {1, 1, 1});
world.add<MeshRenderer>(player3D, { .meshPath = "meshes/player.caf" });

// ── Shader customizado ────────────────────────────────────────
auto* shaderSys = world.getSystem<ShaderSystem>();
auto* vert   = shaderSys->loadVertex("shaders/pbr.vert.spv");
auto* frag   = shaderSys->loadFragment("shaders/pbr.frag.spv");
auto* pipeline = shaderSys->createPipeline(vert, frag, {});
// Assign ao material da mesh
```

---

## Critério de Aceitação

- [ ] Mesh .obj com 10K triângulos carregada e renderizada corretamente
- [ ] Mesh .gltf com materiais PBR básicos funcional
- [ ] 60fps com mesh + shader customizado
- [ ] Vertex buffer alinhado a 32 bytes para SIMD

---

## Dependências

- **Upstream:** [Asset Manager](../fase3/asset-manager.md), [Formato .caf](../fase3/caf-format.md), [RHI](../fase3/rhi.md)
- **Downstream:** [Skeletal Animation](skeletal-animation.md), [Fase 6 — Asset Pipeline](../fase6/asset-pipeline.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §15 Math Library
- [`docs/fase3/rhi.md`](../fase3/rhi.md) — Buffer/Shader creation
