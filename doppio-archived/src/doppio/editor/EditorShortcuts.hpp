#pragma once

#include "core/Types.hpp"

#include <cstdio>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

enum class Shortcut : u8 {
    GizmoTranslate,
    GizmoRotate,
    GizmoScale,
    CameraForward,
    CameraBack,
    CameraLeft,
    CameraRight,
    Save,
    NewScene,
    Undo,
    Redo,
    Copy,
    Paste,
    Duplicate,
    CommandPalette,
    CommandPaletteAlt,
    DeleteSelection,
    Count
};

struct ShortcutBinding {
    int key = 0;
    bool ctrl = false;
    bool shift = false;
};

class EditorShortcuts {
public:
    static EditorShortcuts& instance() {
        static EditorShortcuts shortcuts;
        return shortcuts;
    }

    ShortcutBinding& binding(Shortcut id) { return m_bindings[static_cast<int>(id)]; }
    const ShortcutBinding& binding(Shortcut id) const { return m_bindings[static_cast<int>(id)]; }

    static const char* label(Shortcut id) {
        switch (id) {
            case Shortcut::GizmoTranslate: return "Move";
            case Shortcut::GizmoRotate: return "Rotate";
            case Shortcut::GizmoScale: return "Scale";
            case Shortcut::CameraForward: return "Camera Forward";
            case Shortcut::CameraBack: return "Camera Back";
            case Shortcut::CameraLeft: return "Camera Left";
            case Shortcut::CameraRight: return "Camera Right";
            case Shortcut::Save: return "Save";
            case Shortcut::NewScene: return "New Scene";
            case Shortcut::Undo: return "Undo";
            case Shortcut::Redo: return "Redo";
            case Shortcut::Copy: return "Copy";
            case Shortcut::Paste: return "Paste";
            case Shortcut::Duplicate: return "Duplicate";
            case Shortcut::CommandPalette: return "Quick Search";
            case Shortcut::CommandPaletteAlt: return "Quick Search (alt)";
            case Shortcut::DeleteSelection: return "Delete Selection";
            case Shortcut::Count: break;
        }
        return "";
    }

    const char* chord(Shortcut id) const {
        static char text[64];
        text[0] = '\0';
#ifdef CF_HAS_IMGUI
        const ShortcutBinding& item = binding(id);
        if (item.key == ImGuiKey_None) {
            std::snprintf(text, sizeof(text), "—");
            return text;
        }
        std::snprintf(text, sizeof(text), "%s%s%s",
                      item.ctrl ? "Ctrl+" : "",
                      item.shift ? "Shift+" : "",
                      ImGui::GetKeyName(static_cast<ImGuiKey>(item.key)));
#else
        (void)id;
#endif
        return text;
    }

    bool pressed(Shortcut id) const {
#ifdef CF_HAS_IMGUI
        const ShortcutBinding& item = binding(id);
        if (item.key == ImGuiKey_None) return false;
        const ImGuiIO& io = ImGui::GetIO();
        if (!item.ctrl && io.WantTextInput) return false;
        if (item.ctrl != io.KeyCtrl || item.shift != io.KeyShift) return false;
        return ImGui::IsKeyPressed(static_cast<ImGuiKey>(item.key));
#else
        (void)id;
        return false;
#endif
    }

    bool down(Shortcut id) const {
#ifdef CF_HAS_IMGUI
        const ShortcutBinding& item = binding(id);
        if (item.key == ImGuiKey_None) return false;
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return false;
        if (item.ctrl != io.KeyCtrl) return false;
        return ImGui::IsKeyDown(static_cast<ImGuiKey>(item.key));
#else
        (void)id;
        return false;
#endif
    }

    void beginCapture(Shortcut id) { m_capture = static_cast<int>(id); }
    bool capturing(Shortcut id) const { return m_capture == static_cast<int>(id); }
    bool isCapturing() const { return m_capture >= 0; }

