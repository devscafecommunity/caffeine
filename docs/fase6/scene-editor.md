# 🎬 Scene Editor

> **Fase:** 6 — O Olimpo  
> **Namespace:** `Caffeine::Editor`  
> **Status:** 📅 Planejado  
> **RFs:** RF6.3, RF6.4

---

## Visão Geral

O **Scene Editor** é o coração do Caffeine Studio IDE. Permite criar, inspecionar e modificar entidades ECS visualmente, com suporte a drag-and-drop de assets, hierarquia de entidades, painel de inspeção de componentes e gizmos de transformação 2D/3D.

O editor **não substitui** o código C++ — ele é uma ferramenta de produtividade que lê e escreve os mesmos dados ECS usados em runtime.

**Componentes do editor:**
| Painel | Responsabilidade |
|--------|-----------------|
| `HierarchyPanel` | Árvore de todas as entidades da cena |
| `InspectorPanel` | Edição de componentes da entidade selecionada |
| `SceneViewport` | Viewport com gizmos de transformação |
| `AssetBrowser` | Browser de assets do projeto com drag-and-drop |
| `SceneEditor` | Orquestrador dos 4 painéis acima |

---

## API Planejada

```cpp
namespace Caffeine::Editor {

// ============================================================================
// @brief  Estado global de seleção no editor.
// ============================================================================
struct EditorContext {
    ECS::Entity selectedEntity   = ECS::INVALID_ENTITY;
    ECS::Entity hoveredEntity    = ECS::INVALID_ENTITY;
    bool        isDirty          = false;   // cena modificada sem salvar

    enum class GizmoMode { Translate, Rotate, Scale } gizmoMode = GizmoMode::Translate;
    enum class GizmoSpace { Local, World }             gizmoSpace = GizmoSpace::World;
};

// ============================================================================
// @brief  Painel de hierarquia — exibe todas as entidades da cena em árvore.
//
//  Suporta:
//  - Hierarquia Parent/Child via componente Caffeine::Scene::Parent
//  - Renomear entidades (componente NameComponent)
//  - Criar entidade vazia (botão +)
//  - Deletar entidade selecionada (tecla Delete)
//  - Drag-and-drop para reorganizar hierarquia
// ============================================================================
class HierarchyPanel {
public:
    void render(ECS::World& world, EditorContext& ctx);

private:
    void renderEntityNode(ECS::World& world, ECS::Entity entity,
                          EditorContext& ctx);
    void renderContextMenu(ECS::World& world, ECS::Entity entity,
                           EditorContext& ctx);
};

// ============================================================================
// @brief  Painel de inspeção — edita componentes da entidade selecionada.
//
//  Cada tipo de componente tem um "drawer" registrado que sabe como
//  renderizar campos editáveis via ImGui.
//
//  Suporta:
//  - Adicionar/remover componentes
//  - Edição inline de f32, Vec2, Vec3, bool, strings
//  - Reset de campos ao valor padrão
// ============================================================================
class InspectorPanel {
public:
    void render(ECS::World& world, EditorContext& ctx);

    // Registro de drawers para tipos de componentes customizados
    using ComponentDrawer = std::function<void(void* componentData)>;
    void registerDrawer(ECS::ComponentID id, ComponentDrawer drawer);

private:
    HashMap<ECS::ComponentID, ComponentDrawer> m_drawers;

    // Drawers built-in para componentes da Caffeine:
    void drawTransform(Scene::Transform& t);
    void drawSprite(Render::SpriteRenderer& s);
    void drawRigidBody2D(Physics2D::RigidBody& rb);
    void drawAudioSource(Audio::AudioSource& as);
    void drawCamera(Render::Camera2D& cam);
};

// ============================================================================
// @brief  Viewport com gizmos de transformação.
//
//  Renderiza a cena em um framebuffer separado (offscreen) e exibe
//  dentro de uma janela ImGui. Sobrepõe gizmos 2D/3D sobre o framebuffer.
//
//  Gizmos (RF6.4):
//  - Translate: arrastar eixos X/Y/Z
//  - Rotate:    girar em torno de eixo
//  - Scale:     escalar por eixo ou uniforme
//
//  Teclas de atalho (quando viewport em foco):
//  - W = Translate, E = Rotate, R = Scale
//  - Q = sem gizmo (seleção apenas)
// ============================================================================
class SceneViewport {
public:
    struct Config {
        u32   width  = 1280;
        u32   height = 720;
        bool  grid   = true;        // mostrar grade 2D
        f32   gridSpacing = 64.0f;  // pixels por célula de grade
    };

    bool init(RHI::RenderDevice* device, Config cfg = {});
    void shutdown();

    // Renderiza cena no offscreen buffer e exibe como imagem ImGui
    void render(ECS::World& world, EditorContext& ctx,
                const Render::Camera2D& editorCamera);

    // Retorna true se o mouse está sobre o viewport (não sobre painéis)
    bool isHovered() const { return m_hovered; }

private:
    RHI::TextureHandle m_framebuffer;
    Config             m_config;
    bool               m_hovered = false;

    void renderGrid(RHI::CommandBuffer* cmd);
    void renderGizmos(ECS::World& world, EditorContext& ctx);
    void renderTranslateGizmo(const Scene::Transform& t, EditorContext& ctx);
    void renderRotateGizmo(const Scene::Transform& t, EditorContext& ctx);
    void renderScaleGizmo(const Scene::Transform& t, EditorContext& ctx);
    void handleGizmoInput(ECS::World& world, EditorContext& ctx);
};

// ============================================================================
// @brief  Browser de assets do projeto com drag-and-drop.
//
//  Exibe os assets em `/assets/` com thumbnails e permite:
//  - Navegar por pastas
//  - Arrastar assets para o SceneViewport → cria entidade
//  - Preview de textura ao hover
//  - Renomear / deletar assets (operações no sistema de arquivos)
// ============================================================================
class AssetBrowser {
public:
    struct Entry {
        std::filesystem::path path;
        Assets::AssetType     type;       // Texture, Audio, Mesh, etc.
        RHI::TextureHandle    thumbnail;  // preview da textura
    };

    void init(Assets::AssetManager* assetManager, std::string_view rootPath);
    void render(EditorContext& ctx);

    // Drag payload: retorna o path do asset sendo arrastado, ou {} se nenhum.
    std::optional<std::filesystem::path> getDroppedAsset() const;

private:
    std::vector<Entry>           m_entries;
    std::filesystem::path        m_currentDir;
    Assets::AssetManager*        m_assetManager = nullptr;
    std::optional<std::filesystem::path> m_droppedAsset;

    void refreshDirectory();
    void renderEntry(const Entry& entry);
};

// ============================================================================
// @brief  Orquestrador de todos os painéis do editor.
//
//  Layout padrão (docking ImGui):
//
//   ┌──────────────────────────────────────────────────────────┐
//   │  [Menu Bar]  File | Edit | View | Build | Run           │
//   ├────────┬───────────────────────────┬────────────────────┤
//   │Hierarch│                           │   Inspector        │
//   │  Panel │    Scene Viewport         │   Panel            │
//   │        │    (offscreen render)     │                    │
//   │        │    [W][E][R] gizmos       │  ─ Transform ─     │
//   │        │                           │  ─ Sprite ─        │
//   ├────────┴───────────────────────────┴────────────────────┤
//   │  Asset Browser  |  Console  |  Profiler                 │
//   └──────────────────────────────────────────────────────────┘
// ============================================================================
class SceneEditor {
public:
    bool init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
              std::string_view assetsPath);
    void shutdown();

    // Chame no loop principal entre imgui.beginFrame() e imgui.endFrame()
    void render(ECS::World& world, Render::Camera2D& editorCamera);

    // Serialização da cena
    bool saveScene(std::string_view path);
    bool loadScene(std::string_view path, ECS::World& world);

private:
    EditorContext  m_ctx;
    HierarchyPanel m_hierarchy;
    InspectorPanel m_inspector;
    SceneViewport  m_viewport;
    AssetBrowser   m_assetBrowser;

    bool           m_dockingSetup = false;

    void renderMenuBar(ECS::World& world);
    void setupDockingLayout();

    // Cria entidade ao soltar asset no viewport
    void handleAssetDrop(ECS::World& world, const std::filesystem::path& asset);
};

}  // namespace Caffeine::Editor
```

