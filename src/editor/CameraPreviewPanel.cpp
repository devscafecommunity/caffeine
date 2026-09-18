#include "editor/CameraPreviewPanel.hpp"
#include "editor/SceneViewport.hpp"

#ifdef CF_HAS_IMGUI

#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "editor/EditorContext.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"

#include <stb/stb_image.h>
#include <array>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Caffeine::Editor {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;

bool tryGetEntityPosition(ECS::World& world, ECS::Entity entity, Vec3& outPosition) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) {
        outPosition = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
        return true;
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        outPosition = t->position;
        return true;
    }
    if (auto* p3 = world.get<ECS::Position3D>(entity)) {
        outPosition = p3->position;
        return true;
    }
    return false;
}

Mat4 buildLocalMatrix3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position) : Mat4::identity();
    Mat4 R = r ? Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w).normalized().toMatrix()
               : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) {
        return Mat4::translation(t->position)
             * Mat4::rotationZ(t->rotation.z * kDegToRad)
             * Mat4::rotationY(t->rotation.y * kDegToRad)
             * Mat4::rotationX(t->rotation.x * kDegToRad)
             * Mat4::scale(t->scale.x, t->scale.y, t->scale.z);
    }
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

Vec3 matrixAxis(const Mat4& m, int column, const Vec3& fallback) {
    Vec3 axis(m(0, column), m(1, column), m(2, column));
    const f32 lenSq = axis.lengthSquared();
    return (lenSq > 0.000001f) ? axis / std::sqrt(lenSq) : fallback;
}

Vec3 entityForward(ECS::World& world, ECS::Entity entity) {
    return -1.0f * matrixAxis(entityMatrix(world, entity), 2, Vec3(0.0f, 0.0f, -1.0f));
}

Mat4 buildCameraViewMatrix(ECS::World& world, ECS::Entity entity, Vec3& outPosition) {
    const Mat4 worldMatrix = entityMatrix(world, entity);
    outPosition = worldMatrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));

    const Vec3 right   = matrixAxis(worldMatrix, 0, Vec3(1.0f, 0.0f, 0.0f));
    const Vec3 up      = matrixAxis(worldMatrix, 1, Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 forward = entityForward(world, entity).normalized();

    Mat4 view = Mat4::identity();
    view(0, 0) = right.x;   view(0, 1) = right.y;   view(0, 2) = right.z;
    view(1, 0) = up.x;      view(1, 1) = up.y;      view(1, 2) = up.z;
    view(2, 0) = -forward.x; view(2, 1) = -forward.y; view(2, 2) = -forward.z;
    view(0, 3) = -right.dot(outPosition);
    view(1, 3) = -up.dot(outPosition);
    view(2, 3) =  forward.dot(outPosition);
    return view;
}

ImVec2 projectPoint(const Mat4& vp, ImVec2 origin, ImVec2 panelSize, const Vec3& p) {
    Vec4 clip = vp.transformVec4(Vec4(p.x, p.y, p.z, 1.0f));
    if (clip.w <= 0.1f) return ImVec2(-10000.0f, -10000.0f);
    const f32 ndcX = clip.x / clip.w;
    const f32 ndcY = clip.y / clip.w;
    return ImVec2(
        origin.x + (ndcX + 1.0f) * 0.5f * panelSize.x,
        origin.y + (1.0f - ndcY) * 0.5f * panelSize.y
    );
}

