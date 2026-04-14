# 🎥 Camera 3D

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Render`  
> **Arquivo:** `src/render/Camera3D.hpp`  
> **Status:** 📅 Planejado  
> **RFs:** RF5.5, RF5.6

---

## Visão Geral

Camera3D com projeção perspectiva para renderização 3D. Coexiste com `Camera2D` (Fase 3) — o mesmo frame pode renderizar 2D (HUD, sprites) e 3D (ambiente, personagens) com câmeras diferentes.

---

## API Planejada

```cpp
namespace Caffeine::Render {

// ============================================================================
// @brief  Câmera 3D com projeção perspectiva.
//
//  Modos:
//  - FPS: mouse look, WASD movement
//  - Orbital: rotação em torno de um ponto (editor, estratégia)
//  - Follow: segue entidade com offset (terceira pessoa)
// ============================================================================
class Camera3D {
public:
    Camera3D();

    // ── Matrizes ───────────────────────────────────────────────
    Math::Mat4 viewMatrix()           const;
    Math::Mat4 projectionMatrix()     const;
    Math::Mat4 viewProjectionMatrix() const;   // cached, dirty-flagged

    // Frustum para culling (RF5.6)
    Spatial::Frustum frustum() const;

    // ── Conversão de espaço ────────────────────────────────────
    Math::Vec3 worldToScreen(Math::Vec3 worldPos) const;
    Math::Vec3 screenToWorld(Math::Vec2 screenPos, f32 depth = 0.5f) const;
    bool       isVisible(const Spatial::AABB3D& bounds) const;

    // ── Configuração perspectiva ────────────────────────────────
    void setFOV(f32 fovYDegrees);
    void setAspect(f32 aspect);
    void setNearFar(f32 nearPlane, f32 farPlane);

    // ── Transform ──────────────────────────────────────────────
    void setPosition(Math::Vec3 pos);
    void setRotation(Math::Quat rot);
    void lookAt(Math::Vec3 eye, Math::Vec3 target,
                Math::Vec3 up = {0, 1, 0});

    // ── FPS mode ───────────────────────────────────────────────
    void rotateFPS(f32 deltaPitch, f32 deltaYaw);  // graus
    void moveFPS(Math::Vec3 localDelta);           // espaço local

    // ── Orbital mode ───────────────────────────────────────────
    void orbit(f32 deltaAzimuth, f32 deltaElevation);  // graus
    void setOrbitTarget(Math::Vec3 target);
    void setOrbitDistance(f32 distance);
    void zoom(f32 delta);

    // ── Follow mode ────────────────────────────────────────────
    void follow(ECS::Entity target, Math::Vec3 offset = {0, 2, -5},
                f32 smoothing = 0.05f);
    void update(f64 dt, const ECS::World& world);

    // ── Getters ────────────────────────────────────────────────
    Math::Vec3 position()  const { return m_position; }
    Math::Quat rotation()  const { return m_rotation; }
    Math::Vec3 forward()   const;
    Math::Vec3 right()     const;
    Math::Vec3 up()        const;
    f32        fov()       const { return m_fovY; }

private:
    Math::Mat4 calculateViewMatrix()       const;
    Math::Mat4 calculateProjectionMatrix() const;

    Math::Vec3 m_position    = {0, 0, -5};
    Math::Quat m_rotation    = Math::Quat::identity();
    f32         m_fovY        = 60.0f;
    f32         m_aspect      = 16.0f / 9.0f;
    f32         m_near        = 0.1f;
    f32         m_far         = 1000.0f;

    // Follow
    ECS::Entity m_followTarget    = ECS::Entity::INVALID;
    Math::Vec3  m_followOffset    = {0, 2, -5};
    f32          m_followSmoothing = 0.05f;

    // Orbital
    Math::Vec3 m_orbitTarget   = {0, 0, 0};
    f32         m_orbitDistance = 5.0f;
    f32         m_azimuth       = 0.0f;    // graus
    f32         m_elevation     = 30.0f;   // graus

