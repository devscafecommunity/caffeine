# 🎥 Camera System

> **Fase:** 3 — O Olho da Engine  
> **Namespace:** `Caffeine::Render`  
> **Arquivo:** `src/render/Camera2D.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF3.7

---

## Visão Geral

O Camera System fornece a projeção matemática entre o **espaço do mundo** (coordenadas do jogo) e o **espaço da tela** (pixels). A `Camera2D` usa projeção ortográfica para jogos 2D. Na Fase 5, `Camera3D` adicionará perspectiva.

---

## API Planejada

```cpp
namespace Caffeine::Render {

// ============================================================================
// @brief  Câmera 2D com projeção ortográfica.
//
//  Funcionalidades:
//  - View matrix (posição + rotação da câmera)
//  - Projection matrix (ortográfica — sem perspectiva)
//  - worldToScreen / screenToWorld
//  - Follow entity com smoothing
//  - Camera shake
//  - World bounds clamp
// ============================================================================
class Camera2D {
public:
    Camera2D();

    // ── Matrizes ───────────────────────────────────────────────
    Mat4 viewMatrix()           const;
    Mat4 projectionMatrix()     const;
    Mat4 viewProjectionMatrix() const;  // cached, recalculated se dirty

    // ── Conversão de espaço ────────────────────────────────────
    Vec2 worldToScreen(Vec2 worldPos)   const;
    Vec2 screenToWorld(Vec2 screenPos)  const;
    bool isVisible(Rect2D worldRect)    const;  // frustum culling básico

    // ── Configuração ───────────────────────────────────────────
    void setPosition(Vec2 pos);
    void setZoom(f32 zoom);             // 1.0 = normal, 2.0 = 2x zoom in
    void setRotation(f32 degrees);
    void setViewport(Rect2D viewport);  // pixels (x, y, width, height)
    void setAspectRatio(f32 aspect);

    // ── Follow ─────────────────────────────────────────────────
    void follow(ECS::Entity target, f32 smoothing = 0.1f);  // lerp per frame
    void stopFollowing();
    void update(f64 dt, const ECS::World& world);           // processa follow + shake

    // ── Shake ──────────────────────────────────────────────────
    void shake(Vec2 intensity, f32 duration);  // em pixels

    // ── Bounds ─────────────────────────────────────────────────
    void setBounds(Rect2D worldBounds);
    void clearBounds();

    // ── Getters ────────────────────────────────────────────────
    Vec2   position()  const { return m_position; }
    f32    zoom()      const { return m_zoom; }
    f32    rotation()  const { return m_rotation; }
    Rect2D viewport()  const { return m_viewport; }
    Vec2   size()      const { return {m_viewport.size.x / m_zoom,
                                        m_viewport.size.y / m_zoom}; }

private:
    Mat4  calculateViewMatrix()       const;
    Mat4  calculateProjectionMatrix() const;
    Vec2  applyBounds(Vec2 pos)       const;

    Vec2   m_position    = {0, 0};
    f32    m_zoom        = 1.0f;
    f32    m_rotation    = 0.0f;
    Rect2D m_viewport    = {{0, 0}, {1280, 720}};
    f32    m_aspect      = 1280.0f / 720.0f;

    // Follow
    ECS::Entity m_followTarget    = ECS::Entity::INVALID;
    f32          m_followSmoothing = 0.1f;

    // Shake
    Vec2 m_shakeIntensity = {0, 0};
    f32  m_shakeTime      = 0.0f;
    f32  m_shakeDuration  = 0.0f;
    Vec2 m_shakeOffset    = {0, 0};

    // Bounds
    bool   m_hasBounds   = false;
    Rect2D m_worldBounds = {};

    mutable Mat4 m_cachedViewProj;
    mutable bool m_dirty = true;
};

}  // namespace Caffeine::Render
```

---

## Matemática da Projeção Ortográfica

```
Projection Matrix (ortho):
    left   = -viewport.width  / 2 / zoom
    right  = +viewport.width  / 2 / zoom
    bottom = -viewport.height / 2 / zoom
    top    = +viewport.height / 2 / zoom
    near   = -1000
    far    = +1000

    Mat4::ortho(left, right, bottom, top, near, far)

