# 🛠️ Caffeine Studio IDE — Plano de Desenvolvimento

> **Milestone:** 2.0 — Caffeine Studio  
> **Namespace:** `Caffeine::Editor`  
> **Estado:** 📅 Planeamento  
> **RFs:** RF6.1–RF6.6, RF4.7

---

## Sumário Executivo

O Caffeine Studio IDE é a camada de ferramentas visuais da Caffeine Engine. Atualmente existem componentes-base implementados (ImGuiIntegration, ProfilerWindow, ConsoleWindow, StatsOverlay, AssetPipeline CLI), mas falta o **Scene Editor** — o coração do IDE — e os sistemas de **Scripting**, **Gestão de Projetos** e **Ferramentas Avançadas**.

Este plano divide o desenvolvimento em **4 milestones** progressivas:

| Milestone | Nome | Foco | Dependências |
|:---------:|------|------|--------------|
| **M1** | Scene Editor Foundation | Painéis core, viewport, serialização | ECS, RHI, ImGui |
| **M2** | Visual Editing & Assets | Gizmos, drag-and-drop, pipeline integrado, projeto | M1 + AssetPipeline |
| **M3** | Scripting & Runtime | LuaVM, ScriptSystem, bindings ECS, hot-reload | M1 + ECS |
| **M4** | Advanced Tools & Polish | Timeline, tilemap, materiais, build, prefabs, extensões | M1–M3 |

> **Nota:** Cada milestone é autónoma — pode ser entregue e testada independentemente.

---

## Estado Atual (Pré-M1)

### ✅ Já Implementado

| Componente | Ficheiros | Estado |
|------------|-----------|--------|
| **ImGuiIntegration** | `src/editor/ImGuiIntegration.hpp` | ✅ Integração SDL3+ImGui |
| **ProfilerWindow** | `src/editor/ProfilerWindow.hpp` | ✅ Gráfico de frame times |
| **ConsoleWindow** | `src/editor/ConsoleWindow.hpp` | ✅ Log filtering por nível |
| **StatsOverlay** | `src/editor/StatsOverlay.hpp` | ✅ FPS, cache stats |
| **EditorTypes** | `src/editor/EditorTypes.hpp` | ✅ FrameStats, tipos base |
| **AssetPipeline CLI** | `src/tools/*.hpp` | ✅ Texture, Audio, Mesh encoders |
| **UISystem (game)** | `src/ui/UISystem.hpp` | ✅ UI retained mode para jogos |
| **SceneSerializer** | `src/scene/SceneSerializer.hpp` | ✅ Serialização `.caf` |
| **ECS Core** | `src/ecs/World.hpp` | ✅ Archetypes, queries, systems |
| **RHI** | `src/rhi/RenderDevice.hpp` | ✅ SDL_GPU abstraction |

### ❌ Por Implementar

| Componente | Prioridade | Depende de |
|------------|-----------|------------|
| SceneEditor (orquestrador) | 🔴 Crítico | ImGuiIntegration, ECS |
| HierarchyPanel | 🔴 Crítico | ECS, Scene |
| InspectorPanel | 🔴 Crítico | ECS (component reflection) |
| SceneViewport | 🔴 Crítico | RHI, BatchRenderer, Camera |
| AssetBrowser | 🟡 Alto | AssetManager |
| Transform Gizmos | 🟡 Alto | SceneViewport |
| Project Manager | 🟡 Alto | Sistema de ficheiros |
| LuaVM + ScriptSystem | 🟡 Alto | ECS, sol2 |
| Animation Timeline | 🟢 Médio | Animation ECS |
| Tilemap Editor | 🟢 Médio | SceneEditor |
| Material/Shader Editor | 🟢 Médio | RHI |
| Build System | 🟢 Médio | CMake |
| Prefab System | 🔵 Baixo | ECS, SceneSerializer |

---

## M1 — Scene Editor Foundation

> **Objetivo:** Transformar o overlay de debug num editor de cenas funcional com os 4 painéis principais.

### Escopo

#### 1.1 EditorContext & Undo/Redo

Estado global de seleção e operações:

