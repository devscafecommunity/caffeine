# 🎬 Animation System

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::Animation`  
> **Arquivo:** `src/animation/AnimationSystem.hpp`  
> **Status:** ✅ Implementado  
> **RF:** RF4.9

---

## Visão Geral

Sistema de animação 2D com clipes de sprite e state machine. Cada entidade tem um componente `Animator` que gerencia a state machine de animação.

**Fase 5** adiciona skeletal animation (bones, skinning, blend trees) — ver [`docs/fase5/skeletal-animation.md`](../fase5/skeletal-animation.md).

---

## API

```cpp
namespace Caffeine::Animation {

struct FrameRect {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 w = 0.0f;
    f32 h = 0.0f;
};

struct AnimationClip {
    FixedString<32>        name;
    u32                    fps   = 12;
    std::vector<FrameRect> frames;
    bool                   loop  = true;
    f32 duration() const;
};

struct AnimationTransition {
    FixedString<32>       toState;
    std::function<bool()> condition;
    f32                   blendTime   = 0.1f;
    bool                  hasExitTime = false;
};

struct AnimationState {
    FixedString<32>                  name;
    const AnimationClip*             clip  = nullptr;
    f32                              speed = 1.0f;
    std::vector<AnimationTransition> transitions;
};

struct Animator {
    HashMap<FixedString<32>, AnimationState>     states;
    FixedString<32>                              currentState;
    FixedString<32>                              previousState;
    f32                                          timeInState   = 0.0f;
    f32                                          blendWeight   = 1.0f;
    f32                                          playbackScale = 1.0f;
    bool                                         paused        = false;
    std::vector<std::pair<u32, FixedString<32>>> frameEvents;
    std::function<void(const FixedString<32>&)>  onFrameEvent;
};

class AnimationSystem : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override;

    void play(ECS::World& world, ECS::Entity e, const char* stateName);
    void pause(ECS::World& world, ECS::Entity e);
    void resume(ECS::World& world, ECS::Entity e);
    void setSpeed(ECS::World& world, ECS::Entity e, f32 speed);
    bool isPlaying(ECS::World& world, ECS::Entity e, const char* stateName) const;
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

AnimationState idleState;
idleState.name = "idle";
idleState.clip = &idleClip;
idleState.transitions.push_back({
    FixedString<32>("walk"),
    [&]() { return input.axisValue(Axis::MoveX) != 0.0f; }
});

AnimationState walkState;
walkState.name = "walk";
walkState.clip = &walkClip;
walkState.transitions.push_back({
    FixedString<32>("idle"),
    [&]() { return input.axisValue(Axis::MoveX) == 0.0f; }
});

Animator anim;
anim.states.set(FixedString<32>("idle"), idleState);
anim.states.set(FixedString<32>("walk"), walkState);
anim.currentState = "idle";

world.add<Animator>(player, std::move(anim));

AnimationSystem animSys;
animSys.play(world, player, "attack_1");

world.get<Animator>(player)->frameEvents.push_back({3u, FixedString<32>("attack_hit")});
world.get<Animator>(player)->onFrameEvent = [](const FixedString<32>& evt) {
    if (evt == FixedString<32>("attack_hit")) damageEnemiesInRange();
};

animSys.onUpdate(world, dt);
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
