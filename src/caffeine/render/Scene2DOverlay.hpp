#pragma once

#ifdef CF_HAS_SDL3

#include "BatchRenderer.hpp"
#include "GpuTextureCache.hpp"
#include "../animation/Rig2DSystem.hpp"
#include "../animation/SkinLibrary.hpp"
#include "../ecs/ComponentQuery.hpp"
#include "../ecs/Components.hpp"
#include "../ecs/Components3D.hpp"
#include "../ecs/World.hpp"
#include "../math/Mat4.hpp"
#include "../math/Quat.hpp"
#include "../rhi/CommandBuffer.hpp"
#include "../rhi/RenderDevice.hpp"
#include "../scene/HierarchySystem.hpp"
#include "../scene/SceneComponents.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace Caffeine::Render {

/// Local placement of an entity, the same one the hierarchy uses: Position 3D (with Rotation 3D and Scale 3D) or the
/// editor's Transform. Without either, the 2D components place it on the XY plane: Position 2D, Rotation in radians
/// about Z, and Scale 2D. A missing part is the identity.
inline Mat4 entityPlacementMatrix(ECS::World& world, ECS::Entity entity) {
    if (world.has<ECS::Position3D>(entity) || world.has<ECS::Transform>(entity)) {
        return Scene::localMatrix(world, entity);
    }
    Vec2 position{};
    f32 angle = 0.0f;
    Vec2 scale(1.0f, 1.0f);
    if (const auto* p = world.get<ECS::Position2D>(entity)) position = Vec2(p->x, p->y);
    if (const auto* r = world.get<ECS::Rotation>(entity)) angle = r->angle;
    if (const auto* s = world.get<ECS::Scale2D>(entity)) scale = Vec2(s->x, s->y);
    const Mat4 rotation = Quat::fromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), angle).toMatrix();
    return Mat4::translation(position.x, position.y, 0.0f) * rotation * Mat4::scale(scale.x, scale.y, 1.0f);
}

/// World matrix of an entity: its placement composed with its parents' (Scene::Parent), as the hierarchy does.
inline Mat4 entityWorldPlacement(ECS::World& world, ECS::Entity entity, int depth = 0) {
    const Mat4 local = entityPlacementMatrix(world, entity);
    const auto* parent = world.get<Scene::Parent>(entity);
    if (depth < 8 && parent && parent->parent.isValid() && parent->parent.id() != entity.id()) {
        return entityWorldPlacement(world, parent->parent, depth + 1) * local;
    }
    return local;
}

/// Draws the 2D content of a world over a colour target: every Sprite as a textured quad, every Rig 2D as its
/// skinned mesh. Sprites and rigs are placed by the entity's 3D placement; a sprite is one world unit on its longer
/// side, scaled by the placement. Textures are acquired once and held until release(), so frames do not reload them.
class Scene2DOverlay {
public:
    Scene2DOverlay() = default;
    ~Scene2DOverlay() { release(); }

    Scene2DOverlay(const Scene2DOverlay&) = delete;
    Scene2DOverlay& operator=(const Scene2DOverlay&) = delete;

    /// Builds the GPU batch for targets of `colorFormat`. False when the device cannot; draw() then does nothing.
    bool attach(RHI::RenderDevice* device, RHI::TextureFormat colorFormat) {
        release();
        if (!device) return false;
        m_device = device;
        return m_batch.attachGpu(device, colorFormat);
    }

    /// Drops the textures it holds and the GPU batch. Call before the device goes away.
    void release() {
        if (m_device) {
            for (auto& entry : m_textures) {
                if (entry.second) GpuTextureCache::instance().release(m_device, entry.second);
            }
        }
        m_textures.clear();
        m_batch.detachGpu();
        m_device = nullptr;
    }

    /// Draws the world's sprites and rigs into `target`, over what it holds. `viewProjection` is the camera's
    /// proj * view; `projectRoot` resolves relative sprite paths. Returns the number of primitives drawn (0 when
    /// there is no GPU or nothing to draw).
    u32 draw(RHI::CommandBuffer* cmd, RHI::Texture* target, ECS::World& world, const Mat4& viewProjection,
             const std::string& projectRoot) {
        if (!cmd || !target || !m_batch.gpuReady()) return 0;
        m_batch.beginFrame();
        m_batch.setViewProjection(viewProjection);

        u32 primitives = 0;
        ECS::ComponentQuery spriteQuery;
        spriteQuery.with<ECS::Sprite>();
        world.forEach<ECS::Sprite>(spriteQuery, [&](ECS::Entity entity, ECS::Sprite& sprite) {
            RHI::Texture* texture = sprite.name.empty() ? nullptr : textureFor(sprite.name, projectRoot);
            Vec4 uv(0.0f, 0.0f, 1.0f, 1.0f);
            f32 frameW = 1.0f;
            f32 frameH = 1.0f;
            if (texture) {
                const f32 texW = static_cast<f32>(texture->width);
                const f32 texH = static_cast<f32>(texture->height);
                frameW = texW;
                frameH = texH;
                if (const Animation::SpriteSheet* sheet = world.get<Animation::SpriteSheet>(entity)) {
                    const Animation::SpriteSheetRect rect = Animation::spriteSheetFrame(*sheet, sprite.frameIndex, texW, texH);
                    uv = Vec4(rect.x / texW, rect.y / texH, (rect.x + rect.w) / texW, (rect.y + rect.h) / texH);
                    frameW = rect.w;
                    frameH = rect.h;
                }
            }
            const f32 longest = std::max(frameW, frameH);
            const Mat4 aspect = Mat4::scale(frameW / longest, frameH / longest, 1.0f);
            m_batch.submitTexturedQuad(entityWorldPlacement(world, entity) * aspect, texture, uv);
            ++primitives;
        });

        ECS::ComponentQuery rigQuery;
        rigQuery.with<Animation::Rig2DPlayer>();
        world.forEach<Animation::Rig2DPlayer>(rigQuery, [&](ECS::Entity entity, Animation::Rig2DPlayer& player) {
            if (player.skinned.empty()) return;
            submitRig2DMesh(m_batch, player, entityWorldPlacement(world, entity), Vec4(0.0f, 0.0f, 1.0f, 1.0f));
            ++primitives;
        });
        if (primitives == 0) return 0;

        // Keep the colour already there (the scene is drawn first); the overlay needs no depth.
        RHI::RenderPassDesc pass;
        pass.colorTarget = target;
        pass.loadColor = true;
        cmd->beginRenderPass(pass);
        m_batch.endFrame(cmd);
        cmd->endRenderPass();
        return primitives;
    }

private:
    /// The texture of an image path, acquired on first use. A file that does not load is remembered as null, so it
    /// is not retried every frame.
    RHI::Texture* textureFor(const std::string& path, const std::string& projectRoot) {
        const auto found = m_textures.find(path);
        if (found != m_textures.end()) return found->second;
        RHI::Texture* texture = GpuTextureCache::instance().acquire(m_device, path, projectRoot, 0, true);
        m_textures.emplace(path, texture);
        return texture;
    }

    BatchRenderer m_batch{nullptr};
    RHI::RenderDevice* m_device = nullptr;
    std::unordered_map<std::string, RHI::Texture*> m_textures;
};

}  // namespace Caffeine::Render

#endif  // CF_HAS_SDL3
