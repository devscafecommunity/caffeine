#include "editor/GameplayPreviewPanel.hpp"
#include "editor/EditorPanelUtils.hpp"
#include "ui/UIRenderer.hpp"
#include "render/PostProcessRenderer.hpp"

#ifdef CF_HAS_IMGUI

#include "ecs/CameraComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "editor/EditorContext.hpp"
#include "math/Mat4.hpp"
#include "scene/EnvironmentSystem.hpp"
#include "scene/HierarchySystem.hpp"
#include "render/SkyboxRenderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Caffeine::Editor {
namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;

bool findGameplayCamera(ECS::World& world, ECS::Entity& outEntity, ECS::Camera3DComponent*& outCam) {
    outCam = nullptr;
    ECS::ComponentQuery activeQ;
    activeQ.with<ECS::Camera3DComponent>();
    activeQ.with<ECS::CameraActiveComponent>();
    world.forEach<ECS::Camera3DComponent, ECS::CameraActiveComponent>(
        activeQ, [&](ECS::Entity e, ECS::Camera3DComponent& cam, ECS::CameraActiveComponent&) {
            if (!outCam) {
                outEntity = e;
                outCam = &cam;
            }
        });
    if (outCam) return true;

    ECS::ComponentQuery q;
    q.with<ECS::Camera3DComponent>();
    world.forEach<ECS::Camera3DComponent>(q, [&](ECS::Entity e, ECS::Camera3DComponent& cam) {
        if (!outCam) {
            outEntity = e;
            outCam = &cam;
        }
    });
    return outCam != nullptr;
}

std::string projectRootFromContext(const EditorContext& ctx) {
    if (ctx.currentScenePath.empty()) return {};
    const auto sceneDir = std::filesystem::path(ctx.currentScenePath).parent_path();
    const std::string root = sceneDir.parent_path().string();
    return root.empty() ? sceneDir.string() : root;
}

void drawSceneSkybox(ImDrawList* dl, ImVec2 origin, ImVec2 size, ECS::World& world,
                     const EditorContext& ctx, const Render::SkyboxCamera& camera,
                     Render::SkyboxRenderer& renderer) {
    const std::string projectRoot = projectRootFromContext(ctx);
    std::filesystem::path texturePath;
    const Scene::ActiveSkybox active = Scene::findActiveSkybox(world);
    if (active.component) {
        texturePath = Scene::resolveSkyboxTexturePath(*active.component, projectRoot);
    } else {
        texturePath = Scene::resolveBuiltinSkyboxPath(ctx.skyboxIndex);
    }
    if (texturePath.empty()) return;
    renderer.draw(dl, origin, size, camera, texturePath.string());
}

}  // namespace

#ifdef CF_HAS_SDL3
bool GameplayPreviewPanel::init(RHI::RenderDevice* device) {
    shutdown();
    if (!device) return false;
    m_device = device;
    m_ready = m_renderer.init(device);
    return m_ready;
}

void GameplayPreviewPanel::shutdown() {
    if (m_device) {
        if (m_colorTarget) m_device->destroyTexture(m_colorTarget);
        if (m_depthTarget) m_device->destroyTexture(m_depthTarget);
        m_renderer.shutdown();
    }
#ifdef CF_HAS_IMGUI
    m_skyboxRenderer.releaseGpuTextures();
#endif
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
    m_device = nullptr;
    m_frameCmd = nullptr;
    m_width = 0;
    m_height = 0;
    m_ready = false;
}

void GameplayPreviewPanel::resizeCanvas(u32 width, u32 height) {
    if (!m_device || width < 1 || height < 1) return;
    if (m_width == width && m_height == height && m_colorTarget && m_depthTarget) return;

    if (m_colorTarget) m_device->destroyTexture(m_colorTarget);
    if (m_depthTarget) m_device->destroyTexture(m_depthTarget);

    RHI::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_colorTarget = m_device->createTexture(colorDesc);

    RHI::TextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage = RHI::TextureUsage::DepthStencil;
    m_depthTarget = m_device->createTexture(depthDesc);

    m_width = width;
    m_height = height;
}
#endif

void GameplayPreviewPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    editorPanelApplyDetach(m_detached, ImVec2(960, 540));
    ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Gameplay Preview", &m_open, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    editorPanelDetachTabButton(m_detached);

    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    if (panelSize.x < 8.0f) panelSize.x = 8.0f;
    if (panelSize.y < 8.0f) panelSize.y = 8.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##gameplay_preview", panelSize);

    ECS::Entity cameraEntity;
    ECS::Camera3DComponent* cam = nullptr;
    const bool found = findGameplayCamera(world, cameraEntity, cam);

