#pragma once
#include "core/Types.hpp"
#include "assets/AssetTypes.hpp"
#include <string>
#include <filesystem>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

// ── Payload type identifiers ────────────────────────────────────

constexpr const char* kPayloadAssetPath  = "ASSET_PATH";
constexpr const char* kPayloadEntityDrag = "ENTITY_DRAG";

// ── Drag-drop payload data ──────────────────────────────────────

/// Carries an asset filesystem path + type through a drag-drop operation.
struct AssetDropPayload {
    char  path[512];
    AssetType type;
};

// ── DragDropManager ─────────────────────────────────────────────

/// Static helpers for ImGui drag-source and drop-target operations.
/// Wraps ImGui's payload API with Caffeine-specific payload types.
class DragDropManager {
public:
    /// Begin an asset drag-source. Returns true if the source is active.
    static bool SourceAsset(const char* path, AssetType type, const char* label) {
#ifdef CF_HAS_IMGUI
        if (!ImGui::BeginDragDropSource()) return false;
        AssetDropPayload payload;
        strncpy(payload.path, path, sizeof(payload.path) - 1);
        payload.path[sizeof(payload.path) - 1] = '\0';
        payload.type = type;
        ImGui::SetDragDropPayload(kPayloadAssetPath, &payload, sizeof(payload));
        ImGui::Text("%s", label);
        ImGui::EndDragDropSource();
        return true;
#else
        (void)path; (void)type; (void)label;
        return false;
#endif
    }

    /// Begin an entity drag-source. Returns true if the source is active.
    static bool SourceEntity(u32 entityId, const char* label) {
#ifdef CF_HAS_IMGUI
        if (!ImGui::BeginDragDropSource()) return false;
        ImGui::SetDragDropPayload(kPayloadEntityDrag, &entityId, sizeof(u32));
        ImGui::Text("%s", label);
        ImGui::EndDragDropSource();
        return true;
#else
        (void)entityId; (void)label;
        return false;
#endif
    }

    /// Accept an asset drop on the current item. Returns a pointer to the
    /// payload data if dropped, or nullptr. The payload is valid only during
    /// the current frame.
    static const AssetDropPayload* AcceptAssetDrop() {
#ifdef CF_HAS_IMGUI
        if (!ImGui::BeginDragDropTarget()) return nullptr;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadAssetPath)) {
            const auto* result = static_cast<const AssetDropPayload*>(payload->Data);
            ImGui::EndDragDropTarget();
            return result;
        }
        ImGui::EndDragDropTarget();
#endif
        return nullptr;
    }

    /// Accept an entity drop on the current item. Returns the entity ID, or
    /// u32_max if no drop occurred.
    static u32 AcceptEntityDrop() {
#ifdef CF_HAS_IMGUI
        if (!ImGui::BeginDragDropTarget()) return u32_max;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadEntityDrag)) {
            u32 eid = *static_cast<const u32*>(payload->Data);
            ImGui::EndDragDropTarget();
            return eid;
        }
        ImGui::EndDragDropTarget();
#endif
        return u32_max;
    }
};

} // namespace Caffeine::Editor