---

## Layout Visual do Editor

```
┌──────────────────────────────────────────────────────────────────────┐
│  Caffeine Studio — Scene: "Level01.caf"               ● unsaved     │
│  File | Edit | View | Build | Run                                   │
├────────────┬──────────────────────────────────┬─────────────────────┤
│ Hierarchy  │                                  │  Inspector          │
│            │         Scene Viewport            │                     │
│ ▸ Root     │                                  │  Entity: "Hero"     │
│   ├─ Hero  │   +──────────+                  │  ────────────────── │
│   ├─ Enemy │   │  Sprite  │                  │  ▾ Transform        │
│   └─ Floor │   │  (Gizmo) │                  │    Pos: [120, 340]  │
│            │   │   ←→     │                  │    Rot:  45.0°      │
│            │   +──────────+                  │    Scl: [1.0, 1.0]  │
│            │                                  │  ▾ Sprite Renderer  │
│            │   Grid: ON    Translate [W]      │    Tex: hero.caf    │
│            │                                  │  ▾ RigidBody2D      │
│            │                                  │    Mass: 1.0        │
├────────────┴──────────────────────────────────┴─────────────────────┤
│  Assets /sprites/          │  Console   │  Profiler                 │
│  📁 hero.caf  📁 floor.caf │  [INFO]... │  Frame: 16.7ms / 60fps   │
└────────────────────────────────────────────────────────────────────-─┘
```