View Matrix:
    Mat4::translate(-position) * Mat4::rotateZ(-rotation)
```

**Nota:** A projeção ortográfica não possui perspectiva — objetos distantes têm o mesmo tamanho que objetos próximos. Correto para jogos 2D.

---

## Camera Follow

```cpp
// Smooth follow usando lerp:
void Camera2D::update(f64 dt, const ECS::World& world) {
    if (m_followTarget.isValid()) {
        if (auto* pos = world.get<Position2D>(m_followTarget)) {
            Vec2 target = {pos->x, pos->y};
            m_position = Vec2::lerp(m_position, target, m_followSmoothing);
            m_position = applyBounds(m_position);
            m_dirty = true;
        }
    }
    // Shake decay
    if (m_shakeTime > 0) {
        m_shakeTime -= dt;
        f32 t = m_shakeTime / m_shakeDuration;
        m_shakeOffset = m_shakeIntensity * t * randomDir();
        m_dirty = true;
    }
}
```

---

## Exemplos de Uso

```cpp
// ── Setup básico ─────────────────────────────────────────────
Caffeine::Render::Camera2D camera;
camera.setViewport({ {0, 0}, {1280, 720} });
camera.setZoom(1.0f);

// ── Follow jogador ────────────────────────────────────────────
camera.follow(playerEntity, 0.08f);  // 8% do caminho por frame
camera.setBounds({ {0, 0}, {5000, 3000} });  // limites do mapa

// ── Por frame ────────────────────────────────────────────────
camera.update(dt, world);
batchRenderer.setCamera(camera);

// ── Shake ao levar dano ───────────────────────────────────────
eventBus.subscribe<OnPlayerHit>([&](const OnPlayerHit& e) {
    camera.shake({5, 5}, 0.3f);  // 5px, 300ms
});

// ── Conversão de espaço para UI ───────────────────────────────
Vec2 screenPos = camera.worldToScreen(enemyWorldPos);
// Usa screenPos para renderizar health bar sobre o inimigo

// ── Culling manual ────────────────────────────────────────────
if (!camera.isVisible(entityBounds)) return;  // skip render
```

---

## Camera 3D (Fase 5)

Na Fase 5, `Camera3D` será adicionada com a mesma interface mas projeção perspectiva:

```cpp
// Fase 5:
class Camera3D {
    Mat4 viewMatrix()       const;  // lookAt(eye, target, up)
    Mat4 projectionMatrix() const;  // perspective(fovY, aspect, near, far)
    // ... mesma interface de setPosition/setZoom → será setFOV
};
```

O [Batch Renderer](batch-renderer.md) aceita `Camera2D` e `Camera3D` via polimorfismo.

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Ortho para 2D | Sem distorção perspectiva — correto para jogos 2D |
| Cache `m_dirty` | Evita recalcular VP matrix quando câmera não moveu |
| Follow com lerp | Suave, sem "snap" para posição do jogador |
| `isVisible(Rect2D)` | Frustum culling básico para 2D |
| `setBounds` | Câmera nunca mostra área fora do mapa |

---

## Critério de Aceitação

- [ ] Câmera segue entidade com `smoothing = 0.1f` sem jitter a 60fps
- [ ] `worldToScreen` e `screenToWorld` são operações inversas corretas
- [ ] Camera shake decai corretamente até zero
- [ ] `setBounds` impede câmera de mostrar área fora do mapa
- [ ] VP matrix cached — não recalcula quando câmera não moveu

---

## Dependências

- **Upstream:** [Fase 1 — Mat4, Vec2](../math/vectors.md), [Fase 4 — ECS Entity](../fase4/ecs.md)
- **Downstream:** [Batch Renderer](batch-renderer.md), [Fase 5 — Camera3D](../fase5/camera3d.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §10 Camera System
- [`docs/fase3/batch-renderer.md`](batch-renderer.md) — usa viewProjectionMatrix()
- [`docs/fase5/camera3d.md`](../fase5/camera3d.md) — extensão para 3D