```cpp
struct EditorCommand {
    enum Type { AddEntity, RemoveEntity, AddComponent,
                RemoveComponent, SetField, MoveEntity };
    Type type;
    // Serialized before/after state for undo
    std::vector<u8> beforeState;
    std::vector<u8> afterState;
};

class UndoStack {
    static constexpr u32 MAX_UNDO = 256;
    EditorCommand m_commands[MAX_UNDO];
    u32 m_head = 0, m_tail = 0, m_count = 0;
public:
    void push(const EditorCommand& cmd);
    bool undo(ECS::World& world);
    bool redo(ECS::World& world);
    void clear();
};
```

#### 1.2 HierarchyPanel

Árvore de entidades com suporte a:

- Exibição hierárquica (Parent component)
- Renomear (NameComponent)
- Criar entidade vazia
- Deletar (Delete key)
- Drag-and-drop para reparenting
- Context menu (right-click: Create, Duplicate, Delete, Copy/Paste)
- Search/filter bar no topo

#### 1.3 InspectorPanel

Edição de componentes da entidade selecionada:

- **Drawers built-in:** Transform, SpriteRenderer, RigidBody2D, Camera2D, AudioSource, NameComponent
- **Auto-detection:** se o componente não tem drawer, mostrar raw fields (tipo + valor)
- **Add Component dropdown:** lista todos os tipos de componente registados
- **Remove Component:** botão X em cada header de componente
- **Reset to default:** botão em cada field individual
- **Struct nesting:** campos Vec2, Vec3, Color expandidos inline

#### 1.4 SceneViewport

Viewport com offscreen rendering:

- Framebuffer separado (RHI::TextureHandle)
- Renderização da cena via BatchRenderer
- ImGui::Image para exibir o framebuffer
- Botões de toolbar: grid toggle, gizmo mode (W/E/R), camera snap
- Suporte a zoom (scroll) e pan (middle-click drag)
- **Grid 2D** desenhada no framebuffer

#### 1.5 AssetBrowser (v1)

Navegação básica de ficheiros:

- Listagem de diretórios e ficheiros `.caf`
- Navegação por pastas (double-click)
- Path bar clicável
- Refresh button
- Preview de textura ao selecionar (via AssetManager)

#### 1.6 SceneEditor Orchestrator

Layout docking usando Dear ImGui Docking:

```
┌─────────────────────────────────────────────────────────────┐
│  Menu: File | Edit | View | Build | Run                    │
├───────────┬───────────────────────────────┬─────────────────┤
│Hierarchy  │       Scene Viewport          │   Inspector     │
│  Panel    │                               │     Panel       │
│           │                               │                 │
│    ───    ├───────────────────────────────┴─────────────────┤
│           │  Asset Browser  |  Console  |  Profiler         │
├───────────┴─────────────────────────────────────────────────┤
│  Status Bar: Entity count | FPS | Scene path | Dirty flag   │
└─────────────────────────────────────────────────────────────┘
```

- File > New Scene / Open / Save / Save As
- Edit > Undo / Redo / Preferences
- Integração com ProfilerWindow e ConsoleWindow existentes

#### 1.7 Save/Load Scene

- `SceneEditor::saveScene(path)` → delega no SceneSerializer
- `SceneEditor::loadScene(path)` → limpa world + deserializa
- Indicador de dirty (ficheiro não salvo)
- Prompt "Save changes?" ao fechar cena suja

### Critério de Aceitação (M1)

- [ ] HierarchyPanel mostra todas as entidades em árvore com nomes
- [ ] Clicar numa entidade na Hierarchy seleciona-a e atualiza o Inspector
- [ ] InspectorPanel permite editar Transform (pos, rot, scale) com sliders/input
- [ ] InspectorPanel permite editar SpriteRenderer (texture, color, flip)
- [ ] Add Component dropdown mostra componentes disponíveis
- [ ] SceneViewport renderiza a cena para framebuffer offscreen
- [ ] SceneViewport exibe o framebuffer via ImGui::Image
- [ ] Scroll no viewport faz zoom, middle-click arrasta a câmara
- [ ] AssetBrowser lista diretórios e ficheiros `.caf`
- [ ] File > Save serializa a cena para `.caf` e limpa dirty flag
- [ ] File > Open deserializa `.caf` e popula o ECS
- [ ] Undo/Redo funcionam para AddEntity e RemoveEntity
- [ ] Docking layout persiste entre sessões
- [ ] ProfilerWindow e ConsoleWindow continuam a funcionar no novo layout

