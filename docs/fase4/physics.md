# ⚙️ Physics 2D

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::Physics2D`  
> **Arquivo:** `src/physics/PhysicsSystem2D.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF4.10

---

## Visão Geral

Sistema de física 2D simples com suporte a rigid body dynamics, AABB e circle collision, layers e resposta simples de colisão (restitution + friction).

**Escopo intencional:** Este sistema cobre 90% dos casos de jogos 2D. Para features avançadas (joints, CCD, convex hulls), integrar biblioteca dedicada (Box2D/Jolt) como escalation trigger.

---

## API Planejada

```cpp
namespace Caffeine::Physics2D {

// ============================================================================
// @brief  Resultado de um raycast.
// ============================================================================
struct RaycastHit {
    ECS::Entity entity;
    Vec2         point;
    Vec2         normal;
    f32          distance;
    bool         hit = false;
};

// ============================================================================
// @brief  Manifold de colisão — dados de contato entre dois corpos.
// ============================================================================
struct CollisionManifold {
    Vec2 contactPoint;
    Vec2 normal;       // de B para A
    f32  penetration;
};

// ============================================================================
// @brief  Sistema de física 2D.
//
//  Integração com ECS:
//  - Lê: RigidBody2D, Collider2D, Position2D, Velocity2D
//  - Escreve: Position2D, Velocity2D
//  - Publica: Events::OnCollision2D via EventBus
//
//  NÃO suporta (para integrar biblioteca dedicada quando necessário):
//  - Rotação de rigid bodies
//  - Joints / constraints
//  - Complex shapes (convex hulls)
//  - CCD (Continuous Collision Detection)
// ============================================================================
class PhysicsSystem2D : public ECS::ISystem {
public:
    explicit PhysicsSystem2D(Events::EventBus* eventBus = nullptr);

    void update(ECS::World& world, f64 dt) override;
    i32  priority() const override { return 100; }
    const char* name() const override { return "Physics2D"; }

    // ── Configuração ───────────────────────────────────────────
    void setGravity(Vec2 gravity);
    Vec2 gravity() const { return m_gravity; }

    // ── Queries espaciais ──────────────────────────────────────
    RaycastHit raycast(Vec2 origin, Vec2 direction, f32 maxDistance,
                       u32 layerMask = 0xFFFFFFFF) const;

    Array<ECS::Entity, 32> overlapCircle(Vec2 center, f32 radius,
                                          u32 layerMask = 0xFFFFFFFF) const;

    Array<ECS::Entity, 32> overlapAABB(Rect2D rect,
                                        u32 layerMask = 0xFFFFFFFF) const;

    // ── Controle de bodies ─────────────────────────────────────
    void applyForce(ECS::Entity e, Vec2 force);      // acumulado, limpo no update
    void applyImpulse(ECS::Entity e, Vec2 impulse);  // instantâneo
    void setKinematic(ECS::Entity e, bool kinematic);
    void teleport(ECS::Entity e, Vec2 position);     // sem sweep de colisão

private:
    void integrate(ECS::Entity e, RigidBody2D& rb, Position2D& pos,
                   Velocity2D& vel, f32 dt);
    bool detectAABBvsAABB(const Collider2D& a, const Position2D& pa,
                           const Collider2D& b, const Position2D& pb,
                           CollisionManifold* out);
    bool detectCirclevsCircle(f32 rA, Vec2 cA, f32 rB, Vec2 cB,
                               CollisionManifold* out);
    void resolveCollision(ECS::Entity a, ECS::Entity b,
                          const CollisionManifold& m);
    void publishCollisionEvent(ECS::Entity a, ECS::Entity b,
                                const CollisionManifold& m);

    Vec2 m_gravity = {0, -9.81f * 60.0f};  // pixels/s²
    HashMap<ECS::Entity, Vec2>  m_pendingForces;
    Events::EventBus*            m_eventBus = nullptr;
};

}  // namespace Caffeine::Physics2D
```