bool projectLineClipped(const Mat4& vp, ImVec2 origin, ImVec2 panelSize,
                        const Vec3& a, const Vec3& b, ImVec2& outA, ImVec2& outB) {
    Vec4 clipA = vp.transformVec4(Vec4(a.x, a.y, a.z, 1.0f));
    Vec4 clipB = vp.transformVec4(Vec4(b.x, b.y, b.z, 1.0f));
    const f32 wMin = 0.1f;

    bool va = clipA.w > wMin;
    bool vb = clipB.w > wMin;
    if (!va && !vb) return false;

    if (va && vb) {
        outA = projectPoint(vp, origin, panelSize, a);
        outB = projectPoint(vp, origin, panelSize, b);
        return true;
    }

    if (!va) {
        const f32 t = (wMin - clipA.w) / (clipB.w - clipA.w);
        clipA = Vec4(
            clipA.x + t * (clipB.x - clipA.x),
            clipA.y + t * (clipB.y - clipA.y),
            clipA.z + t * (clipB.z - clipA.z),
            wMin);
    } else {
        const f32 t = (wMin - clipB.w) / (clipA.w - clipB.w);
        clipB = Vec4(
            clipB.x + t * (clipA.x - clipB.x),
            clipB.y + t * (clipA.y - clipB.y),
            clipB.z + t * (clipA.z - clipB.z),
            wMin);
    }

    const f32 ndcAx = clipA.x / clipA.w, ndcAy = clipA.y / clipA.w;
    const f32 ndcBx = clipB.x / clipB.w, ndcBy = clipB.y / clipB.w;
    outA = ImVec2(
        origin.x + (ndcAx + 1.0f) * 0.5f * panelSize.x,
        origin.y + (1.0f - ndcAy) * 0.5f * panelSize.y);
    outB = ImVec2(
        origin.x + (ndcBx + 1.0f) * 0.5f * panelSize.x,
        origin.y + (1.0f - ndcBy) * 0.5f * panelSize.y);
    return true;
}

void drawLine3D(ImDrawList* dl, const Mat4& vp, ImVec2 origin, ImVec2 panelSize,
                const Vec3& a, const Vec3& b, ImU32 col, float thickness) {
    ImVec2 sa, sb;
    if (!projectLineClipped(vp, origin, panelSize, a, b, sa, sb)) return;
    dl->AddLine(sa, sb, col, thickness);
}

void drawCubeWireframe(ImDrawList* dl, const Mat4& vp, ImVec2 origin, ImVec2 panelSize,
                       const Mat4& worldMatrix, ImU32 col, float thickness) {
    const std::array<Vec3, 8> corners = {{
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    }};
    const int edges[][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };
    Vec3 worldCorners[8];
    for (int i = 0; i < 8; ++i) {
        worldCorners[i] = worldMatrix.transformPoint(corners[i]);
    }
    for (const auto& edge : edges) {
        drawLine3D(dl, vp, origin, panelSize, worldCorners[edge[0]], worldCorners[edge[1]], col, thickness);
    }
}

} // namespace

void CameraPreviewPanel::onImGuiRender(ECS::World& world, EditorContext& ctx, SceneViewport& viewport) {
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Camera Preview", &m_open)) {
        ImGui::End();
        return;
    }

    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    if (panelSize.x < 4.0f) panelSize.x = 4.0f;
    if (panelSize.y < 4.0f) panelSize.y = 4.0f;

    ECS::Entity cameraEntity;
    float camX = 0.0f, camY = 0.0f, zoom = 1.0f;
    ECS::Camera3DComponent* cam3D = nullptr;
    bool found2D = false;
    bool found3D = false;

    if (ctx.selectedEntity.isValid() && world.has<ECS::Camera3DComponent>(ctx.selectedEntity)) {
        cameraEntity = ctx.selectedEntity;
        cam3D = world.get<ECS::Camera3DComponent>(cameraEntity);
        found3D = (cam3D != nullptr);
    }

    if (!found3D) {
        ECS::ComponentQuery q;
        q.with<ECS::Camera3DComponent>();
        world.forEach<ECS::Camera3DComponent>(q,
            [&](ECS::Entity e, ECS::Camera3DComponent& cam) {
                if (!found3D) {
                    cameraEntity = e;
                    cam3D = &cam;
                    found3D = true;
                }
            });
    }

    if (!found3D) {
        ECS::ComponentQuery q;
        q.with<ECS::Camera2DComponent>();
        q.with<ECS::Transform>();
        world.forEach<ECS::Camera2DComponent, ECS::Transform>(q,
            [&](ECS::Entity e, ECS::Camera2DComponent& cam, ECS::Transform& pos) {
                if (!found2D) {
                    cameraEntity = e;
                    camX  = pos.position.x;
                    camY  = pos.position.y;
                    zoom  = cam.zoom;
                    found2D = true;
                }
            });
    }

    if (!found2D && !found3D) {
        ECS::ComponentQuery q;
        q.with<ECS::Camera2DComponent>();
        world.forEach<ECS::Camera2DComponent>(q,
            [&](ECS::Entity e, ECS::Camera2DComponent& cam) {
                if (!found2D) {
                    cameraEntity = e;
                    zoom  = cam.zoom;
                    found2D = true;
                }
            });
    }

    ImVec2 origin = ImGui::GetCursorScreenPos();

    if (!found2D && !found3D) {
        ImGui::InvisibleButton("##campreview_fallback", panelSize);
        renderEditorCameraFallback(world, ctx, viewport, origin, panelSize);
    } else if (found3D && cam3D) {
        ImGui::InvisibleButton("##campreview3d", panelSize);
        renderCamera3DView(world, ctx, viewport, origin, panelSize, cameraEntity, *cam3D);
    } else {
        ImGui::InvisibleButton("##campreview", panelSize);
        renderCameraView(world, ctx, origin, panelSize, camX, camY, zoom);
    }

    ImGui::End();
}

