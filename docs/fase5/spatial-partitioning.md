# 🌳 Spatial Partitioning

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Spatial`  
> **Arquivo:** `src/spatial/Octree.hpp`  
> **Status:** ✅ Implementado  
> **RF:** RF5.4

---

## Visão Geral

Spatial partitioning divide o espaço 3D em regiões para acelerar queries espaciais (broad-phase collision, frustum culling, raycasts). Migra do **Quadtree** 2D (Fase 4, se implementado) para **Octree** 3D.

---

## API Planejada

```cpp
namespace Caffeine::Spatial {

// ============================================================================
// @brief  AABB 3D — caixa alinhada por eixos.
// ============================================================================
struct AABB3D {
    Vec3 min = {0, 0, 0};
    Vec3 max = {0, 0, 0};

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size()   const { return max - min; }
    bool contains(Vec3 p) const;
    bool intersects(const AABB3D& o) const;
    bool intersectsRay(Vec3 origin, Vec3 dir, f32* tMin = nullptr) const;
};

// ============================================================================
// @brief  Frustum para culling.
//
//  Planos: Near, Far, Left, Right, Top, Bottom.
//  Convenção: d ≤ 0 → dentro, d > 0 → fora.
// ============================================================================
struct Frustum {
    Vec4 planes[6];   // Ax + By + Cz + D = 0 (normalizados)

    // fromCamera() é o método recomendado — funciona com qualquer
    // convenção de projeção pois constrói os planos da geometria.
    // fromMatrix() requer matriz VP estilo OpenGL padrão.
    static Frustum fromCamera(Vec3 eye, Vec3 target, Vec3 up,
                               f32 fovY, f32 aspect, f32 nearZ, f32 farZ);
    static Frustum fromMatrix(const Mat4& viewProj);

    bool contains(Vec3 point)      const;
    bool intersects(const AABB3D& aabb) const;
    bool containsSphere(Vec3 center, f32 radius) const;
};

// ============================================================================
// @brief  Octree para particionamento 3D.
//
//  - Cada nó divide em 8 sub-nós quando excede maxEntities
//  - Depth máxima configurável (default: 8)
//  - Entidades inseridas por AABB
// ============================================================================
class Octree {
public:
    struct Config {
        AABB3D rootBounds;
        u32    maxEntitiesPerNode = 8;
        u32    maxDepth           = 8;
    };

    explicit Octree(const Config& cfg);

    // ── Insert / Remove ────────────────────────────────────────
    void insert(ECS::Entity entity, const AABB3D& bounds);
    void remove(ECS::Entity entity);
    void update(ECS::Entity entity, const AABB3D& newBounds);
    void rebuild();   // rebuild completo (para quando muitas entidades moveram)

    // ── Queries ────────────────────────────────────────────────
    void queryAABB(const AABB3D& bounds,
                   std::vector<ECS::Entity>& out) const;

    void queryFrustum(const Frustum& frustum,
                      std::vector<ECS::Entity>& out) const;   // RF5.6

    void queryRay(Vec3 origin, Vec3 dir, f32 maxDist,
                  std::vector<ECS::Entity>& out) const;

    void queryRadius(Vec3 center, f32 radius,
                     std::vector<ECS::Entity>& out) const;

    // ── Stats ──────────────────────────────────────────────────
    u32 nodeCount()   const;
    u32 entityCount() const;
    u32 depth()       const;

private:
    struct Node {
        AABB3D bounds;
        std::vector<ECS::Entity>      entities;
        std::unique_ptr<Node>          children[8];
        bool                           isLeaf = true;
        u32                            depth  = 0;

        void subdivide();
        int  childIndex(Vec3 point) const;
    };

    Node m_root;
    Config m_config;
};

}  // namespace Caffeine::Spatial
```

---

## Frustum Culling (RF5.6)

```
Câmera (eye, target, up, fov, aspect, near, far)
        │
        ▼
  Frustum::fromCamera(eye, target, up, fovY, aspect, nearZ, farZ)
        │
        ▼
  Octree::queryFrustum(frustum, visibleEntities)
        │
        ▼
  Apenas entidades DENTRO do frustum são enviadas ao MeshSystem
  (entidades fora não geram draw calls)
```

> **Nota:** `Frustum::fromMatrix()` está disponível para matrizes VP estilo
> OpenGL padrão, mas não funciona com a `Mat4::perspective()` da Caffeine
> (que possui layout Z/W trocado). Prefira `fromCamera()` sempre que possível.

**Benefício:** Em uma cena com 10K objetos, apenas os ~100 visíveis geram draw calls.

---

## Quadtree 2D (Fase 4 → 5)

A Fase 4 pode usar um **Quadtree** 2D para broad-phase collision 2D (pré-requisito do Octree):

```cpp
// Fase 4 (opcional, para otimizar Physics2D com muitas entidades):
class Quadtree {
    void insert(ECS::Entity, Rect2D bounds);
    void query(Rect2D area, std::vector<ECS::Entity>& out);
};

// Fase 5: Octree substitui/estende para 3D
```

---

## Exemplos de Uso

```cpp
// ── Setup ─────────────────────────────────────────────────────
Caffeine::Spatial::Octree octree({
    .rootBounds = { {-1000,-1000,-1000}, {1000,1000,1000} },
    .maxEntitiesPerNode = 8,
    .maxDepth = 8
});

// ── Popular com entidades ─────────────────────────────────────
world.query(meshQuery, [&](ECS::Entity e, Position3D& pos, MeshRenderer& mr) {
    AABB3D bounds = computeBounds(mr.mesh, pos);
    octree.insert(e, bounds);
});

// ── Frustum culling ───────────────────────────────────────────
Frustum frustum = Frustum::fromCamera(
    camera3D.position(), camera3D.target(), camera3D.up(),
    camera3D.fovY(), camera3D.aspect(), camera3D.nearZ(), camera3D.farZ()
);
std::vector<ECS::Entity> visible;
octree.queryFrustum(frustum, visible);

// Apenas renderizar entidades visíveis:
for (ECS::Entity e : visible) {
    auto* mr = world.get<MeshRenderer>(e);
    meshSystem.submitMesh(*mr->mesh, getWorldMatrix(e), cmd);
}

// ── Update quando entidades movem ────────────────────────────
if (entity.get<Position3D>().changed) {
    octree.update(entity, newBounds);
}
```

---

## Critério de Aceitação

- [x] `AABB3D` — center, size, contains (point/AABB), intersects, intersectsRay
- [x] `Frustum::fromCamera` — construção robusta a partir de parâmetros de câmera
- [x] `Frustum::contains` / `intersects` / `containsSphere` — testes com pontos, AABBs e esferas
- [x] `Octree` — insert, remove, update, rebuild, queries (AABB/Frustum/Ray/Radius)
- [x] `Octree` — subdivisão automática, profundidade máxima, contagem consistente
- [ ] Performance bench: 10K entidades, query em < 1ms (benchmark pendente)
- [ ] Integração com Camera3D + MeshSystem para culling em draw calls

---

## Dependências

- **Upstream:** [Fase 4 — ECS](../fase4/ecs.md), [3D Math](3d-math.md)
- **Downstream:** [Camera3D](camera3d.md) (fornece Frustum), [Mesh Loading](mesh-loading.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — RF5.4 Octree spatial
- [`docs/fase5/camera3d.md`](camera3d.md) — fornece Frustum para queryFrustum