---

## Pipeline de Física por Frame

```
1. Broad phase
   └── AABB bounding box check para todos os pares
       (evita narrow phase para objetos distantes)

2. Narrow phase
   ├── AABB vs AABB  (Separating Axis Theorem)
   └── Circle vs Circle

3. Resolver colisões
   └── Apply impulse baseado em restitution/friction/mass

4. Integração
   └── pos += vel * dt  (Euler semi-implícito)
       vel += (gravity + forces) * dt

5. Publicar eventos
   └── EventBus::publishDeferred(OnCollision2D{...})
```

---

## Exemplos de Uso

```cpp
// ── Setup ─────────────────────────────────────────────────────
auto* physics = world.registerSystem<PhysicsSystem2D>(&eventBus);
physics->setGravity({0, -500.0f});  // pixels/s²

// ── Entidade com física ───────────────────────────────────────
world.add<RigidBody2D>(player, {
    .mass        = 5.0f,
    .restitution = 0.3f,
    .friction    = 0.8f
});
world.add<Collider2D>(player, {
    .size     = {30, 60},
    .offset   = {0, 0},
    .isStatic = false,
    .layer    = 1  // player layer
});

// ── Plataforma estática ───────────────────────────────────────
world.add<Collider2D>(platform, {
    .size     = {200, 20},
    .isStatic = true,
    .layer    = 0  // world layer
});

// ── Aplicar força de pulo ─────────────────────────────────────
if (input.justPressed(Action::Jump)) {
    physics->applyImpulse(player, {0, 800.0f});
}

// ── Raycast para "chão existe abaixo?" ────────────────────────
auto hit = physics->raycast(playerPos, {0, -1}, 32.0f);
bool onGround = hit.hit;

// ── Overlap para área de ataque ───────────────────────────────
Rect2D attackBox = { playerPos + Vec2{20, 0}, {40, 40} };
auto hits = physics->overlapAABB(attackBox);
for (auto& enemy : hits) {
    world.add<TakeDamage>(enemy, { .amount = 25 });
}
```

---

## Collision Layers

```
Layer 0 = World (plataformas, paredes)
Layer 1 = Player
Layer 2 = Enemy
Layer 3 = Projectile
Layer 4 = Trigger

Máscara: layerMask = (1<<0)|(1<<2) = colide com World E Enemy
```

---

## Escalation Triggers

Se o jogo precisar de:
- Rotação de rigid bodies → integrar **Box2D** ou **Jolt Physics**
- Joints/constraints → integrar biblioteca dedicada
- CCD (evitar tunelamento em velocidade alta) → biblioteca dedicada

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| AABB simples | Cobre 90% dos casos 2D sem complexidade |
| Semi-implicit Euler | Estável e simples para plataformers |
| Eventos de colisão via EventBus | Physics não acopla a Audio/FX |
| `priority = 100` | Física antes de tudo — base para movement/animation |
| `pendingForces` mapa | Permite aplicar forças de múltiplos sistemas |

---

## Critério de Aceitação

- [ ] 1K rigid bodies a 60fps
- [ ] Physics step < 3ms para 1K bodies
- [ ] `tsan` clean — `applyForce` thread-safe
- [ ] Raycast retorna hit correto para todos ângulos
- [ ] Colisões publicam `OnCollision2D` corretamente

---

## Dependências

- **Upstream:** [ECS Core](ecs.md), [Event Bus](events.md), [Fase 1 — Math](../math/vectors.md)
- **Downstream:** [Audio](audio.md) (reage a colisões), [Fase 5 — Spatial Partitioning](../fase5/spatial-partitioning.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §13 Physics 2D
- [`docs/fase4/events.md`](events.md) — `OnCollision2D` event
- [Box2D/LiquidFun](https://github.com/google/liquidfun) — referência para casos avançados
