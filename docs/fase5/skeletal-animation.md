# 🦴 Skeletal Animation

> **Fase:** 5 — Transição Dimensional  
> **Namespace:** `Caffeine::Animation`  
> **Arquivo:** `src/animation/SkeletalAnimation.hpp`  
> **Status:** 📅 Planejado

---

## Visão Geral

Extensão do [Animation System da Fase 4](../fase4/animation.md) para suportar skeletal animation 3D: hierarquia de bones, vertex skinning e blend trees. Carregado de arquivos `.gltf` com animações.

---

## API Planejada

```cpp
namespace Caffeine::Animation {

// ============================================================================
// @brief  Bone — nó da hierarquia esquelética.
// ============================================================================
struct Bone {
    FixedString<32>  name;
    i32              parentIndex;   // -1 se root
    Math::Mat4       bindPoseInverse;   // inverse bind pose (pré-calculado)
    Math::Mat4       localTransform;    // TRS local atual
};

// ============================================================================
// @brief  Esqueleto — hierarquia de bones.
// ============================================================================
struct Skeleton {
    std::vector<Bone> bones;
    u32               boneCount() const { return bones.size(); }
    i32               findBone(const char* name) const;
};

// ============================================================================
// @brief  Keyframe de animação esquelética.
// ============================================================================
struct SkeletalKeyframe {
    f32           time;
    Math::Vec3    position;
    Math::Quat    rotation;
    Math::Vec3    scale;
};

// ============================================================================
// @brief  Clipe de animação esquelética.
// ============================================================================
struct SkeletalClip {
    FixedString<32>  name;
    f32              duration;
    f32              fps;
    bool             loop = true;

    // Canal por bone: bone_index → lista de keyframes
    HashMap<u32, std::vector<SkeletalKeyframe>> channels;

    void sampleAt(f64 time, std::vector<Math::Mat4>& boneTransforms) const;
};

// ============================================================================
// @brief  Blend tree — combina múltiplas animações com pesos.
//
//  Ex: "walk" (0.3) + "run" (0.7) → animação interpolada
// ============================================================================
struct BlendNode {
    enum class Type : u8 {
        Clip,   // Folha: reproduz um clipe
        Blend1D, // Interpola por 1 parâmetro (ex: speed)
        Blend2D  // Interpola por 2 parâmetros (ex: direction + speed)
    };

    Type type;
    const SkeletalClip* clip = nullptr;  // para Type::Clip
    std::vector<std::pair<f32, BlendNode*>> children;  // para Blend1D
    f32 parameter = 0.0f;   // valor atual do parâmetro de blend
};

// ============================================================================
// @brief  Componente de animação esquelética de uma entidade.
// ============================================================================
struct SkeletalAnimator {
    const Skeleton*         skeleton     = nullptr;
    std::vector<Math::Mat4> boneMatrices; // resultado final para GPU

    // State machine (igual ao Fase 4, mas com SkeletalClip)
    HashMap<FixedString<32>, AnimationState> states;
    FixedString<32>  currentState;
    f32              timeInState = 0.0f;

    // Blend tree (opcional — substitui state machine simples)
    BlendNode* blendTree = nullptr;

    // IK targets (futuro)
    // std::vector<IKTarget> ikTargets;
};

// ============================================================================
// @brief  Sistema de animação esquelética.
//
//  Herda AnimationSystem (Fase 4) e adiciona skinning.
// ============================================================================
class SkeletalAnimationSystem : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32  priority() const override { return 210; }  // logo após AnimationSystem (200)
    const char* name() const override { return "SkeletalAnimation"; }

private:
    void evaluateBlendTree(BlendNode* node, f64 time,
                            std::vector<Math::Mat4>& out);
    void applySkinning(const SkeletalAnimator& anim,
                       Assets::Mesh3D* mesh);
};

}  // namespace Caffeine::Animation
```

---

## Vertex Skinning

```
Per-vertex data (adicionado ao Vertex3D em Fase 5):
  Vec4 boneWeights;   // 4 pesos (soma = 1.0)
  u32  boneIndices;   // 4 índices packed (u8 × 4)

GPU shader:
  finalPosition = Σ (boneWeights[i] * boneMatrices[boneIndices[i]] * position)
```

---

## Exemplos de Uso

```cpp
// ── Carregar .gltf com skeletal data ─────────────────────────
auto mesh = meshLoader.loadAsync("characters/hero.caf");
// hero.caf inclui: geometria + skeleton + clipes de animação

// ── Setup SkeletalAnimator ────────────────────────────────────
SkeletalAnimator anim;
anim.skeleton = mesh.get()->skeleton;
anim.boneMatrices.resize(anim.skeleton->boneCount());

// Configurar state machine
AnimationState idleState { "idle", &mesh.get()->clips["idle"] };
idleState.transitions.push_back({
    .toState = "walk",
    .condition = [&]() { return input.axisValue(MoveX) != 0; }
});
anim.states["idle"] = idleState;
anim.currentState   = "idle";

world.add<SkeletalAnimator>(hero, std::move(anim));

// ── Blend tree (run speed) ────────────────────────────────────
BlendNode runBlend { .type = BlendNode::Type::Blend1D };
runBlend.children = {
    { 0.0f, new BlendNode { .clip = &clips["idle"] } },
    { 0.5f, new BlendNode { .clip = &clips["walk"] } },
    { 1.0f, new BlendNode { .clip = &clips["run"] } }
};
runBlend.parameter = playerSpeed / maxSpeed;
anim.blendTree = &runBlend;
```

---

## Critério de Aceitação

- [ ] Animação esquelética com 60fps em 10 personagens simultâneos
- [ ] Blend tree interpola suavemente entre walk/run
- [ ] Bone matrices corretas (comparadas com referência glTF)
- [ ] Skinning vertex shader produz deformação correta

---

## Dependências

- **Upstream:** [Animation System](../fase4/animation.md), [3D Math](3d-math.md), [Mesh Loading](mesh-loading.md)
- **Downstream:** IK (futuro), Ragdoll physics (futuro)

---

## Referências

- [`docs/fase4/animation.md`](../fase4/animation.md) — AnimationSystem base
- [`docs/fase5/mesh-loading.md`](mesh-loading.md) — carrega skeleton + clipes de .gltf
- [`docs/fase5/3d-math.md`](3d-math.md) — Quaternion para interpolação de bones
