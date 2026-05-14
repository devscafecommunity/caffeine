#include "editor/SceneEditor.hpp"

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

// ── Init / Shutdown ─────────────────────────────────────────────

#ifdef CF_HAS_SDL3
bool SceneEditor::init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
                        const char* assetsPath) {
    if (!m_viewport.init(device)) return false;
    m_assetBrowser.init(assetsPath);
    m_assetManager = assetManager;
    return true;
}

void SceneEditor::shutdown() {
    m_viewport.shutdown();
}
#endif

// ── Main render ─────────────────────────────────────────────────

void SceneEditor::render(ECS::World& world
#ifdef CF_HAS_SDL3
                          , Render::Camera2D& editorCamera
#endif
                         ) {
    if (!m_open) return;

    handleShortcuts(world);

    // Setup dockspace root window
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar
                                 | ImGuiWindowFlags_NoDocking
                                 | ImGuiWindowFlags_NoTitleBar
                                 | ImGuiWindowFlags_NoCollapse
                                 | ImGuiWindowFlags_NoResize
                                 | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoBringToFrontOnFocus
                                 | ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (!m_dockingSetup) {
        setupDockspace(dockspaceId);
        m_dockingSetup = true;
    }

    renderMainMenuBar(world);

    // Render panels
    m_hierarchy.render(world, m_ctx);
    m_inspector.render(world, m_ctx);
#ifdef CF_HAS_SDL3
    m_viewport.render(world, m_ctx, editorCamera);
#else
    m_viewport.render(world, m_ctx);
#endif
    m_assetBrowser.render(m_ctx);
    m_console.render();
    m_profiler.render(Debug::Profiler::instance());

    handleAssetDrop(world);

    ImGui::End(); // DockSpace

    renderStatusBar(world);
}

// ── Dockspace setup ─────────────────────────────────────────────

void SceneEditor::setupDockspace(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID dockLeft, dockRight, dockBottom, dockCenter;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.20f, &dockLeft, &dockCenter);
    ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);
    ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottom, &dockCenter);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Scene Viewport", dockCenter);
    ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);
    ImGui::DockBuilderDockWindow("Console", dockBottom);
    ImGui::DockBuilderDockWindow("Profiler", dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ── Menu bar ────────────────────────────────────────────────────

void SceneEditor::renderMainMenuBar(ECS::World& world) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                world.destroyAll();
                m_ctx.selectedEntity = ECS::Entity::INVALID;
                m_ctx.isDirty = false;
                m_ctx.undoStack.clear();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                saveScene("scene.caf", world);
            }
            if (ImGui::MenuItem("Save As...")) {
                saveScene("scene.caf", world);
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                loadScene("scene.caf", world);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                m_open = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_ctx.undoStack.canUndo())) {
                m_ctx.undoStack.undo(world);
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_ctx.undoStack.canRedo())) {
                m_ctx.undoStack.redo(world);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &m_ctx.hierarchyOpen);
            ImGui::MenuItem("Inspector", nullptr, &m_ctx.inspectorOpen);
            ImGui::MenuItem("Viewport",  nullptr, &m_ctx.viewportOpen);
            ImGui::MenuItem("Assets",    nullptr, &m_ctx.assetsOpen);
            ImGui::EndMenu();
        }

        char dirtyMarker = m_ctx.isDirty ? '*' : ' ';
        char buf[64];
        snprintf(buf, sizeof(buf), "Caffeine Studio — Scene%c", dirtyMarker);
        ImGui::Text("    %s", buf);

        ImGui::EndMainMenuBar();
    }
}

// ── Status bar ──────────────────────────────────────────────────

void SceneEditor::renderStatusBar(ECS::World& world) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));

    if (ImGui::BeginMainMenuBar()) {
        // Scene path
        if (!m_ctx.currentScenePath.empty()) {
            ImGui::Text("Scene: %s", m_ctx.currentScenePath.c_str());
        } else {
            ImGui::Text("Scene: untitled");
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Dirty flag
        if (m_ctx.isDirty) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Modified");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1), "Saved");
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Undo stack depth
        ImGui::Text("Undo: %u", m_ctx.undoStack.count());

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar();
}

// ── Shortcuts ───────────────────────────────────────────────────

void SceneEditor::handleShortcuts(ECS::World& world) {
    bool ctrl = ImGui::GetIO().KeyCtrl;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        saveScene("scene.caf", world);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (ImGui::GetIO().KeyShift) {
            if (m_ctx.undoStack.canRedo()) m_ctx.undoStack.redo(world);
        } else {
            if (m_ctx.undoStack.canUndo()) m_ctx.undoStack.undo(world);
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        if (m_ctx.undoStack.canRedo()) m_ctx.undoStack.redo(world);
    }
}

// ── Serialization ───────────────────────────────────────────────

bool SceneEditor::saveScene(const char* path, ECS::World& world) {
    Scene::SceneSerializer serializer(world);
    if (!serializer.serialize(path)) return false;
    m_ctx.isDirty = false;
    return true;
}

bool SceneEditor::loadScene(const char* path, ECS::World& world) {
    Scene::SceneSerializer serializer(world);
    if (!serializer.deserialize(path)) return false;
    m_ctx.selectedEntity = ECS::Entity::INVALID;
    m_ctx.isDirty = false;
    return true;
}

// ── Asset drop ──────────────────────────────────────────────────

void SceneEditor::handleAssetDrop(ECS::World& world) {
    auto dropped = m_assetBrowser.getDroppedAsset();
    if (!dropped) return;

    std::filesystem::path assetPath = *dropped;
    std::string ext = assetPath.extension().string();

    m_ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);

    ECS::Entity entity = world.create();
    setEntityName(world, entity, assetPath.stem().string().c_str());
    world.add<ECS::Position2D>(entity, 0.0f, 0.0f);

    if (ext == ".caf" || ext == ".png" || ext == ".jpg") {
        world.add<ECS::Sprite>(entity, assetPath.filename().string(), 0);
    }

    m_ctx.selectedEntity = entity;
    m_ctx.endUndo(world);
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
