# 🖼️ UI System

> **Fase:** 4 — O Cérebro  
> **Namespace:** `Caffeine::UI`  
> **Arquivos:** `src/ui/UIComponents.hpp`, `src/ui/UISystem.hpp`  
> **Status:** ✅ Implementado  
> **RF:** RF4.11

---

## Visão Geral

Sistema de UI **retained mode** — widgets são entidades ECS com componentes de UI. Isso permite que UI seja afetada por ECS systems (ex: HealthBar reflete automaticamente o valor de um `Health` component via `bindValue`).

**Fase 6** adiciona Dear ImGui para a interface do editor — ver [`docs/fase6/embedded-ui.md`](../ui/editor-ui.md).

---

## Componentes

### `UIColor`

Cor RGBA com canais `f32` em `[0, 1]`. Presets estáticos: `white()`, `black()`, `transparent()`, `red()`, `green()`, `blue()`.

### `UIRect`

Rect de tela com `position` e `size` (`Vec2`). Métodos: `contains(Vec2)`, `isValid()`.

### `RectTransform`

Layout relativo ao parent. `anchorMin`/`anchorMax` são frações do tamanho do parent; `offsetMin`/`offsetMax` são deltas em pixels.

```
anchorMin=anchorMax={0,0}, offsetMin={px,py}, offsetMax={px+w,py+h}  →  widget fixo em (px,py) tamanho (w,h)
```

### `UIStyle`

Aparência visual: `backgroundColor`, `textColor`, `borderColor`, `borderWidth`, `borderRadius`, `fontSize`, `textAlignment`.

### `UIWidget`

Componente base presente em toda entidade UI:

| Campo | Tipo | Descrição |
|-------|------|-----------|
| `type` | `UIWidgetType` | Canvas, Panel, Button, Label, ProgressBar, Checkbox, Slider |
| `parentId` | `u32` | ID do parent; `kUIInvalidParent` para canvas raiz |
| `visible` | `bool` | Se false, não é processado nem retornado por hitTest |
| `interactable` | `bool` | Se false, ignorado por hitTest |
| `siblingOrder` | `i32` | Desempata hitTest quando dois widgets se sobrepõem |
| `computedRect` | `UIRect` | Preenchido a cada frame por `layoutWidgets()` |
| `onClick` | `std::function<void(Entity)>` | Disparado ao clicar |
| `onHoverEnter` | `std::function<void(Entity)>` | Disparado ao entrar com o mouse |
| `onHoverExit` | `std::function<void(Entity)>` | Disparado ao sair com o mouse |
| `onValueChanged` | `std::function<void(Entity, f32)>` | Disparado por bindValue |

### Componentes específicos

| Struct | Campos relevantes |
|--------|-------------------|
| `UIButton` | `labelText`, `idleColor`, `hoverColor`, `pressedColor`, `isHovered`, `isPressed` |
| `UILabel` | `text`, `wordWrap` |
| `UIProgressBar` | `minValue`, `maxValue`, `currentValue`, `showText`, `fillColor` |
| `UISlider` | `minValue`, `maxValue`, `currentValue`, `snapToInt` |
| `UICheckbox` | `checked`, `checkedColor` |

---

## API — `UISystem`

```cpp
namespace Caffeine::UI {

class UISystem : public ECS::ISystem {
public:
    explicit UISystem(Events::EventBus* eventBus = nullptr);

    void onUpdate(ECS::World& world, f32 dt) override;

    ECS::Entity createCanvas(ECS::World& world, Vec2 size = {1280.0f, 720.0f});
    ECS::Entity createPanel(ECS::World& world, u32 parentId, UIRect rect);
    ECS::Entity createButton(ECS::World& world, u32 parentId, const char* text,
                              Vec2 pos, Vec2 size = {120.0f, 40.0f});
    ECS::Entity createLabel(ECS::World& world, u32 parentId, const char* text, Vec2 pos);
    ECS::Entity createProgressBar(ECS::World& world, u32 parentId,
                                   Vec2 pos, Vec2 size = {200.0f, 20.0f});
    ECS::Entity createSlider(ECS::World& world, u32 parentId,
                              Vec2 pos, Vec2 size = {200.0f, 20.0f});
    ECS::Entity createCheckbox(ECS::World& world, u32 parentId, Vec2 pos);

    void bindValue(ECS::Entity widget, std::function<f32(ECS::World&)> getter);

    ECS::Entity hitTest(ECS::World& world, Vec2 screenPos);

    void injectMousePosition(Vec2 pos);
    void injectMouseClick(bool pressed);
};

}  // namespace Caffeine::UI
```