---

## Integração com ECS e SceneSerializer

```cpp
// Salvar cena via menu File > Save:
void SceneEditor::saveScene(std::string_view path) {
    Scene::SceneSerializer serializer;
    serializer.serialize(m_world, path);    // → .caf binário (ver fase4/scene.md)
    m_ctx.isDirty = false;
}

// Carregar cena ao abrir arquivo:
void SceneEditor::loadScene(std::string_view path, ECS::World& world) {
    world.clear();                          // destrói todas as entidades atuais
    Scene::SceneSerializer serializer;
    serializer.deserialize(world, path);    // → popula o ECS
    m_ctx.selectedEntity = ECS::INVALID_ENTITY;
}

// Criar entidade ao soltar texture no viewport:
void SceneEditor::handleAssetDrop(ECS::World& world,
                                  const std::filesystem::path& asset) {
    auto entity = world.createEntity();
    world.add<Scene::NameComponent>(entity, { asset.stem().string() });
    world.add<Scene::Transform>(entity, {});    // posição zero
    world.add<Render::SpriteRenderer>(entity, {
        .texture = m_assetManager->load<Render::Texture>(asset.string())
    });
    m_ctx.selectedEntity = entity;
    m_ctx.isDirty = true;
}
```

---

## Gizmos de Transformação (RF6.4)

```
Translate Gizmo (W):        Rotate Gizmo (E):         Scale Gizmo (R):
                                                        
        ▲ Y                         ◯                       ■ Y
        │                        ╱     ╲                    │
        │                       │   ←   │                   │──────■ X
        └────► X               ╲       ╱                    │
      ╱                          ╲   ╱                    ╱
    ╱ Z (3D only)                  ╲╱                   ╱ Z (3D only)

- Arrastar eixo X: move apenas em X
- Arrastar eixo Y: move apenas em Y
- Arrastar plano XY (centro): move livremente em 2D
- Shift+drag: snap de 16px (configurável)
```

---

## Registo de Drawers Customizados

```cpp
// Registar um drawer para um componente de jogo personalizado:
struct HealthComponent {
    f32 current;
    f32 maximum;
};

// No init do jogo:
editor.getInspector().registerDrawer(
    ECS::componentID<HealthComponent>(),
    [](void* data) {
        auto* health = static_cast<HealthComponent*>(data);
        ImGui::SliderFloat("HP", &health->current, 0.0f, health->maximum);
        ImGui::SliderFloat("Max HP", &health->maximum, 1.0f, 9999.0f);
    }
);
```

---

## Critério de Aceitação

- [ ] HierarchyPanel exibe todas as entidades em árvore com nomes
- [ ] InspectorPanel permite editar Transform (posição, rotação, escala)
- [ ] InspectorPanel permite editar SpriteRenderer (texture, cor, flip)
- [ ] SceneViewport renderiza a cena em offscreen e exibe via ImGui::Image
- [ ] Gizmos Translate: arrastar eixo X/Y move a entidade selecionada
- [ ] Gizmos Rotate: girar entidade visualmente
- [ ] Gizmos Scale: escalar entidade visualmente
- [ ] AssetBrowser lista `.caf` em `/assets/` com thumbnails
- [ ] Drag-and-drop de textura do AssetBrowser cria entidade com SpriteRenderer
- [ ] File > Save serializa a cena para `.caf` (round-trip com SceneSerializer)
- [ ] File > Open deserializa a cena e popula o ECS corretamente

---

## Dependências

- **Upstream:**
  - [ECS](../fase4/ecs.md) — `World`, `Entity`, componentes
  - [Scene Manager](../fase4/scene.md) — `SceneSerializer`
  - [Embedded UI](embedded-ui.md) — `ImGuiIntegration`
  - [RHI](../fase3/rhi.md) — framebuffer offscreen
  - [Asset Manager](../fase3/asset-manager.md) — thumbnails + drop
  - [Batch Renderer](../fase3/batch-renderer.md) — renderização no viewport
- **Downstream:**
  - — (fase final)

---

## Referências

- [`docs/fase4/ecs.md`](../fase4/ecs.md) — ECS World e componentes
- [`docs/fase4/scene.md`](../fase4/scene.md) — SceneSerializer
- [`docs/fase6/embedded-ui.md`](embedded-ui.md) — ImGuiIntegration
- [`docs/fase3/rhi.md`](../fase3/rhi.md) — RenderDevice
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [Dear ImGui — Docking](https://github.com/ocornut/imgui/wiki/Docking)
- [Dear ImGui — ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) — biblioteca de gizmos
