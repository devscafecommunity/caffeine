# 📐 3D Math Extension

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Math`  
> **Arquivo:** `src/math/Quat.hpp` (extensão de `Mat4.hpp`)  
> **Status:** 📅 Planejado  
> **RF:** RF5.1

---

## Visão Geral

Extensão da biblioteca matemática da Fase 1 para suportar operações 3D completas. O foco é `Quaternion` para rotações 3D (mais estável que Euler, sem gimbal lock) e extensões SIMD nos vetores existentes.

**Nota:** `Vec2`, `Vec3`, `Vec4`, `Mat4` já existem da Fase 1. Esta fase adiciona `Quat` e completa os métodos 3D de `Mat4` (perspective, lookAt).

---

## API Planejada

```cpp
namespace Caffeine::Math {

// ============================================================================
// @brief  Quaternion para rotações 3D.
//
//  Representação: (x, y, z, w) onde w é a parte escalar.
//  Uso: Sem gimbal lock, SLERP suave, conversão para/de Matrix/Euler.
// ============================================================================
struct Quat {
    f32 x = 0, y = 0, z = 0, w = 1;  // identidade

    // ── Construtores estáticos ──────────────────────────────────
    static Quat identity()   { return {0, 0, 0, 1}; }
    static Quat fromAxisAngle(Vec3 axis, f32 angleRad);
    static Quat fromEuler(f32 pitchRad, f32 yawRad, f32 rollRad);
    static Quat lookAt(Vec3 forward, Vec3 up = {0, 1, 0});

    // ── Interpolação ────────────────────────────────────────────
    static Quat slerp(Quat a, Quat b, f32 t);   // spherical linear
    static Quat nlerp(Quat a, Quat b, f32 t);   // normalized linear (mais rápido)

    // ── Conversões ─────────────────────────────────────────────
    Vec3 toEuler()  const;   // pitch, yaw, roll em radianos
    Mat4 toMatrix() const;   // Mat4 column-major para shader

    // ── Operações ──────────────────────────────────────────────
    Vec3 rotate(Vec3 v) const;          // rota um vetor por este quaternion
    Quat conjugate()    const { return {-x, -y, -z, w}; }
    Quat inverse()      const;          // conjugate / length²
    Quat normalized()   const;
    f32  length()       const;
    f32  dot(Quat o)    const { return x*o.x + y*o.y + z*o.z + w*o.w; }

    Quat operator*(Quat o)  const;   // composição de rotações
    Vec3 operator*(Vec3 v)  const { return rotate(v); }
    bool operator==(Quat o) const;
};

// ============================================================================
// @brief  Extensões 3D para Mat4 (completando Fase 1).
//
//  Métodos já presentes na Fase 1: identity, translate, scale, rotateZ, ortho.
//  Esta fase adiciona: rotateX, rotateY, perspective, lookAt.
// ============================================================================
// Em Mat4.hpp — adicionados na Fase 5:
// static Mat4 rotateY(f32 angle);                     // yaw
// static Mat4 rotateX(f32 angle);                     // pitch
// static Mat4 fromQuat(Quat q);                        // de Quaternion
// static Mat4 perspective(f32 fovY, f32 aspect,
//                          f32 near, f32 far);          // perspectiva
// static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);  // view matrix

// ============================================================================
// @brief  SIMD hints para Vec4 (aligned access).
//
//  Vec4 já tem alignas(16) da Fase 1.
//  Funções SIMD usam intrinsics se CF_SIMD definido.
// ============================================================================
#ifdef CF_SIMD
Vec4 simdAdd(Vec4 a, Vec4 b);
Vec4 simdMul(Vec4 a, Vec4 b);
f32  simdDot(Vec4 a, Vec4 b);
#endif

}  // namespace Caffeine::Math
```

---

## Quaternion vs Euler vs Rotation Matrix

| Representação | Memória | Composição | Interpolação | Problema |
|---------------|---------|------------|--------------|---------|
| **Euler** (pitch/yaw/roll) | 12B | Sim | Lerp (com artefatos) | Gimbal lock |
| **Quaternion** | 16B | O(1) | SLERP suave | Visualização não intuitiva |
| **Rotation Matrix** | 48B | O(n²) | Difícil | Redundante |

**Caffeine usa Quaternion internamente** para rotações 3D; converte para Matrix apenas ao enviar ao shader.

---

## Exemplos de Uso

```cpp
// ── Rotação por eixo ──────────────────────────────────────────
Caffeine::Math::Quat rot = Quat::fromAxisAngle({0, 1, 0}, Math::PI_HALF);  // 90° em Y
Vec3 rotated = rot * Vec3{1, 0, 0};  // → {0, 0, -1}

// ── Euler para Quaternion ─────────────────────────────────────
Quat camRot = Quat::fromEuler(pitch, yaw, 0);

// ── SLERP para câmera suave ───────────────────────────────────
Quat current = entity.get<Rotation3D>().quat;
Quat target  = Quat::lookAt(direction);
entity.get<Rotation3D>().quat = Quat::slerp(current, target, 0.1f * dt * 60);

// ── Converter para shader ─────────────────────────────────────
Mat4 modelMatrix = Mat4::translate(pos) * quat.toMatrix() * Mat4::scale(scale);
// upload modelMatrix para shader via UniformBuffer
```

---

## Componentes ECS 3D

```cpp
namespace Caffeine::Components {

struct Position3D  { f32 x = 0, y = 0, z = 0; };
struct Rotation3D  { Caffeine::Math::Quat quat = Quat::identity(); };
struct Scale3D     { f32 x = 1, y = 1, z = 1; };
struct Velocity3D  { f32 x = 0, y = 0, z = 0; };

struct WorldTransform3D {
    Caffeine::Math::Mat4 matrix = Mat4::identity();
};

}  // namespace Caffeine::Components
```

---

## Critério de Aceitação

- [ ] `Quat::slerp` produz interpolação correta entre quaisquer duas orientações
- [ ] `quat.toMatrix()` produz mesma matrix que `Mat4::fromAxisAngle`
- [ ] `Quat * Vec3` rota corretamente (comparado com reference impl)
- [ ] Sem gimbal lock em rotações compostas de 3 eixos
- [ ] `Vec4` acessos SIMD-aligned (alinhado a 16 bytes)

---

## Dependências

- **Upstream:** [Fase 1 — Vec3, Vec4, Mat4](../math/vectors.md)
- **Downstream:** [Camera3D](camera3d.md), [Skeletal Animation](skeletal-animation.md), [Mesh Loading](mesh-loading.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §15 Math Library
- [`docs/math/vectors.md`](../math/vectors.md) — Documentação de Vec/Mat existentes