---

## M2 — Visual Editing & Assets

> **Objetivo:** Tornar o editor visualmente produtivo — gizmos, drag-and-drop, pipeline de assets integrado.

### Escopo

#### 2.1 Transform Gizmos

Implementação de gizmos 2D (inspirado em ImGuizmo):

```
Translate (W):       Rotate (E):          Scale (R):
     ▲ Y                  ◯                    ■ Y
     │                 ╱     ╲                 │
     └────► X          │       │               └────■ X
   ╱                 ╲       ╱               ╱
 ╱ Z (3D)              ╲   ╱               ╱ Z (3D)
```

- Arrastar eixo: modifica apenas esse eixo
- Arrastar centro: modificação livre (2D) ou plano (3D)
- Snap: Shift+drag usa snapping configurável (16px, 45° rotation)
- Highlight a hover (eixo fica amarelo)
- Gizmo mode switcher na toolbar ou teclas W/E/R/Q

#### 2.2 Drag-and-Drop

- Arrastar AssetBrowser → SceneViewport cria entidade com SpriteRenderer + Transform
- Arrastar AssetBrowser → Inspector field de textura atribui diretamente
- Arrastar Hierarchy → reorganiza parent/child

#### 2.3 Asset Pipeline Integration

- **Auto-import:** ao detetar novo ficheiro em `assets/raw/`, converter automaticamente para `.caf`
- **Progress bar** para conversões longas
- **Log de conversão** no ConsoleWindow
- **Re-import button** no AssetBrowser (context menu)
- **Asset manifest** (`assets/manifest.caf`) com metadados por asset

#### 2.4 Hot-Reload de Assets

- AssetManager::pollFileChanges() already exists
- Atualizar textura na GPU sem restart
- Mostrar toast notification: "hero.caf reloaded"
- Suporte a: texturas, shaders HLSL/GLSL

#### 2.5 Project Manager

```
┌─────────────────────────────────────────────┐
│       ☕ Caffeine Studio — Project Manager   │
├─────────────────────────────────────────────┤
│                                             │
│  Recent Projects:                           │
│  ┌─────────────────────────────────────┐    │
│  │ 📁 MyGame       C:/projects/mygame  │    │
│  │ 📁 Platformer    .../platformer     │    │
│  │ 📁 RPG           .../rpg           │    │
│  └─────────────────────────────────────┘    │
│                                             │
│  [  New Project  ]  [  Open Project  ]      │
│                                             │
└─────────────────────────────────────────────┘
```

- New Project Wizard: nome, path, template (2D/3D/Empty)
- Project structure criada automaticamente: `assets/raw/`, `scripts/`, `build/`
- `project.caffeine` ficheiro de configuração do projeto (JSON)
- Lembrar último projeto aberto (auto-open)

#### 2.6 AssetBrowser (v2)

- Thumbnails gerados para texturas (miniaturas GPU)
- Preview panel: ao selecionar asset, mostra preview
- Drag preview: thumbnail segue o cursor durante drag
- Search/filter por nome e tipo
- Grid view / list view toggle
- Folder creation (right-click > New Folder)

#### 2.7 Multiple Scene Tabs

- Abas para cada cena aberta (tab bar no topo do viewport)
- Cada aba tem o seu próprio ECS World
- Arrastar tab para desacoplar em janela flutuante

### Critério de Aceitação (M2)

- [ ] Gizmo Translate arrasta entidade no eixo X/Y
- [ ] Gizmo Rotate roda entidade visualmente
- [ ] Gizmo Scale escala entidade no eixo ou uniformemente
- [ ] Snap funciona com Shift+drag
- [ ] Drag de textura do AssetBrowser para o Viewport cria entidade
- [ ] Drag de textura para Inspector field atribui diretamente
- [ ] Asset Pipeline converte PNG/WAV automaticamente em background
- [ ] Hot-reload atualiza textura modificada sem restart
- [ ] Project Manager cria novo projeto com estrutura de pastas
- [ ] AssetBrowser mostra thumbnails de texturas
- [ ] Múltiplas scenes abertas em tabs funcionam independentemente
- [ ] Toast notification aparece em hot-reload e conversões

---

## M3 — Scripting & Runtime

