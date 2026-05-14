#include "editor/EditorContext.hpp"
#include "scene/SceneSerializer.hpp"
#include "core/Assertions.hpp"
#include <cstring>

namespace Caffeine::Editor {

const char* getEntityName(ECS::World& world, ECS::Entity e) {
    if (auto* nc = world.get<NameComponent>(e)) {
        return nc->name;
    }
    static const char* s_unnamed = "Unnamed";
    return s_unnamed;
}

void setEntityName(ECS::World& world, ECS::Entity e, const char* name) {
    auto& nc = world.add<NameComponent>(e);
    std::strncpy(nc.name, name, sizeof(nc.name) - 1);
    nc.name[sizeof(nc.name) - 1] = '\0';
}

// ============================================================================
// UndoStack
// ============================================================================

void UndoStack::push(const EditorCommand& cmd) {
    // If we're mid-buffer (after an undo), truncate any redo history
    if (m_current < m_count) {
        for (u32 i = m_current; i < m_count; ++i) {
            m_commands[i].beforeState.clear();
            m_commands[i].afterState.clear();
        }
        m_count = m_current;
    }

    // If the buffer is full, discard the oldest entry (circular shift)
    if (m_count >= MAX_UNDO) {
        for (u32 i = 0; i < MAX_UNDO - 1; ++i) {
            m_commands[i] = std::move(m_commands[i + 1]);
        }
        m_count = MAX_UNDO - 1;
    }

    m_commands[m_count] = cmd;
    ++m_count;
    m_current = m_count;
}

bool UndoStack::undo(ECS::World& world) {
    if (!canUndo()) return false;

    --m_current;
    applySnapshot(world, m_commands[m_current].beforeState);
    return true;
}

bool UndoStack::redo(ECS::World& world) {
    if (!canRedo()) return false;

    applySnapshot(world, m_commands[m_current].afterState);
    ++m_current;
    return true;
}

void UndoStack::clear() {
    for (u32 i = 0; i < m_count; ++i) {
        m_commands[i].beforeState.clear();
        m_commands[i].afterState.clear();
    }
    m_count   = 0;
    m_current = 0;
}

void UndoStack::applySnapshot(ECS::World& world, const std::vector<u8>& snapshot) {
    CF_ASSERT(!snapshot.empty(), "UndoStack::applySnapshot — empty snapshot");

    world.destroyAll();

    Scene::SceneSerializer serializer(world);
    serializer.deserializeFromMemory(snapshot);
}

// ============================================================================
// EditorContext
// ============================================================================

void EditorContext::beginUndo(EditorCommand::Type type, u32 entityId, ECS::World& world) {
    CF_ASSERT(!m_undoPending, "EditorContext::beginUndo — called without matching endUndo");

    m_pendingCmd = EditorCommand{};
    m_pendingCmd.type          = type;
    m_pendingCmd.targetEntity  = entityId;

    Scene::SceneSerializer serializer(world);
    serializer.serializeToMemory(m_pendingCmd.beforeState);

    m_undoPending = true;
}

void EditorContext::endUndo(ECS::World& world) {
    CF_ASSERT(m_undoPending, "EditorContext::endUndo — called without matching beginUndo");

    Scene::SceneSerializer serializer(world);
    serializer.serializeToMemory(m_pendingCmd.afterState);

    undoStack.push(m_pendingCmd);

    m_undoPending = false;
    isDirty       = true;
}

} // namespace Caffeine::Editor
