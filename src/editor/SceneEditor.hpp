#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "scene/SceneSerializer.hpp"
#include "render/Camera2D.hpp"
#include "editor/EditorContext.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/SceneViewport.hpp"
#include "editor/AssetBrowser.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "assets/AssetManager.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstdio>

namespace Caffeine::Editor {
using namespace Caffeine;

class SceneEditor {
public:
    SceneEditor() = default;

#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
              const char* assetsPath = "assets") {
        if (!m_viewport.init(device)) return false;
        m_assetBrowser.init(assetsPath);
        m_assetManager = assetManager;
        return true;
    }

    void shutdown() {
        m_viewport.shutdown();
    }
#endif

    void render(ECS::World& world
#ifdef CF_HAS_SDL3
                , Render::Camera2D& editorCamera
#endif
               ) {
#ifdef CF_HAS_IMGUI
        if (!m_open) return;

        // Setup docking
        if (!m_dockingSetup) {
            setupDockingLayout();
        }

        // Menu bar
        renderMenuBar(world);

        // Render panels
        m_hierarchy.render(world, m_ctx);
        m_inspector.render(world, m_ctx);
#ifdef CF_HAS_SDL3
        m_viewport.render(world, m_ctx, editorCamera);
#else
        m_viewport.render(world, m_ctx);
#endif
        m_assetBrowser.render(m_ctx);

        // Handle asset drop in viewport
        handleAssetDrop(world);
#endif
    }

    // ── Serialization ──

    bool saveScene(const char* path, ECS::World& world) {
        Scene::SceneSerializer serializer(world);
        if (!serializer.serialize(path)) return false;
        m_ctx.isDirty = false;
        return true;
    }

    bool loadScene(const char* path, ECS::World& world) {
        Scene::SceneSerializer serializer(world);
        if (!serializer.deserialize(path)) return false;
        m_ctx.selectedEntity = ECS::Entity::INVALID;
        m_ctx.isDirty = false;
        return true;
    }

    // ── Accessors ──

    EditorContext& context() { return m_ctx; }
    HierarchyPanel& hierarchy() { return m_hierarchy; }
    InspectorPanel& inspector() { return m_inspector; }
    SceneViewport&  viewport()  { return m_viewport; }
    AssetBrowser&   assetBrowser() { return m_assetBrowser; }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void renderMenuBar(ECS::World& world) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) {
                    world.destroyAll();
                    m_ctx.selectedEntity = ECS::Entity::INVALID;
                    m_ctx.isDirty = false;
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
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
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

    void setupDockingLayout() {
        m_dockingSetup = true;
    }

    void handleAssetDrop(ECS::World& world) {
        auto dropped = m_assetBrowser.getDroppedAsset();
        if (!dropped) return;

        std::filesystem::path assetPath = *dropped;
        std::string ext = assetPath.extension().string();

        ECS::Entity entity = world.create();
        setEntityName(world, entity, assetPath.stem().string().c_str());
        world.add<ECS::Position2D>(entity, 0.0f, 0.0f);

        if (ext == ".caf" || ext == ".png" || ext == ".jpg") {
            world.add<ECS::Sprite>(entity, assetPath.filename().string(), 0);
        }

        m_ctx.selectedEntity = entity;
        m_ctx.isDirty = true;
    }
#endif

    EditorContext  m_ctx;
    HierarchyPanel m_hierarchy;
    InspectorPanel m_inspector;
    SceneViewport  m_viewport;
    AssetBrowser   m_assetBrowser;

#ifdef CF_HAS_SDL3
    Assets::AssetManager* m_assetManager = nullptr;
#endif

    bool m_open         = true;
    bool m_dockingSetup = false;
};

} // namespace Caffeine::Editor
