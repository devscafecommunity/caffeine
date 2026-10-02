# Physics 3D (Bullet3)

> **Namespace:** `Caffeine::Physics3D`  
> **Ficheiros:** `src/physics/PhysicsComponents3D.hpp`, `PhysicsWorld3D.cpp`, `PhysicsSystem3D.hpp`  
> **Estado:** 🟡 Primeira fatia — box, sphere, capsule, sync ECS no play mode  
> **Pilar:** [`plans/2026-09-22-engine-pillars-roadmap.md`](../plans/2026-09-22-engine-pillars-roadmap.md) §3

O ECS não inclui headers do Bullet. `RigidBody3D` e `Collider3D` são POD serializáveis. Os `btRigidBody` vivem em `PhysicsWorld3D`, indexados pelo id da entidade.

## Componentes

| Componente | Campos |
|------------|--------|
| `RigidBody3D` | `mass`, damping, `friction`, `restitution`, `bodyType` (Dynamic / Kinematic / Static), `lockRotation` |
| `Collider3D` | `Box` (half extents), `Sphere` (`halfExtents.x` = raio), `Capsule` (`x` raio, `y` altura total), `offset`, `isTrigger` |

Ambos são necessários. No Doppio: **Add Component → Physics 3D**. O step corre só em play mode (`SceneEditor::tickSystems`), a 1/60 s com no máximo 4 substeps. Ao sair do play mode o mundo Bullet é destruído; a cena editada volta pelo snapshot já existente.

## Política

- Gravidade por defeito: `(0, -9.81, 0)`.
- Corpos dinâmicos: a física escreve `Position3D`, `Transform` e `Rotation3D`.
- Corpos estáticos e cinemáticos: a pose do ECS é empurrada para o Bullet em cada frame.
- Escala de `Scale3D` / `Transform.scale` entra no tamanho do collider.
- Terreno heightfield, mesh convexa, debug draw e joints ainda não estão ligados.

Build: `CAFFEINE_ENABLE_BULLET3=ON` (defeito). Sem a flag, `PhysicsWorld3D::isAvailable()` devolve false e o sistema é no-op.