void CameraPreviewPanel::renderNoCamera(ImVec2 panelSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin  = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                      IM_COL32(20, 20, 24, 255));

    const char* msg = "No Cameras detected";
    ImVec2 textSize = ImGui::CalcTextSize(msg);
    ImVec2 textPos  = ImVec2(origin.x + (panelSize.x - textSize.x) * 0.5f,
                              origin.y + (panelSize.y - textSize.y) * 0.5f);
    dl->AddText(textPos, IM_COL32(120, 120, 130, 200), msg);

    ImGui::InvisibleButton("##camempty", panelSize);
}

void CameraPreviewPanel::renderCameraView(ECS::World& world, EditorContext& ctx,
                                          ImVec2 origin, ImVec2 panelSize,
                                          float camX, float camY, float zoom) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                      IM_COL32(15, 15, 18, 255));

    dl->PushClipRect(origin,
                     ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                     true);

    const float worldToScreen = zoom * 50.0f;
    const float cx = origin.x + panelSize.x * 0.5f;
    const float cy = origin.y + panelSize.y * 0.5f;

    auto w2s = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(cx + (wx - camX) * worldToScreen,
                      cy - (wy - camY) * worldToScreen);
    };

    ECS::ComponentQuery spriteQ;
    spriteQ.with<ECS::Transform>();
    spriteQ.with<ECS::Sprite>();

    world.forEach<ECS::Transform, ECS::Sprite>(spriteQ,
        [&](ECS::Entity, ECS::Transform& pos, ECS::Sprite& sprite) {
            float scaleX = std::max(0.1f, pos.scale.x), scaleY = std::max(0.1f, pos.scale.y);
            ImVec2 screenPos = w2s(pos.position.x, pos.position.y);

            float halfW = std::max(8.0f, 0.5f * worldToScreen * scaleX);
            float halfH = std::max(8.0f, 0.5f * worldToScreen * scaleY);

            bool hasTexture = false;
            ImTextureRef texRef;

            if (!sprite.name.empty()) {
                const std::string path = resolveSpritePath(sprite.name, ctx);
                if (!path.empty()) {
                    auto it = m_texCache.find(path);
                    if (it == m_texCache.end()) {
                        int w = 0, h = 0, ch = 0;
                        unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
                        TexEntry entry;
                        if (px && w > 0 && h > 0) {
                            entry.width  = w;
                            entry.height = h;
                            entry.texture = std::make_unique<ImTextureData>();
                            entry.texture->Create(ImTextureFormat_RGBA32, w, h);
                            std::memcpy(entry.texture->GetPixels(), px,
                                        static_cast<size_t>(w * h * 4));
                            entry.texture->SetStatus(ImTextureStatus_WantCreate);
                            ImGui_ImplSDLGPU3_UpdateTexture(entry.texture.get());
                        } else {
                            entry.loadFailed = true;
                        }
                        if (px) stbi_image_free(px);
                        auto [newIt, ok] = m_texCache.emplace(path, std::move(entry));
                        it = newIt;
                    }
                    if (!it->second.loadFailed && it->second.texture) {
                        if (it->second.texture->Status == ImTextureStatus_WantCreate)
                            ImGui_ImplSDLGPU3_UpdateTexture(it->second.texture.get());
                        ImTextureID tid = it->second.texture->GetTexID();
                        hasTexture = (tid != ImTextureID_Invalid);
                        if (hasTexture) {
                            texRef = it->second.texture->GetTexRef();
                            const float aspect = static_cast<float>(it->second.width) /
                                                 static_cast<float>(it->second.height);
                            if (aspect > 1.0f) halfH = std::max(8.0f, halfW / aspect);
                            else               halfW = std::max(8.0f, halfH * aspect);
                        }
                    }
                }
            }

            const ImVec2 p1 = ImVec2(screenPos.x - halfW, screenPos.y - halfH);
            const ImVec2 p2 = ImVec2(screenPos.x + halfW, screenPos.y - halfH);
            const ImVec2 p3 = ImVec2(screenPos.x + halfW, screenPos.y + halfH);
            const ImVec2 p4 = ImVec2(screenPos.x - halfW, screenPos.y + halfH);

            if (hasTexture) {
                dl->AddImageQuad(texRef, p1, p2, p3, p4);
            } else {
                dl->AddRectFilled(p1, p3, IM_COL32(64, 64, 64, 200));
            }
        });

    dl->PopClipRect();

    const float borderAlpha = 160;
    dl->AddRect(origin,
                ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                IM_COL32(80, 140, 255, borderAlpha), 0.0f, 0, 1.5f);
}