#ifdef CF_HAS_SDL3
    if (found && cam && m_ready && m_frameCmd && m_renderer.isReady()) {
        u32 w = static_cast<u32>(panelSize.x);
        u32 h = static_cast<u32>(panelSize.y);
        w = std::clamp(w, 8u, 1280u);
        h = std::clamp(h, 8u, 720u);
        resizeCanvas(w, h);

        const Mat4 worldMatrix = Scene::computeWorldMatrix(world, cameraEntity);
        Vec3 position = worldMatrix.transformPoint(Vec3(0, 0, 0));
        Vec3 right(worldMatrix(0, 0), worldMatrix(1, 0), worldMatrix(2, 0));
        Vec3 up(worldMatrix(0, 1), worldMatrix(1, 1), worldMatrix(2, 1));
        Vec3 forward(-worldMatrix(0, 2), -worldMatrix(1, 2), -worldMatrix(2, 2));
        if (right.lengthSquared() > 1e-8f) right = right.normalized();
        if (up.lengthSquared() > 1e-8f) up = up.normalized();
        if (forward.lengthSquared() > 1e-8f) forward = forward.normalized();

        Render::GpuSceneCamera camera;
        camera.position = position;
        camera.focus = position + forward;
        Mat4 view = Mat4::identity();
        view(0, 0) = right.x;
        view(0, 1) = right.y;
        view(0, 2) = right.z;
        view(1, 0) = up.x;
        view(1, 1) = up.y;
        view(1, 2) = up.z;
        view(2, 0) = -forward.x;
        view(2, 1) = -forward.y;
        view(2, 2) = -forward.z;
        view(0, 3) = -right.dot(position);
        view(1, 3) = -up.dot(position);
        view(2, 3) = forward.dot(position);
        camera.view = view;
        camera.fovRad = cam->fov * kDegToRad;
        camera.nearClip = std::max(cam->nearClip, 0.05f);
        camera.farClip = std::max(cam->farClip, 50.0f);
        const f32 aspect = static_cast<f32>(w) / static_cast<f32>(std::max(h, 1u));
        camera.proj = Mat4::perspective(camera.fovRad, aspect, camera.nearClip, camera.farClip);

        m_renderer.renderWithCamera(m_frameCmd, world, camera, m_colorTarget, m_depthTarget, w, h,
                                    projectRootFromContext(ctx));

        Render::SkyboxCamera skyCamera;
        skyCamera.forward = forward;
        skyCamera.right = right;
        skyCamera.up = up;
        skyCamera.fovY = camera.fovRad;
        skyCamera.aspect = aspect;
        drawSceneSkybox(dl, origin, panelSize, world, ctx, skyCamera, m_skyboxRenderer);

        if (m_colorTarget && m_colorTarget->handle) {
            dl->AddImage(reinterpret_cast<ImTextureID>(m_colorTarget->handle), origin,
                         ImVec2(origin.x + panelSize.x, origin.y + panelSize.y), ImVec2(0, 0),
                         ImVec2(1, 1));
        }
        const char* badge = ctx.isPlayMode ? "PLAY" : "STOPPED";
        const ImU32 badgeCol = ctx.isPlayMode ? IM_COL32(120, 230, 140, 230) : IM_COL32(220, 180, 90, 220);
        dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), badgeCol, badge);
        if (const char* name = getEntityName(world, cameraEntity); name && name[0] != '\0') {
            dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 22.0f), IM_COL32(200, 200, 210, 200), name);
        }
    } else
#endif
    {
        dl->AddRectFilled(origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                          IM_COL32(18, 18, 22, 255));
        const char* msg = found ? "Gameplay GPU not ready" : "No Camera3D in the scene";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (panelSize.x - ts.x) * 0.5f,
                           origin.y + (panelSize.y - ts.y) * 0.5f),
                    IM_COL32(160, 160, 170, 220), msg);
    }

    dl->AddRect(origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                ctx.isPlayMode ? IM_COL32(80, 200, 120, 180) : IM_COL32(80, 140, 255, 140), 0.0f, 0,
                1.5f);

    if (ctx.isPlayMode) {
        UI::drawWidgets(world, dl, origin, panelSize);
    }
    if (const ECS::PostProcessComponent* fx = Render::findPostProcessForCamera(world, cameraEntity)) {
        Render::applyPostProcessOverlay(dl, origin, panelSize, *fx);
    }

    ImGui::End();
}

}  // namespace Caffeine::Editor

#endif