> **Objetivo:** Permitir scripting Lua com bindings ECS, hot-reload e integração total no editor.

### Escopo

#### 3.1 LuaVM

Implementação da máquina virtual Lua com sol2:

```cpp
class LuaVM {
    sol::state m_lua;
public:
    bool init();
    void shutdown();
    bool loadScript(std::string_view path);
    bool reloadScript(std::string_view path);
    template<typename... Args>
    bool call(std::string_view function, Args&&... args);
    sol::state& state() { return m_lua; }
    
private:
    void registerCaffeineBindings();
    void registerEntityBindings();
    void registerTransformBindings();
    void registerInputBindings();
    void registerEventBindings();
    void registerDebugBindings();
    void registerMathBindings();
};
```

#### 3.2 ScriptComponent + ScriptSystem

```cpp
struct ScriptComponent {
    Assets::AssetHandle<ScriptAsset> script;
};

class ScriptSystem : public ECS::ISystem {
    LuaVM* m_vm;
public:
    void onInit(ECS::World& world) override;
    void onUpdate(ECS::World& world, f32 dt) override;
    void onShutdown(ECS::World& world) override;
    void initNewScripts(ECS::World& world);
};
```

Ciclo de vida Lua exposto:
- `onCreate(entity)` — chamado uma vez após criação
- `onUpdate(entity, dt)` — chamado todo frame
- `onDestroy(entity)` — chamado antes de destruição
- `onCollision(entity, other)` — chamado ao colidir

#### 3.3 Bindings ECS → Lua

```lua
local e = caffeine.world.create()
caffeine.world.addTransform(e, { x=100, y=200 })
caffeine.world.addSprite(e, { texture="hero.caf" })

local t = caffeine.world.getTransform(e)
t.x = t.x + 10
caffeine.world.setTransform(e, t)

if caffeine.input.isKeyDown("Space") then
    -- ...
end

local mx, my = caffeine.input.mousePosition()
```

#### 3.4 Sandboxing

| Permitido | Bloqueado |
|-----------|-----------|
| `caffeine.*` APIs | `os.*`, `io.*` (excepto editor mode) |
| `math.*`, `string.*`, `table.*` | `load`, `loadstring` |
| `require` (apenas módulos do projeto) | `debug.*` |

#### 3.5 ScriptWatcher

- Monitoriza `scripts/` com file timestamps
- Hot-reload de `.lua` modificado via LuaVM::reloadScript()
- Log de erros de compilação Lua no ConsoleWindow
- Indicador visual no editor: "Script modified — reloaded"

#### 3.6 Editor Integration

- InspectorPanel: field para atribuir script a ScriptComponent
- Script editor: botão "Edit Script" abre ficheiro `.lua` no editor de sistema
- Syntax highlight preview (futuro: editor embutido)
- Error panel: erros Lua mostrados com linha e mensagem

#### 3.7 Basic Particle System

- ParticleEmitter component (ECS)
- Lua API para spawn de partículas
- Visual preview no SceneViewport (play mode)
- Suporte a: lifetime, velocity, color over time, size over time

### Critério de Aceitação (M3)

- [ ] LuaVM executa script `.lua` simples (`caffeine.debug.log("hello")`)
- [ ] ScriptComponent pode ser adicionado via InspectorPanel
- [ ] `onUpdate` é chamado todo frame para entidades com ScriptComponent
- [ ] Bindings de Transform (get/set pos, rot, scale) funcionam
- [ ] Bindings de Input (isKeyDown, getAxis) funcionam
- [ ] Bindings de Eventos (on, emit) funcionam
- [ ] Erros Lua são logados no ConsoleWindow sem crash
- [ ] Hot-reload: modificar `.lua` recarrega sem restart
- [ ] Sandbox bloqueia `os.execute()`, `io.open()`
- [ ] ParticleEmitter cria partículas com parâmetros configuráveis
- [ ] Script pode ser atribuído/removido via Inspector

---

## M4 — Advanced Tools & Polish

> **Objetivo:** Completar o IDE com ferramentas de produção — animação, tilemap, materiais, build, prefabs, extensões.

### Escopo

#### 4.1 Animation Timeline Editor

Editor de animação estilo timeline:

