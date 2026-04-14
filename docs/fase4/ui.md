# 🖼️ UI System

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::UI`  
> **Arquivo:** `src/ui/UISystem.hpp`  
> **Status:** 📅 Planejado  
> **RF:** RF4.11

---

## Visão Geral

Sistema de UI **retained mode** — widgets são entidades ECS com componentes de UI. Isso permite que UI seja afetada por ECS systems (ex: HealthBar reflete automaticamente o valor de `Health` component).

**Fase 6** adiciona Dear ImGui para a interface do editor — ver [`docs/fase6/embedded-ui.md`](../fase6/embedded-ui.md).

---

## API Planejada

```cpp
namespace Caffeine::UI {

// ============================================================================
// @brief  Layout rect em espaço de tela ou frações do parent.
//
//  anchorMin/Max em [0, 1] — (0,0) = canto inferior esquerdo da tela
//  pivot em [0, 1] — (0.5, 0.5) = centro do widget
// ============================================================================
struct RectTransform {
    Vec2   anchorMin = {0, 0};
    Vec2   anchorMax = {1, 1};
    Vec2   pivot     = {0.5f, 0.5f};
    Rect2D offset    = {};  // pixels de deslocamento das anchors
};

// ============================================================================
// @brief  Estilo visual do widget.
// ============================================================================
struct UIStyle {
    Color backgroundColor = {0.1f, 0.1f, 0.1f, 0.9f};
    Color textColor       = Color::WHITE;
    Color borderColor     = {0.3f, 0.3f, 0.3f, 1.0f};
    f32   borderWidth     = 1.0f;
    f32   borderRadius    = 4.0f;
    Font* font            = nullptr;
    f32   fontSize        = 16.0f;
    Vec2  textAlignment   = {0.5f, 0.5f};  // (0,0) = esquerda, (1,1) = direita
};

// ============================================================================
// @brief  Widget base — componente ECS para UI.
// ============================================================================
struct UIWidget {
    enum class Type : u8 {
        Canvas,      // Raiz da hierarquia UI
        Panel,       // Container de outros widgets
        Button,      // Clicável
        Label,       // Texto estático
        ProgressBar, // Barra de progresso (HP, XP, etc.)
        Checkbox,    // Toggle
        Slider       // Valor analógico
    };

    Type          type         = Type::Panel;
    bool          visible      = true;
    bool          interactable = true;
    i32           siblingOrder = 0;     // ordem de renderização entre irmãos
    UIStyle       style;
    RectTransform transform;

    // Callbacks de interação
    std::function<void(ECS::Entity)> onClick;
    std::function<void(ECS::Entity)> onHoverEnter;
    std::function<void(ECS::Entity)> onHoverExit;
    std::function<void(ECS::Entity, f32)> onValueChanged;  // Slider/ProgressBar
};

// ── Widgets específicos ────────────────────────────────────────
struct Button : UIWidget {
    FixedString<64> labelText;
    Color           idleColor    = {0.2f, 0.2f, 0.2f, 1.0f};
    Color           hoverColor   = {0.35f, 0.35f, 0.35f, 1.0f};
    Color           pressedColor = {0.1f, 0.1f, 0.1f, 1.0f};
};

struct Label : UIWidget {
    FixedString<256> text;
    bool             wordWrap = false;
};

struct ProgressBar : UIWidget {
    f32  minValue     = 0.0f;
    f32  maxValue     = 100.0f;
    f32  currentValue = 50.0f;
    bool showText     = false;
    Color fillColor   = {0.2f, 0.8f, 0.2f, 1.0f};  // verde por default
};

struct Slider : UIWidget {
    f32 minValue     = 0.0f;
    f32 maxValue     = 1.0f;
    f32 currentValue = 0.5f;
    bool snapToInt   = false;
};

// ============================================================================
// @brief  Sistema de UI ECS.
//
//  Memory: Widgets alocados no PoolAllocator por tipo.
//  Priority: 500 — depois de physics/animation, antes de render
// ============================================================================
class UISystem : public ECS::ISystem {
public:
    void update(ECS::World& world, f64 dt) override;
    i32  priority() const override { return 500; }
    const char* name() const override { return "UI"; }

