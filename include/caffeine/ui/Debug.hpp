#pragma once

/**
 * Caffeine UI — debugging helpers for element trees.
 *
 * UIContext already records a flattened snapshot every frame. DebugPanel shows
 * the previous frame as an indented list (type, id, size and interaction state),
 * which makes it easy to see why a plugin panel or a new element behaves the
 * way it does.
 */

#include "caffeine/ui/Container.hpp"
#include "caffeine/ui/Display.hpp"

#include <imgui.h>

#include <string>

namespace Caffeine::UIKit::UI {

class DebugPanel : public Column {
public:
    explicit DebugPanel(UIContext& context)
        : Column("DebugPanel"), m_context(&context) {}

    const char* type() const override { return "DebugPanel"; }

    void draw(UIContext& ctx) override {
        ImGui::TextColored(ctx.theme().textMuted, "Element tree (previous frame)");
        const float scale = std::max(ctx.scale(), 0.001f);
        const float height = size().height > 0.0f ? size().height * scale : 260.0f * scale;
        if (ImGui::BeginChild("##debug_tree", ImVec2(0.0f, height),
                              ImGuiChildFlags_Borders)) {
            if (!m_context) {
                ImGui::TextDisabled("No UIContext");
            } else {
                const auto& entries = m_context->previousDebugEntries();
                for (const DebugEntry& entry : entries) {
                    std::string indent(static_cast<std::size_t>(entry.depth) * 2, ' ');
                    const ImVec4 color = entry.hovered ? ctx.theme().accent
                                                       : ctx.theme().textMuted;
                    ImGui::TextColored(color, "%s%s #%s", indent.c_str(), entry.type.c_str(),
                                       entry.id.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%.0fx%.0f%s%s", entry.max.x - entry.min.x,
                                        entry.max.y - entry.min.y,
                                        entry.enabled ? "" : " disabled",
                                        entry.visible ? "" : " hidden");
                }
            }
        }
        ImGui::EndChild();
    }

private:
    UIContext* m_context = nullptr;
};

}  // namespace Caffeine::UIKit::UI
