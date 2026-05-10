# 🎥 Camera 3D

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Render`  
> **Arquivo:** `src/render/Camera3D.hpp`  
> **Status:** ✅ Implementado  
> **RFs:** RF5.5, RF5.6

---

## Visão Geral

Camera3D com projeção perspectiva para renderização 3D. Coexiste com `Camera2D` (Fase 3) — o mesmo frame pode renderizar 2D (HUD, sprites) e 3D (ambiente, personagens) com câmeras diferentes.

---

## API

```cpp
namespace Caffeine::Render {

class Camera3D {
public:
    Camera3D() = default;

    // ── Matrizes ───────────────────────────────────────────────
    Mat4 viewMatrix()           const;
    Mat4 projectionMatrix()     const;
    Mat4 viewProjectionMatrix() const;   // cached, dirty-flagged

    // ── Frustum (RF5.6) ────────────────────────────────────────
    Spatial::Frustum frustum() const;

    // ── Conversão de espaço ────────────────────────────────────
    Vec3 worldToScreen(Vec3 worldPos) const;
    Vec3 screenToWorld(Vec2 screenPos, f32 depth = 0.5f) const;
    bool isVisible(const Spatial::AABB3D& bounds) const;

    // ── Configuração perspectiva ───────────────────────────────
    void setFOV(f32 fovYDegrees);        // clamped [1, 179]
    void setAspect(f32 aspect);          // clamped > 0
    void setNearFar(f32 near, f32 far);  // near ≥ 0.1, far > near
    void setViewport(Rect2D viewport);   // necessário para screen <-> world

    // ── Transform ──────────────────────────────────────────────
    void setPosition(Vec3 pos);
    void setRotation(Quat rot);
    void lookAt(Vec3 eye, Vec3 target, Vec3 up = {0, 1, 0});

    // ── FPS mode ───────────────────────────────────────────────
    void rotateFPS(f32 deltaPitch, f32 deltaYaw);  // graus
    void moveFPS(Vec3 localDelta);                  // espaço local

    // ── Orbital mode ───────────────────────────────────────────
    void orbit(f32 deltaAzimuth, f32 deltaElevation);  // graus
    void setOrbitTarget(Vec3 target);
    void setOrbitDistance(f32 distance);
    void zoom(f32 delta);

    // ── Follow mode ────────────────────────────────────────────
    void follow(ECS::Entity target, Vec3 offset = {0, 2, -5},
                f32 smoothing = 0.05f);
    void stopFollowing();
    void update(f64 dt, const ECS::World& world);

    // ── Getters ────────────────────────────────────────────────
    Vec3   position()  const;
    Quat   rotation()  const;
    Vec3   forward()   const;
    Vec3   right()     const;
    Vec3   up()        const;
    f32    fov()       const;
    f32    aspect()    const;
    f32    nearPlane() const;
    f32    farPlane()  const;
    Rect2D viewport()  const;
};

}  // namespace Caffeine::Render
```

---

## Matemática da Projeção Perspectiva

```
Projection Matrix (Mat4::perspective):
    f = 1 / tan(fovY/2)
    A = -(far + near) / (far - near)     ← row 2, col 2
    B = -(2 * far * near) / (far - near) ← row 3, col 2

    [f/aspect,  0,   0,  0]
    [0,         f,   0,  0]
    [0,         0,   A, -1]
    [0,         0,   B,  0]

Nota: layout não-padrão — o fator -1 da divisão perspectiva
está em (2,3) ao invés de (3,2), e o termo near/far está em (3,2).

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

- [x] `lookAt` produz view matrix correta (verificado via view * proj roundtrip)
- [x] `perspective` produz projection correta (comparado com Mat4::perspective direct)
- [x] Frustum culling: objetos fora do frustum não renderizam (teste automatizado)
- [x] Follow suave sem jitter (lerp testado via update)
- [x] FPS mode: pitch clampado a [-89°, +89°] (teste automatizado)
- [x] screenToWorld roundtrip (world → screen → world produz mesmo ponto)
- [x] worldToScreen projeta origem no centro da viewport

---

## Dependências

- **Upstream:** [3D Math](3d-math.md), [Spatial Partitioning](spatial-partitioning.md), [Fase 4 — ECS](../fase4/ecs.md)
- **Downstream:** [Mesh Loading](mesh-loading.md), [Skeletal Animation](skeletal-animation.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §10 Camera System, RF5.5-5.6
- [`docs/fase3/camera.md`](../fase3/camera.md) — Camera2D (para coexistência)
- [`docs/fase5/spatial-partitioning.md`](spatial-partitioning.md) — Frustum culling via Octree
