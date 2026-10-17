# 📐 3D Math Extension

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine` (tipos) / `Caffeine::Math` (funções)  
> **Arquivos:** `src/math/Quat.hpp`, `src/math/Mat4.hpp` (perspective, lookAt)  
> **Status:** ✅ Implementado  
> **Testes:** `tests/test_math.cpp` (48 casos, incl. slerp/toMatrix/fromMatrix)  
> **RF:** RF5.1

---

## Visão Geral

Extensão da biblioteca matemática da Fase 1 para suportar operações 3D completas. O foco é `Quaternion` para rotações 3D (mais estável que Euler, sem gimbal lock).

**Nota:** `Vec2`, `Vec3`, `Vec4`, `Mat4` já existem da Fase 1. Esta fase adiciona `Quat` e os métodos 3D de `Mat4` (rotationX/Y, perspective, lookAt).

---

## API Real

```cpp
namespace Caffeine {

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
    static Quat fromAxisAngle(Vec3 axis, f32 angleRad);  // eixo normalizado internamente
    static Quat fromEuler(f32 pitchRad, f32 yawRad, f32 rollRad);  // convenção ZYX
    static Quat lookAt(Vec3 forward, Vec3 up);  // SEM default: passar up explicitamente
    static Quat fromMatrix(const Mat4& m);      // algoritmo de Shoemake (1987)

    // ── Interpolação ────────────────────────────────────────────
    static Quat slerp(Quat a, Quat b, f32 t);   // esférica (arco mais curto; fallback linear se a·b > 0.9999)
    static Quat nlerp(Quat a, Quat b, f32 t);   // linear normalizada (mais rápida, sem velocidade angular constante)

    // ── Conversões ─────────────────────────────────────────────
    Vec3 toEuler()  const;   // ZYX, inverso de fromEuler()
    Mat4 toMatrix() const;   // rotação column-major (v' = q·v·q⁻¹)

    // ── Operações ──────────────────────────────────────────────
    Vec3 rotate(Vec3 v) const;          // rota um vetor (2 cross products)
    Quat conjugate()    const { return {-x, -y, -z, w}; }
    Quat inverse()      const;          // conjugate / |q|²
    Quat normalized()   const;          // identidade se comprimento zero (silencioso)
    f32  length()       const;
    f32  lengthSquared() const;
    f32  dot(Quat o)    const { return x*o.x + y*o.y + z*o.z + w*o.w; }

    Quat operator*(Quat o)  const;   // composição (aplica o primeiro, depois this)
    Vec3 operator*(Vec3 v)  const { return rotate(v); }
    bool operator==(Quat o) const;   // igualdade EXATA de floats
};

// ============================================================================
// @brief  Métodos 3D de Mat4 (todos implementados).
//
//  Já existentes: identity, zero, translation, scale, rotationX/Y/Z,
//  ortho, perspective, lookAt. NÃO existe Mat4::fromQuat —
//  use Quat::toMatrix() para quaternion → matriz.
// ============================================================================

}  // namespace Caffeine
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
Caffeine::Quat rot = Caffeine::Quat::fromAxisAngle({0, 1, 0}, Caffeine::Math::PI_HALF);  // 90° em Y
Caffeine::Vec3 rotated = rot * Caffeine::Vec3{1, 0, 0};  // → {0, 0, -1}

// ── Euler para Quaternion ─────────────────────────────────────
Caffeine::Quat camRot = Caffeine::Quat::fromEuler(pitch, yaw, 0);

// ── SLERP para câmera suave ───────────────────────────────────
Caffeine::Quat current = /* orientação atual */;
Caffeine::Quat target  = Caffeine::Quat::lookAt(direction, {0, 1, 0});
Caffeine::Quat smooth  = Caffeine::Quat::slerp(current, target, 0.1f * dt * 60);

// ── Converter para shader ─────────────────────────────────────
Caffeine::Mat4 modelMatrix = Caffeine::Mat4::translation(pos) * quat.toMatrix() * Caffeine::Mat4::scale(scale);
// upload modelMatrix para shader via UniformBuffer
```

---

## Componentes ECS 3D (reais)

```cpp
namespace Caffeine::ECS {

struct Position3D { Vec3 position; };
// ⚠️ Rotation3D guarda Vec4 (x, y, z, w), NÃO Quat — converta manualmente:
struct Rotation3D { Vec4 quaternion = Vec4(0.0f, 0.0f, 0.0f, 1.0f); };
struct Scale3D    { Vec3 scale = Vec3(1.0f, 1.0f, 1.0f); };

}  // namespace Caffeine::ECS
```

> Não existem `Velocity3D` nem `WorldTransform3D` no código. Ver
> `src/ecs/Components3D.hpp`.

---

## Critério de Aceitação

- [x] `Quat::slerp` interpolação correta (`tests/test_math.cpp`: mesmo quat, t=0/t=1)
- [x] `quat.toMatrix()` == `Mat4::rotationY(angle)` para o mesmo ângulo (testado)
- [x] `fromMatrix(identity)` == identidade; `fromEuler`/`toEuler` round-trip testado
- [x] `Quat * Vec3` rota corretamente (`rotate` preserva comprimento)
- [ ] `Mat4::fromQuat` — não existe (usar `toMatrix()`); decidir se cria o helper
- [ ] SIMD (`CF_SIMD`, `simdAdd`…) — não existe no código; seção removida
- [ ] `Rotation3D` guardar `Quat` em vez de `Vec4` (melhoria de API)

---

## Dependências

- **Upstream:** [Vec3, Vec4, Mat4](vectors.md)
- **Downstream:** [Camera 3D](../rendering/camera-3d.md), [Skeletal Animation](../animation/skeletal-animation.md), [Mesh Loading](../assets/mesh-loading.md)

---

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Renderização & Pipeline]() | Quaternions e Mat4 na pipeline 3D |
| [Memória & Dados]() | Tipos matemáticos como dados fundamentais |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §15 Math Library
- [`docs/math/vectors.md`](../math/vectors.md) — Documentação de Vec/Mat existentes
- [Índice de Tópicos Transversais]()