    mutable Math::Mat4 m_cachedVP;
    mutable bool        m_dirty = true;
};

}  // namespace Caffeine::Render
```

---

## Matemática da Projeção Perspectiva

```
Projection Matrix (perspective):
    f = 1 / tan(fovY/2)
    
    [f/aspect,  0,      0,                0]
    [0,         f,      0,                0]
    [0,         0,  far/(far-near),       1]
    [0,         0, -far*near/(far-near),  0]

View Matrix (lookAt):
    forward = normalize(target - eye)
    right   = normalize(cross(forward, up))
    up      = cross(right, forward)
    
    Mat4 lookAt = rotation(right, up, -forward) * translate(-eye)
```

---

## Frustum Culling Integration

```cpp
// Camera3D fornece Frustum para o Octree:
Spatial::Frustum frustum = camera3D.frustum();
octree.queryFrustum(frustum, visibleEntities);
// → apenas visibleEntities geram draw calls
```

---

## Exemplos de Uso

```cpp
// ── Setup câmera perspectiva ──────────────────────────────────
Caffeine::Render::Camera3D camera;
camera.setFOV(60.0f);
camera.setAspect(1280.0f / 720.0f);
camera.setNearFar(0.1f, 1000.0f);
camera.lookAt({0, 5, -10}, {0, 0, 0});

// ── FPS camera ────────────────────────────────────────────────
camera.rotateFPS(input.axisValue(Axis::LookY) * sensitivity,
                  input.axisValue(Axis::LookX) * sensitivity);

Vec3 moveDir = {
    input.axisValue(Axis::MoveX),
    0,
    input.axisValue(Axis::MoveY)
};
camera.moveFPS(moveDir * moveSpeed * dt);

// ── Follow (terceira pessoa) ──────────────────────────────────
camera.follow(playerEntity, {0, 3, -6}, 0.05f);
camera.update(dt, world);

// ── Orbital (editor/estratégia) ───────────────────────────────
camera.setOrbitTarget({0, 0, 0});
camera.setOrbitDistance(15.0f);
camera.orbit(deltaX * 0.5f, deltaY * 0.5f);
camera.zoom(scrollDelta);

// ── Frustum culling ───────────────────────────────────────────
auto visible = octree.queryFrustum(camera.frustum());
```

---

## Coexistência com Camera2D

```cpp
// Frame com renderização 2D + 3D:

// Render 3D (mundo)
cmd->beginRenderPass(worldPassDesc);
meshSystem.renderWithCamera(camera3D, cmd);  // perspectiva
cmd->endRenderPass();

// Render 2D (HUD sobreposto)
cmd->beginRenderPass(hudPassDesc);
batchRenderer.setCamera(camera2D);  // ortho
batchRenderer.endFrame(cmd);
cmd->endRenderPass();
```

---

## Critério de Aceitação

- [ ] `lookAt` produz view matrix correta (comparado com glm::lookAt)
- [ ] `perspective` produz projection correta (comparado com glm::perspective)
- [ ] Frustum culling: objetos fora do frustum não renderizam
- [ ] Follow suave sem jitter (igual ao Camera2D)
- [ ] FPS mode: pitch clampado a [-89°, +89°] (sem flip)

---

## Dependências

- **Upstream:** [3D Math](3d-math.md), [Spatial Partitioning](spatial-partitioning.md), [Fase 4 — ECS](../fase4/ecs.md)
- **Downstream:** [Mesh Loading](mesh-loading.md), [Skeletal Animation](skeletal-animation.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §10 Camera System, RF5.5-5.6
- [`docs/fase3/camera.md`](../fase3/camera.md) — Camera2D (para coexistência)
- [`docs/fase5/spatial-partitioning.md`](spatial-partitioning.md) — Frustum culling via Octree