---

## Hierarquia de Widgets

```
Canvas (root)
  ├── Panel (HUD)
  │     ├── ProgressBar (health)   ← bindValue → retorna Health.current
  │     ├── Label (score)
  │     └── Label (fps counter)
  ├── Panel (inventory)
  │     └── [slots dinamicamente criados]
  └── Panel (pause menu — visible=false por default)
        ├── Button "Resume"
        ├── Button "Options"
        └── Button "Quit"
```

---

## Exemplo de Uso

```cpp
UISystem uiSys;

Entity canvas   = uiSys.createCanvas(world, {1280.0f, 720.0f});
Entity hudPanel = uiSys.createPanel(world, canvas.id(), {{0,0},{1280,720}});

Entity healthBar = uiSys.createProgressBar(world, hudPanel.id(), {20.0f, 700.0f}, {200.0f, 20.0f});
world.get<UIProgressBar>(healthBar)->fillColor = {0.9f, 0.2f, 0.2f, 1.0f};

uiSys.bindValue(healthBar, [&](ECS::World& w) {
    return w.get<Health>(playerEntity)->current;
});

Entity scoreLabel = uiSys.createLabel(world, hudPanel.id(), "Score: 0", {1100.0f, 700.0f});

Entity playBtn = uiSys.createButton(world, canvas.id(), "Play Game", {640.0f, 360.0f}, {200.0f, 50.0f});
world.get<UIWidget>(playBtn)->onClick = [&](ECS::Entity) {
    sceneManager.switchScene("assets/scenes/level1.caf");
};

uiSys.injectMousePosition(mousePos);
uiSys.injectMouseClick(isMouseDown);
uiSys.onUpdate(world, dt);
```

---

## Decisões de Design

| Decisão | Justificativa |
|---------|-------------|
| Retained mode | UI persiste entre frames sem reconstrução |
| Widgets como entidades ECS | UI pode ser afetada por systems (bindings automáticos) |
| `bindValue` com getter `f32` | Evita reflexão em C++ — getter lambda é suficiente para ProgressBar/Slider |
| `UIColor` com `f32` (não `Color` do engine) | `Color` do engine usa `u8`; UI precisa de precisão float |
| Layout em 8 passes BFS | ECS forEach não garante ordem topológica; passes extras resolvem hierarquias profundas |
| `kUIInvalidParent = 0xFFFFFFFFu` | Sentinel de "sem parent" compatível com `u32` |

---

## Critério de Aceitação

- [x] Canvas cria entidade com `UIWidgetType::Canvas` e `computedRect` correto
- [x] `createButton`, `createLabel`, `createProgressBar`, `createSlider`, `createCheckbox` retornam entidades válidas com componentes corretos
- [x] `onUpdate` em world vazio não crasha
- [x] Layout calcula `computedRect` correto para filhos diretos do canvas
- [x] `hitTest` retorna entidade correta para ponto dentro do rect
- [x] `hitTest` ignora widgets com `visible=false` ou `interactable=false`
- [x] `hitTest` desempata por `siblingOrder` (maior vence)
- [x] `onClick` dispara ao clicar no widget
- [x] `onHoverEnter` dispara ao entrar com o mouse
- [x] `bindValue` atualiza `UIProgressBar::currentValue` no mesmo frame

---

## Dependências

- **Upstream:** [ECS Core](ecs.md), [Fase 1 — Memory, Containers](../memory/allocators.md)
- **Downstream:** [Fase 6 — Embedded UI (ImGui)](../ui/editor-ui.md)

---

## 🔗 Tópicos Relacionados

| Tópico | Descrição |
|--------|-----------|
| [Sistemas de Jogo]() | UI retained mode como sistema ECS |
| [Editor & Debug]() | UI do jogo (F4) vs UI do editor (F6) |

## Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §14 UI System
- [`docs/fase6/embedded-ui.md`](../ui/editor-ui.md) — Dear ImGui para editor
- [Índice de Tópicos Transversais]()
