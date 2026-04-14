# 🎬 Animation System

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::Animation`  
> **Arquivo:** `src/animation/AnimationSystem.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF4.9

---

## Visão Geral

Sistema de animação 2D com clipes de sprite e state machine. Cada entidade tem um componente `Animator` que gerencia a state machine de animação.

**Fase 5** adiciona skeletal animation (bones, skinning, blend trees) — ver [`docs/fase5/skeletal-animation.md`](../fase5/skeletal-animation.md).

---

## API Planejada

```cpp
namespace Caffeine::Animation {

// ============================================================================
// @brief  Clipe de animação — sequência de frames de sprite sheet.
// ============================================================================
struct AnimationClip {
    FixedString<32>     name;
    u32                 fps          = 12;
    std::vector<Rect2D> frames;       // regiões na textura/atlas
    bool                loop         = true;
    f32 duration() const { return (f32)frames.size() / (f32)fps; }
};

// ============================================================================
// @brief  Transição entre estados de animação.
// ============================================================================
struct AnimationTransition {
    FixedString<32>       toState;
    std::function<bool()> condition;   // quando esta condição é verdade, transiciona
    f32                   blendTime   = 0.1f;  // segundos de crossfade
    bool                  hasExitTime = false;  // true = espera clip terminar
};

// ============================================================================
// @brief  Estado de animação (nó na state machine).
// ============================================================================
struct AnimationState {
    FixedString<32>                  name;
    const AnimationClip*             clip        = nullptr;
    f32                              speed       = 1.0f;
    std::vector<AnimationTransition> transitions;
};

// ============================================================================
// @brief  Componente de animação da entidade.
//
//  Contém a state machine e estado atual.
//  O AnimationSystem itera sobre todas as entidades com este componente.
// ============================================================================
struct Animator {
    HashMap<FixedString<32>, AnimationState> states;
    FixedString<32>  currentState;
    FixedString<32>  previousState;
    f32              timeInState    = 0.0f;
    f32              blendWeight    = 1.0f;   // para crossfade (0 → 1)
    f32              playbackScale  = 1.0f;   // multiplicador de velocidade
    bool             paused         = false;

    // Eventos de animação (frame específico dispara callback)
    std::vector<std::pair<u32, FixedString<32>>> frameEvents;
    std::function<void(const FixedString<32>&)>  onFrameEvent;
};

// ============================================================================
// @brief  Sistema de animação ECS.
//
//  Por frame:
//  1. Verifica condições de transição
//  2. Avança timeInState
//  3. Calcula frame atual do clipe
//  4. Atualiza Sprite.srcRect com o frame correto
//  5. Dispara frameEvents se frame atingido
// ============================================================================
class AnimationSystem : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32  priority() const override { return 200; }
    const char* name() const override { return "Animation"; }

    // API imperativa (útil para gameplay code)
    void play(ECS::Entity e, const char* stateName,
              f32 blendTime = 0.1f);
    void pause(ECS::Entity e);
    void resume(ECS::Entity e);
    void setSpeed(ECS::Entity e, f32 speed);
    bool isPlaying(ECS::Entity e, const char* stateName) const;

private:
    void evaluateTransitions(ECS::Entity e, Animator& anim, f64 dt);
    void advanceFrame(ECS::Entity e, Animator& anim, Sprite& sprite, f64 dt);
    void checkFrameEvents(ECS::Entity e, Animator& anim);
};

}  // namespace Caffeine::Animation
```

---

## State Machine Visual

```
           jump == true
idle ──────────────────► jump_rise
  │                           │
  │◄──────────────────────────┤  apex (vel.y < 0.1)
  │     land                  ▼
  │◄──────────────── jump_fall
  │
  │  moveX != 0
  ├────────────────► walk
  │
  │  attack btn
  └────────────────► attack_1 ──► attack_2 (combo)
```

---

## Exemplos de Uso

```cpp
// ── Setup do Animator ─────────────────────────────────────────
Animator anim;

AnimationClip idleClip;
idleClip.name = "idle";
idleClip.fps  = 8;
idleClip.frames = { {0,0,64,64}, {64,0,64,64} };
idleClip.loop = true;

AnimationClip walkClip;
walkClip.name = "walk";
walkClip.fps  = 12;
walkClip.frames = { {128,0,64,64}, {192,0,64,64}, {256,0,64,64}, {320,0,64,64} };
walkClip.loop = true;

AnimationState idleState { "idle", &idleClip };
idleState.transitions.push_back({
    .toState   = "walk",
    .condition = [&]() { return input.axisValue(Axis::MoveX) != 0.0f; }
});

AnimationState walkState { "walk", &walkClip };
walkState.transitions.push_back({
    .toState   = "idle",
    .condition = [&]() { return input.axisValue(Axis::MoveX) == 0.0f; }
});

anim.states["idle"] = idleState;
anim.states["walk"] = walkState;
anim.currentState   = "idle";

world.add<Animator>(player, std::move(anim));

// ── Registrar sistema ─────────────────────────────────────────
world.registerSystem<AnimationSystem>();

// ── Controle programático ─────────────────────────────────────
auto* animSys = world.getSystem<AnimationSystem>();
animSys->play(player, "attack_1", 0.0f);  // sem blend (combos)

// ── Frame events ──────────────────────────────────────────────
world.get<Animator>(player)->frameEvents.push_back({3, "attack_hit"});
world.get<Animator>(player)->onFrameEvent = [](const FixedString<32>& evt) {
    if (evt == "attack_hit") damageEnemiesInRange();
};
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| State machine | Lógica de animação separada da lógica de gameplay |
| `condition` como lambda | Flexível sem overhead de parsing |
| `blendTime` por transição | Crossfade suave por padrão |
| Frame events | Sincronizar gameplay com quadros específicos da animação |
| `priority = 200` | Roda após PhysicsSystem (100) e MovementSystem (150) |

---

## Critério de Aceitação

- [ ] `advanceFrame()` < 0.1ms para 100 entidades animadas
- [ ] 60fps com 100 entidades animadas simultaneamente
- [ ] Transições com blendTime suaves (sem pop visual)
- [ ] Frame events disparados no frame correto

---

## Dependências

- **Upstream:** [ECS Core](ecs.md), [Asset Manager](../fase3/asset-manager.md) (AnimationClip carregado)
- **Downstream:** [Fase 5 — Skeletal Animation](../fase5/skeletal-animation.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §12 Animation System
- [`docs/fase5/skeletal-animation.md`](../fase5/skeletal-animation.md) — extensão para 3D