void CameraPreviewPanel::renderEditorCameraFallback(ECS::World& world, EditorContext& ctx,
                                                    SceneViewport& viewport,
                                                    ImVec2 origin, ImVec2 panelSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->PushClipRect(origin,
                     ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                     true);

    const f32 sinY = std::sin(ctx.camYaw);
    const f32 cosY = std::cos(ctx.camYaw);
    const f32 sinP = std::sin(ctx.camPitch);
    const f32 cosP = std::cos(ctx.camPitch);
    const Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
    const Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
    const f32 aspect = panelSize.x / std::max(panelSize.y, 1.0f);
    const Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, 10000.0f);
    const Mat4 vp = proj * view;

    dl->AddRectFilledMultiColor(
        origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
        IM_COL32(20, 20, 24, 255), IM_COL32(20, 20, 24, 255),
        IM_COL32(34, 38, 50, 255), IM_COL32(34, 38, 50, 255));

    const ImU32 gridColor = IM_COL32(90, 90, 110, 120);
    const float gridExtent = 20.0f;
    for (int i = -20; i <= 20; ++i) {
        drawLine3D(dl, vp, origin, panelSize,
                   Vec3(static_cast<f32>(i), 0.0f, -gridExtent),
                   Vec3(static_cast<f32>(i), 0.0f, gridExtent),
                   gridColor, 0.5f);
        drawLine3D(dl, vp, origin, panelSize,
                   Vec3(-gridExtent, 0.0f, static_cast<f32>(i)),
                   Vec3(gridExtent, 0.0f, static_cast<f32>(i)),
                   gridColor, 0.5f);
    }

    viewport.drawSceneMeshesForCamera(world, ctx, dl, vp, camPos, origin, panelSize);

    dl->PopClipRect();

    dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f),
                IM_COL32(200, 200, 210, 230),
                "Editor Camera (sem Camera3D na cena)");
    dl->AddRect(origin,
                ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                IM_COL32(120, 120, 130, 160), 0.0f, 0, 1.5f);
}

