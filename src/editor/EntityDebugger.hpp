#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"

#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class EntityDebuggerPanel {
public:
    struct ComponentInfo {
        std::string name;
        u32 componentId = u32_max;
        usize sizeBytes = 0;
        const void* rawData = nullptr;
        std::vector<std::string> matchingSystems;
    };

    EntityDebuggerPanel() = default;

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

#ifdef CF_HAS_IMGUI
    void render(ECS::World& world, EditorContext& ctx);
#endif

private:
#ifdef CF_HAS_IMGUI
    void refresh(ECS::World& world, ECS::Entity entity);
    void renderEntityHeader(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
    void renderArchetypeInfo(ECS::World& world, ECS::Entity entity);
    void renderComponentList();
    void renderSystemQueries(ECS::World& world);
    void renderMemoryHexView(const u8* data, usize size);
    ECS::Entity resolveTargetEntity(ECS::World& world, EditorContext& ctx) const;
    std::string componentNameForId(u32 componentId) const;
#endif

    bool m_open = false;
    bool m_autoRefresh = true;
    bool m_useManualId = false;
    u32 m_manualEntityId = 0;
    int m_expandedComponent = -1;

    ECS::Entity m_targetEntity;
    std::vector<ComponentInfo> m_cachedComponents;
    usize m_totalComponentBytes = 0;
    u32 m_archetypeIndex = u32_max;
    u64 m_archetypeHash = 0;
};

}  // namespace Caffeine::Editor
