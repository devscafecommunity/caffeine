#pragma once
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include <string>
#include <vector>
#include <cstring>

#ifdef CF_HAS_SCRIPTING
#include "script/ScriptEngine.hpp"
#endif

namespace Caffeine::Editor {

// ============================================================================
// Name component — stores the editor-facing name of an entity.
// ============================================================================
struct NameComponent {
    char name[64] = "Entity";
};

const char* getEntityName(ECS::World& world, ECS::Entity e);
void        setEntityName(ECS::World& world, ECS::Entity e, const char* name);

// ============================================================================
// EditorCommand — represents a single undoable action.
// beforeState / afterState are full-world snapshots.
// ============================================================================
struct EditorCommand {
    enum Type : u8 {
        AddEntity,
        RemoveEntity,
        AddComponent,
        RemoveComponent,
        SetField,
        MoveEntity
    };

    Type            type;
    u32             targetEntity;  // Entity ID at time of command
    std::vector<u8> beforeState;   // Full world snapshot before the action
    std::vector<u8> afterState;    // Full world snapshot after the action
};

// ============================================================================
// UndoStack — linear undo/redo manager with snapshot-based state.
//
// Maintains a list of EditorCommands up to MAX_UNDO.
// When a new command is pushed after an undo, the redo history is truncated.
// ============================================================================
class UndoStack {
    static constexpr u32 MAX_UNDO = 256;

    EditorCommand           m_commands[MAX_UNDO];
    u32                     m_count   = 0;
    u32                     m_current = 0;  // 0 = at oldest, m_count = at newest

public:
    UndoStack() = default;

    /// Push a new command. Any redo history beyond m_current is discarded.
    void push(const EditorCommand& cmd);

    /// Undo the command at m_current-1. Returns false if nothing to undo.
    bool undo(ECS::World& world);

    /// Redo the command at m_current. Returns false if nothing to redo.
    bool redo(ECS::World& world);

    /// Clear all commands.
    void clear();

    bool canUndo() const { return m_current > 0; }
    bool canRedo() const { return m_current < m_count; }
    u32  count()   const { return m_count; }

private:
    void applySnapshot(ECS::World& world, const std::vector<u8>& snapshot);
};

// ============================================================================
// EditorContext — global editor state shared across all panels.
// ============================================================================
class EditorContext {
public:
    // ── Selection ──────────────────────────────────────────────────────
    ECS::Entity selectedEntity   = ECS::Entity::INVALID;
    ECS::Entity hoveredEntity    = ECS::Entity::INVALID;
    ECS::Entity clipboardEntity  = ECS::Entity::INVALID;

    // ── Scene state ────────────────────────────────────────────────────
    std::string currentScenePath;
    bool        isDirty          = false;

    // ── Gizmo ──────────────────────────────────────────────────────────
    enum class GizmoMode : u8 { None, Translate, Rotate, Scale };
    enum class GizmoSpace : u8 { Local, World };

    GizmoMode  gizmoMode  = GizmoMode::Translate;
    GizmoSpace gizmoSpace = GizmoSpace::World;

    // ── Viewport state ─────────────────────────────────────────────────
    f32 viewportPanX = 0.0f;
    f32 viewportPanY = 0.0f;
    f32 viewportZoom = 1.0f;

    // ── Panel visibility ───────────────────────────────────────────────
    bool hierarchyOpen = true;
    bool inspectorOpen = true;
    bool viewportOpen  = true;
    bool assetsOpen    = true;

    // ── Undo system ────────────────────────────────────────────────────
    UndoStack undoStack;

#ifdef CF_HAS_SCRIPTING
    // ── Script Engine ──────────────────────────────────────────────────
    Script::ScriptEngine* scriptEngine = nullptr;
#endif

    // ── Methods ────────────────────────────────────────────────────────

    /// Select an entity (updates selectedEntity).
    void selectEntity(ECS::Entity e) { selectedEntity = e; }

    /// Mark the scene as modified (dirty).
    void markDirty() { isDirty = true; }

    /// Clear selection and hover.
    void clearSelection() {
        selectedEntity  = ECS::Entity::INVALID;
        hoveredEntity   = ECS::Entity::INVALID;
    }

    /// Capture the current world as the "before" snapshot, begin an undo
    /// command. Must be followed by endUndo() after the edit.
    void beginUndo(EditorCommand::Type type, u32 entityId, ECS::World& world);

    /// Capture the current world as the "after" snapshot and push the
    /// pending undo command onto the stack. Marks dirty automatically.
    void endUndo(ECS::World& world);

private:
    bool            m_undoPending = false;
    EditorCommand   m_pendingCmd;
};

} // namespace Caffeine::Editor