```
┌─────────────────────────────────────────────────────────────┐
│  Animation: "hero_idle"    [▶ Play]  [■ Stop]  [⟳ Loop]   │
│  Duration: 1.2s    FPS: 12                                 │
├─────────────────────────────────────────────────────────────┤
│  Timeline:  |──●──●──●──●──●──●──●──●──●──●──●──●──|      │
│             0s   0.1  0.2  0.3  0.4  0.5  0.6  ...  1.2s   │
├─────────────────────────────────────────────────────────────┤
│  Tracks:                                                     │
│  ▸ Sprite     [hero_1] [hero_2] [hero_3] ...               │
│  ▸ Position   ●─────────────●──────────────●               │
│  ▸ Rotation   ───●─────────────────────●────               │
│  ▸ Events     [play_sfx]        [spawn_particle]            │
└─────────────────────────────────────────────────────────────┘
```

- Keyframe creation and editing
- Sprite track (frame indices)
- Transform tracks (position, rotation, scale)
- Event track (calls Lua functions at specific times)
- Onion skinning (semi-transparent previous/next frame)
- Export to `.caf` animation asset

#### 4.2 Tilemap Editor

Editor de níveis 2D baseado em tiles:

- Tile palette with drag-and-drop placement
- Multiple layers (background, foreground, collision)
- Auto-tiling (rule-based tile placement)
- Bucket fill, erase, selection tools
- Layer visibility and locking
- Export to `.caf` tilemap asset

#### 4.3 Material/Shader Editor

- Visual shader graph (nodes) ou editor de código HLSL/GLSL
- Preview de shader em tempo real no viewport
- Propriedades expostas editáveis (sliders, cores)
- Compilação com feedback de erros
- Biblioteca de shaders pré-definidos (unlit, lit, sprite, post-process)

#### 4.4 Audio Preview & Spatial Placement

- Waveform preview no Inspector
- Play/Stop no AssetBrowser
- Spatial audio placement no SceneViewport (AudioSource gizmo)
- Volume slider, pitch slider, falloff distance
- Mixing desk: bus de áudio, volume por category (SFX, Music, Voice)

#### 4.5 Build System Integration

- One-click "Build & Run"
- Build configuration dialog (Debug/Release, platforms)
- Build progress in ConsoleWindow
- Post-build: open output directory
- Build settings stored in `project.caffeine`

#### 4.6 Prefab System

- Create prefab from entity (save as `.prefab.caf`)
- Prefab instance: linked copy (updates propagate)
- Prefab override: campos podem diferir da instância
- Prefab nesting (prefab dentro de prefab)
- AssetBrowser mostra prefabs com ícone distinto
- Drag prefab do AssetBrowser → Viewport instancia

#### 4.7 Entity Debugger

Ferramenta de debug visual de entidades:

```
┌────────────────────────────────────────────┐
│  🔬 Entity Debugger                        │
├────────────────────────────────────────────┤
│  Entity: 142 (Hero)                        │
│  Archetype: [Transform, Sprite, RigidBody] │
│                                            │
│  Components:                               │
│  ✅ Transform    size: 28 bytes            │
│     └ pos: [120.0, 340.0]                 │
│  ✅ Sprite       size: 16 bytes            │
│  ✅ RigidBody2D  size: 32 bytes            │
│                                            │
│  Memory: 76 bytes total                    │
│  Queries matching: MovementSystem,         │
│                    RenderSystem, Physics    │
└────────────────────────────────────────────┘
```

- Entity selector (pick from viewport)
- Component list with memory size
- Archetype info
- Matching systems list
- Component byte viewer (hex dump)

#### 4.8 Extension/Plugin System

- Plugin loading from `plugins/` directory
- Plugin API: register panels, menu items, component drawers
- Hot-reload de plugins (shared libraries)
- Plugin marketplace UI (list available, install, update)
- Example plugin: "Caffeine Script Editor" (embutido com syntax highlight)

### Critério de Aceitação (M4)

- [ ] Animation Timeline permite criar keyframes e reproduzir
- [ ] Onion skinning mostra frame anterior semi-transparente
- [ ] Tilemap Editor coloca tiles em múltiplas camadas
- [ ] Auto-tiling funciona com regras configuráveis
- [ ] Material Editor permite alterar parâmetros de shader
- [ ] Preview de shader compila e mostra resultado em tempo real
- [ ] Audio preview toca som no AssetBrowser
- [ ] Spatial audio gizmo mostra alcance no viewport
- [ ] Build & Run compila o projeto e inicia o jogo
- [ ] Prefab instancia com linked copy; alterações propagam
- [ ] Entity Debugger mostra archetype e memória por componente
- [ ] Plugin system carrega `.dll`/`.so` e regista novo painel