void CameraPreviewPanel::renderCamera3DView(ECS::World& world, EditorContext& ctx,
                                            SceneViewport& viewport,
                                            ImVec2 origin, ImVec2 panelSize,
                                            ECS::Entity cameraEntity,
                                            ECS::Camera3DComponent& cam) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->PushClipRect(origin,
                     ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                     true);

    Vec3 camPos;
    const Mat4 view = buildCameraViewMatrix(world, cameraEntity, camPos);

    const f32 aspect = panelSize.x / std::max(panelSize.y, 1.0f);
    const f32 fovRad = cam.fov * kDegToRad;
    const Mat4 proj = Mat4::perspective(fovRad, aspect, cam.nearClip, cam.farClip);
    const Mat4 vp = proj * view;

    dl->AddRectFilledMultiColor(
        origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
        IM_COL32(20, 20, 24, 255), IM_COL32(20, 20, 24, 255),
        IM_COL32(34, 38, 50, 255), IM_COL32(34, 38, 50, 255));

    const ImU32 gridColor = IM_COL32(90, 90, 110, 120);
    const ImU32 majorGridColor = IM_COL32(120, 120, 145, 160);
    const float gridSpacing = 1.0f;
    const float majorSpacing = gridSpacing * 10.0f;
    const float gridExtent = 30.0f;

    auto isMajor = [&](float coord) {
        const float m = std::fmod(std::abs(coord), majorSpacing);
        return m < gridSpacing * 0.05f;
    };

    const float camX = camPos.x;
    const float camZ = camPos.z;
    const float startX = std::floor((camX - gridExtent) / gridSpacing) * gridSpacing;
    const float endX   = std::ceil ((camX + gridExtent) / gridSpacing) * gridSpacing;
    const float startZ = std::floor((camZ - gridExtent) / gridSpacing) * gridSpacing;
    const float endZ   = std::ceil ((camZ + gridExtent) / gridSpacing) * gridSpacing;

    for (float x = startX; x <= endX; x += gridSpacing) {
        if (std::abs(x) < gridSpacing * 0.01f) continue;
        const bool major = isMajor(x);
        drawLine3D(dl, vp, origin, panelSize,
                   Vec3(x, 0.0f, camZ - gridExtent), Vec3(x, 0.0f, camZ + gridExtent),
                   major ? majorGridColor : gridColor, major ? 1.0f : 0.5f);
    }
    for (float z = startZ; z <= endZ; z += gridSpacing) {
        if (std::abs(z) < gridSpacing * 0.01f) continue;
        const bool major = isMajor(z);
        drawLine3D(dl, vp, origin, panelSize,
                   Vec3(camX - gridExtent, 0.0f, z), Vec3(camX + gridExtent, 0.0f, z),
                   major ? majorGridColor : gridColor, major ? 1.0f : 0.5f);
    }
    drawLine3D(dl, vp, origin, panelSize,
               Vec3(camX - gridExtent, 0.0f, 0.0f), Vec3(camX + gridExtent, 0.0f, 0.0f),
               IM_COL32(220, 60, 60, 200), 1.5f);
    drawLine3D(dl, vp, origin, panelSize,
               Vec3(0.0f, 0.0f, camZ - gridExtent), Vec3(0.0f, 0.0f, camZ + gridExtent),
               IM_COL32(60, 60, 220, 200), 1.5f);

    viewport.drawSceneMeshesForCamera(world, ctx, dl, vp, camPos, origin, panelSize, cameraEntity);

    ECS::ComponentQuery posQ;
    posQ.with<ECS::Position3D>();
    posQ.without<ECS::MeshFilterComponent>();
    world.forEach<ECS::Position3D>(posQ,
        [&](ECS::Entity entity, ECS::Position3D&) {
            if (entity == cameraEntity) return;
            if (Scene::isEffectivelyDisabled(world, entity)) return;
            Vec3 p;
            if (!tryGetEntityPosition(world, entity, p)) return;
            ImVec2 sp = projectPoint(vp, origin, panelSize, p);
            if (sp.x < -5000.0f) return;
            dl->AddCircleFilled(sp, 4.0f, IM_COL32(180, 180, 200, 200));
        });

    dl->PopClipRect();

    const char* name = getEntityName(world, cameraEntity);
    if (name && name[0] != '\0') {
        dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f),
                    IM_COL32(200, 200, 210, 230), name);
    }

    dl->AddRect(origin,
                ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                IM_COL32(80, 140, 255, 160), 0.0f, 0, 1.5f);
}

std::string CameraPreviewPanel::resolveSpritePath(const std::string& name,
                                                   const EditorContext& ctx) const {
    if (name.empty()) return {};
    std::filesystem::path p(name);
    if (std::filesystem::exists(p)) return name;

    std::filesystem::path root;
    if (!ctx.currentScenePath.empty())
        root = std::filesystem::path(ctx.currentScenePath).parent_path();
    else
        root = std::filesystem::current_path();

    auto candidate = root / name;
    if (std::filesystem::exists(candidate)) return candidate.string();

    for (auto& sub : {"assets", "sprites", "textures"}) {
        auto sp = root / sub / name;
        if (std::filesystem::exists(sp)) return sp.string();
    }
    return {};
}

}

#endif