    bool pollCapture() {
#ifdef CF_HAS_IMGUI
        if (m_capture < 0) return false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_capture = -1;
            return false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
            if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
                key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
                key == ImGuiKey_Escape) {
                continue;
            }
            if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(key))) continue;
            ShortcutBinding& item = m_bindings[m_capture];
            item.key = key;
            item.ctrl = io.KeyCtrl;
            item.shift = io.KeyShift;
            for (int other = 0; other < static_cast<int>(Shortcut::Count); ++other) {
                if (other == m_capture) continue;
                ShortcutBinding& taken = m_bindings[other];
                if (taken.key == item.key && taken.ctrl == item.ctrl && taken.shift == item.shift) {
                    taken.key = ImGuiKey_None;
                }
            }
            m_capture = -1;
            return true;
        }
#else
        (void)0;
#endif
        return false;
    }

    void resetDefaults() {
        for (ShortcutBinding& item : m_bindings) item = {};
#ifdef CF_HAS_IMGUI
        auto set = [this](Shortcut id, ImGuiKey key, bool ctrl, bool shift) {
            ShortcutBinding& item = binding(id);
            item.key = key;
            item.ctrl = ctrl;
            item.shift = shift;
        };
        set(Shortcut::GizmoTranslate, ImGuiKey_T, false, false);
        set(Shortcut::GizmoRotate, ImGuiKey_R, false, false);
        set(Shortcut::GizmoScale, ImGuiKey_E, false, false);
        set(Shortcut::CameraForward, ImGuiKey_W, false, false);
        set(Shortcut::CameraBack, ImGuiKey_S, false, false);
        set(Shortcut::CameraLeft, ImGuiKey_A, false, false);
        set(Shortcut::CameraRight, ImGuiKey_D, false, false);
        set(Shortcut::Save, ImGuiKey_S, true, false);
        set(Shortcut::NewScene, ImGuiKey_N, true, false);
        set(Shortcut::Undo, ImGuiKey_Z, true, false);
        set(Shortcut::Redo, ImGuiKey_Y, true, false);
        set(Shortcut::Copy, ImGuiKey_C, true, false);
        set(Shortcut::Paste, ImGuiKey_V, true, false);
        set(Shortcut::Duplicate, ImGuiKey_D, true, false);
        set(Shortcut::CommandPalette, ImGuiKey_L, true, false);
        set(Shortcut::CommandPaletteAlt, ImGuiKey_P, true, true);
        set(Shortcut::DeleteSelection, ImGuiKey_Delete, false, false);
#endif
        m_capture = -1;
    }

    std::string serialize() const {
        std::string out;
        for (int i = 0; i < static_cast<int>(Shortcut::Count); ++i) {
            const ShortcutBinding& item = m_bindings[i];
            out += std::to_string(item.key);
            out += ':';
            out += item.ctrl ? '1' : '0';
            out += ':';
            out += item.shift ? '1' : '0';
            out += ';';
        }
        return out;
    }

    void deserialize(const std::string& text) {
        if (text.empty()) return;
        resetDefaults();
        int index = 0;
        size_t cursor = 0;
        while (index < static_cast<int>(Shortcut::Count) && cursor < text.size()) {
            const size_t end = text.find(';', cursor);
            const std::string token = text.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
            int key = 0;
            int ctrl = 0;
            int shift = 0;
            if (std::sscanf(token.c_str(), "%d:%d:%d", &key, &ctrl, &shift) == 3) {
                m_bindings[index].key = key;
                m_bindings[index].ctrl = ctrl != 0;
                m_bindings[index].shift = shift != 0;
            }
            ++index;
            if (end == std::string::npos) break;
            cursor = end + 1;
        }
    }

private:
    EditorShortcuts() { resetDefaults(); }

    ShortcutBinding m_bindings[static_cast<int>(Shortcut::Count)]{};
    int m_capture = -1;
};

}  // namespace Caffeine::Editor