---

## Dependências entre Milestones

```
M1 ─────────────────────────────────────────────►
  │                                                  │
  ├── ECS Core (World, Entity, queries)          ✅  │
  ├── RHI (RenderDevice, CommandBuffer)           ✅  │
  ├── ImGuiIntegration                            ✅  │
  ├── BatchRenderer, Camera2D                     ✅  │
  ├── SceneSerializer                             ✅  │
  ├── AssetManager                                ✅  │
  └── ProfilerWindow, ConsoleWindow               ✅  │
                                                   │
M2 ─────────────────────────────────────────────►  │
  │                                                  │
  ├── M1 + SceneViewport                          🔲  │
  ├── AssetPipeline encoders                      ✅  │
  └── FileWatcher (AssetManager::pollFileChanges) ✅  │
                                                   │
M3 ─────────────────────────────────────────────►  │
  │                                                  │
  ├── M1                                           🔲  │
  ├── sol2 (Lua binding)                          ⬜  │
  └── Debug::LogSystem, Debug::DebugDraw          ✅  │
                                                   │
M4 ─────────────────────────────────────────────►  │
  │                                                  │
  ├── M1, M2, M3                                  🔲  │
  └── Animation ECS, Physics 2D                    ✅  │
```

---

## Riscos & Mitigações

| Risco | Impacto | Probabilidade | Mitigação |
|-------|---------|:------------:|-----------|
| ImGui Docking não estável | M1 atrasado | Baixa | Usar fallback manual layout |
| sol2 com C++20 complexo | M3 atrasado | Média | Protótipo isolado antes de integrar |
| Gizmos matemáticos complexos | M2 atrasado | Média | Usar ImGuizmo como base |
| Hot-reload de shaders instável | M2 atrasado | Alta | Implementar fallback com shader default |
| Prefab serialização complexa | M4 atrasado | Média | Começar com prefabs flat (sem nesting) |
| Plugin system cross-platform | M4 atrasado | Alta | Windows-first, Linux/Mac depois |

---

## Estimativa de Esforço

| Milestone | Ficheiros | Ficheiros Novos | Estimativa |
|:---------:|:---------:|:---------------:|:----------:|
| M1 | ~15 | 8 (.hpp + .cpp) | 3–4 semanas |
| M2 | ~10 | 5 | 2–3 semanas |
| M3 | ~12 | 6 | 3–4 semanas |
| M4 | ~20 | 12 | 4–6 semanas |

> **Total estimado:** 12–17 semanas (3–4 meses) para o Caffeine Studio IDE completo.

---

## Referências

- [`docs/editor/scene-editor.md`](../docs/editor/scene-editor.md) — Documentação atual do Scene Editor
- [`docs/ui/editor-ui.md`](../docs/ui/editor-ui.md) — Embedded UI / ImGuiIntegration
- [`docs/ui/game-ui.md`](../docs/ui/game-ui.md) — UI System (game HUD)
- [`docs/scripting/scripting.md`](../docs/scripting/scripting.md) — Scripting (Lua)
- [`docs/assets/asset-pipeline.md`](../docs/assets/asset-pipeline.md) — Asset Pipeline CLI
- [`docs/ecs/core.md`](../docs/ecs/core.md) — ECS Core
- [`docs/ecs/scene.md`](../docs/ecs/scene.md) — Scene Manager / Serializer
- [`docs/rendering/rhi.md`](../docs/rendering/rhi.md) — RHI
- [`docs/debug/debug-tools.md`](../docs/debug/debug-tools.md) — LogSystem, Profiler
- [`docs/architecture_specs.md`](../docs/architecture_specs.md) — Technical specifications
- [Dear ImGui Docking](https://github.com/ocornut/imgui/wiki/Docking)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) — Gizmo library
- [sol2](https://github.com/ThePhD/sol2) — Lua/C++ binding
- [Lua 5.4 Manual](https://www.lua.org/manual/5.4/)