    // ── Factory helpers ────────────────────────────────────────
    ECS::Entity createCanvas(ECS::World& world);
    ECS::Entity createButton(ECS::World& world, ECS::Entity parent,
                              const char* text, Vec2 pos, Vec2 size = {120, 40});
    ECS::Entity createLabel(ECS::World& world, ECS::Entity parent,
                             const char* text, Vec2 pos);
    ECS::Entity createProgressBar(ECS::World& world, ECS::Entity parent,
                                   Vec2 pos, Vec2 size = {200, 20});

    // ── Data binding ───────────────────────────────────────────
    // Conecta automaticamente um campo de um componente a um widget
    // Ex: HealthBar.currentValue ← componente Health.current
    void bindComponent(ECS::Entity widget, ECS::Entity target,
                       ECS::ComponentID component, const char* fieldPath);

    // ── Hit testing ────────────────────────────────────────────
    ECS::Entity hitTest(Vec2 screenPos) const;

private:
    void layoutWidgets(ECS::World& world);
    void renderWidget(ECS::Entity e, const UIWidget& widget,
                      RHI::CommandBuffer* cmd);
    void processInput(ECS::World& world, const Input::InputManager& input);
};

}  // namespace Caffeine::UI
```

---

## Hierarquia de Widgets

```
Canvas (root)
  ├── Panel (HUD)
  │     ├── ProgressBar (health)   ← bind → Health.current
  │     ├── Label (score)          ← bind → Score.value
  │     └── Label (fps counter)
  ├── Panel (inventory)
  │     └── [slots dinamicamente criados]
  └── Panel (pause menu — hidden by default)
        ├── Button "Resume"
        ├── Button "Options"
        └── Button "Quit"
```

---

## Exemplos de Uso

```cpp
// ── Criar HUD ─────────────────────────────────────────────────
auto* uiSys = world.registerSystem<UISystem>();

Entity canvas = uiSys->createCanvas(world);
Entity hudPanel = uiSys->createPanel(world, canvas, {{0,0},{1280,720}});

// Health bar
Entity healthBar = uiSys->createProgressBar(world, hudPanel,
    {20, 700}, {200, 20});
world.get<ProgressBar>(healthBar)->fillColor = {0.9f, 0.2f, 0.2f, 1.0f};

// Bind automático: healthBar.currentValue ↔ playerHealth.current
uiSys->bindComponent(healthBar, playerEntity,
    ComponentID::of<Health>(), "current");

// Label de score
Entity scoreLabel = uiSys->createLabel(world, hudPanel, "Score: 0", {1100, 700});

// ── Botão com callback ────────────────────────────────────────
Entity playBtn = uiSys->createButton(world, canvas, "Play Game",
    {640, 360}, {200, 50});
world.get<Button>(playBtn)->onClick = [&](ECS::Entity e) {
    sceneManager.switchScene("assets/scenes/level1.caf");
};

// ── Bind manual via update ────────────────────────────────────
// (alternativa ao bindComponent para lógica customizada)
world.query(scoreQuery, [&](Label& lbl, const Score& score) {
    lbl.text = FixedString<256>("Score: ") + score.value;
});
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Retained mode (vs immediate) | UI persiste entre frames sem reconstrução |
| Widgets como entidades ECS | UI pode ser afetada por systems (bindings automáticos) |
| `bindComponent` | Elimina boilerplate de sincronização manual |
| `priority = 500` | UI após gameplay, mas antes de render final |
| Pool allocator por tipo | Zero alloc em runtime para widgets frequentes |

---

## Critério de Aceitação

- [ ] 50 widgets a 60fps
- [ ] UI render < 2ms
- [ ] `bindComponent` atualiza ProgressBar no mesmo frame que o componente muda
- [ ] Hit testing correto com hierarquia de transforms
- [ ] Widgets com `visible = false` não processados

---

## Dependências

- **Upstream:** [ECS Core](ecs.md), [Fase 3 — RHI](../fase3/rhi.md), [Input System](../fase2/input.md)
- **Downstream:** [Fase 6 — Embedded UI (ImGui)](../fase6/embedded-ui.md)

---

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §14 UI System
- [`docs/fase6/embedded-ui.md`](../fase6/embedded-ui.md) — Dear ImGui para editor
