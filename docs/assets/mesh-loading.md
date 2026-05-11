# 🗿 Mesh Loading

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Assets`  
> **Arquivo:** `src/assets/MeshLoader.hpp`  
> **Status:** ✅ Implementado  
> **RFs:** RF5.2, RF5.3

---

## Visão Geral

Carregamento de malhas 3D nos formatos `.obj` e `.gltf`, convertendo para o formato interno `.caf` (Mesh). Shaders HLSL (Windows Direct3D) / GLSL (Linux/macOS OpenGL via SDL_GPU).

---

## API Implementada

```cpp
namespace Caffeine::Assets {

struct Color {
    f32 r, g, b, a = 1.0f;
    static Color white();
    static Color black();
};

struct Rect3D {
    Vec3 min, max;
    Vec3 center() const;
    Vec3 extents() const;
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

class MeshLoader {
public:
    MeshLoader() = default;
#ifdef CF_HAS_SDL3
    explicit MeshLoader(RHI::RenderDevice* device);
#endif

    static Mesh3D* fromMemory(const Vertex3D* verts, u32 vertCount, 
                              const u32* indices, u32 indexCount);
    static Mesh3D* parseOBJ(const char* src, usize srcLen);
    Mesh3D* loadOBJ(const char* path);
    
#ifdef CF_HAS_SDL3
    void uploadToGPU(Mesh3D* mesh);
#endif
};

class MeshSystem : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override;
};

}  // namespace Caffeine::Assets

namespace Caffeine::ECS {

struct Position3D { Vec3 position; };
struct Rotation3D { Vec4 quaternion = Vec4(0.0f, 0.0f, 0.0f, 1.0f); };
struct Scale3D { Vec3 scale = Vec3(1.0f, 1.0f, 1.0f); };

}  // namespace Caffeine::ECS
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
// ── Carregar mesh OBJ ─────────────────────────────────────────
Caffeine::Assets::MeshLoader meshLoader;
Mesh3D* mesh = meshLoader.loadOBJ("models/player.obj");

// ── Criar entidade 3D ─────────────────────────────────────────
Entity player3D = world.create();
world.add<Position3D>(player3D, {Vec3(0.0f, 0.0f, 0.0f)});
world.add<Rotation3D>(player3D);
world.add<Scale3D>(player3D, {Vec3(1.0f, 1.0f, 1.0f)});
world.add<MeshRenderer>(player3D, MeshRenderer{});

// ── Sistema de renderização ───────────────────────────────────
MeshSystem meshSystem;
meshSystem.onUpdate(world, 0.016f);
```

---

## Critério de Aceitação

- [x] Vertex3D struct definido (position, normal, texcoord, tangent)
- [x] Mesh3D struct com vertices, indices, subMeshes, bounds
- [x] MeshLoader::fromMemory cria mesh de vértices e índices
- [x] MeshLoader::parseOBJ lê formato .obj (v, vt, vn, f)
- [x] MeshLoader::loadOBJ carrega arquivo do disco
- [x] MeshSystem::onUpdate integrado com ECS
- [x] Componentes 3D (Position3D, Rotation3D, Scale3D)
- [x] 22+ testes cobrindo todos os componentes
- [x] Compila sem SDL3 (CPU-only path)
- [x] GPU upload guardado com #ifdef CF_HAS_SDL3

---

## Dependências

- **Upstream:** [Asset Manager](../assets/asset-manager.md), [Formato .caf](../assets/caf-format.md), [RHI](../rendering/rhi.md)
- **Downstream:** [Skeletal Animation](skeletal-animation.md), [Fase 6 — Asset Pipeline](../assets/asset-pipeline.md)

---

## Referências

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Renderização & Pipeline]() | Meshes e shaders na pipeline 3D |
| [Assets & Pipeline]() | Mesh loading integrado ao pipeline de assets |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §15 Math Library
- [`rendering/rhi.md`](../rendering/rhi.md) — Buffer/Shader creation
- [Índice de Tópicos Transversais]()
