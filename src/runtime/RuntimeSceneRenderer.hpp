#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <memory>
#include <unordered_map>
#include <vector>
#endif

namespace Caffeine::Runtime {

#ifdef CF_HAS_IMGUI

class RuntimeSceneRenderer {
public:
    void render(ECS::World& world, Editor::EditorContext& ctx, ImDrawList* dl, const Mat4& vp,
                const Vec3& camPos, ImVec2 origin, ImVec2 panelSize,
                ECS::Entity skipEntity = ECS::Entity::INVALID);

private:
    struct RasterTexture {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
    };

    RasterTexture m_frameTexture;
};

#endif

}  // namespace Caffeine::Runtime
