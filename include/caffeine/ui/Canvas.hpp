#pragma once

/**
 * Caffeine UI — canvases and node graphs as composable elements.
 *
 * Canvas is a blank drawing surface with a callback. NodeCanvasView embeds the
 * reusable NodeCanvas and owns a list of NodeElement children; NodeElement owns
 * its body elements (labels, sliders, InputPin/OutputPin, etc.).
 *
 * The element model never owns graph semantics: links, validation and the
 * script/asset behind a graph stay in the host tool.
 */

#include "caffeine/ui/Container.hpp"
#include "caffeine/ui/NodeCanvas.hpp"

#include <imnodes.h>
#include <imgui.h>

#include <functional>
#include <string>
#include <utility>

namespace Caffeine::UIKit::UI {

class Canvas : public Element {
public:
    using DrawFn = std::function<void(UIContext&, ImDrawList*, const ImVec2& origin,
                                      const ImVec2& size)>;

    explicit Canvas(std::string id = {}) {
        if (!id.empty()) setId(std::move(id));
    }

    const char* type() const override { return "Canvas"; }

    Canvas& setOnDraw(DrawFn callback) {
        m_onDraw = std::move(callback);
        return *this;
    }
    Canvas& setBackground(ImVec4 background) {
        m_background = background;
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    DrawFn m_onDraw;
    ImVec4 m_background{0.04f, 0.05f, 0.07f, 1.0f};
};

class NodeCanvasView : public Container {
public:
    explicit NodeCanvasView(std::string id = {}) : Container(Axis::Vertical) {
        if (!id.empty()) setId(std::move(id));
    }

    const char* type() const override { return "NodeCanvasView"; }

    Caffeine::UIKit::NodeCanvas& canvas() { return m_canvas; }
    const Caffeine::UIKit::NodeCanvas& canvas() const { return m_canvas; }

    void draw(UIContext& ctx) override;

private:
    Caffeine::UIKit::NodeCanvas m_canvas;
};

class NodeElement : public Container {
public:
    NodeElement(int imNodeId, std::string title, std::string subtitle = {},
                ImU32 headerColor = IM_COL32(86, 110, 160, 255))
        : Container(Axis::Vertical),
          m_imNodeId(imNodeId),
          m_title(std::move(title)),
          m_subtitle(std::move(subtitle)),
          m_headerColor(headerColor) {
        setId("Node:" + std::to_string(imNodeId));
    }

    const char* type() const override { return "NodeElement"; }
    int imNodeId() const { return m_imNodeId; }
    const std::string& title() const { return m_title; }

    NodeElement& setHeaderColor(ImU32 color) {
        m_headerColor = color;
        return *this;
    }

    void draw(UIContext& ctx) override;

private:
    int m_imNodeId = 0;
    std::string m_title;
    std::string m_subtitle;
    ImU32 m_headerColor = IM_COL32(86, 110, 160, 255);
};

class InputPin : public Element {
public:
    InputPin(int pinId, std::string label = {})
        : m_pinId(pinId), m_label(std::move(label)) {
        setId("InputPin:" + std::to_string(pinId));
    }
    const char* type() const override { return "InputPin"; }
    void draw(UIContext&) override;

private:
    int m_pinId = 0;
    std::string m_label;
};

class OutputPin : public Element {
public:
    OutputPin(int pinId, std::string label = {})
        : m_pinId(pinId), m_label(std::move(label)) {
        setId("OutputPin:" + std::to_string(pinId));
    }
    const char* type() const override { return "OutputPin"; }
    void draw(UIContext&) override;

private:
    int m_pinId = 0;
    std::string m_label;
};

// ── Inline implementation ──────────────────────────────────────────────────

inline void Canvas::draw(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    const ImVec2 size = controlSize(*this);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, m_background);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,
                        style().rounding >= 0.0f ? style().rounding : ctx.theme().rounding);
    const bool open = ImGui::BeginChild("##canvas", size, ImGuiChildFlags_Borders);
    if (open) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y),
                                ImGui::ColorConvertFloat4ToU32(m_background));
        if (m_onDraw) m_onDraw(ctx, drawList, origin, avail);
        ImGui::Dummy(avail);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

inline void NodeCanvasView::draw(UIContext& ctx) {
    const float scale = std::max(ctx.scale(), 0.001f);
    const float width = resolvedWidth() * scale;
    const float height = size().height > 0.0f ? size().height * scale : 420.0f * scale;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ctx.theme().rounding);
    const bool open = ImGui::BeginChild("##node_canvas", ImVec2(width, height),
                                        ImGuiChildFlags_Borders);
    if (open) {
        m_canvas.begin();
        ctx.setNodeCanvas(&m_canvas);
        drawChildren(ctx);
        ctx.setNodeCanvas(nullptr);
        m_canvas.end();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

inline void NodeElement::draw(UIContext& ctx) {
    Caffeine::UIKit::NodeCanvas* canvas = ctx.nodeCanvas();
    if (!canvas) {
        // Fallback keeps the tree usable (and testable) outside a graph.
        ImGui::TextColored(ctx.theme().accent, "%s", m_title.c_str());
        if (!m_subtitle.empty()) ImGui::TextDisabled("%s", m_subtitle.c_str());
        drawChildren(ctx);
        return;
    }

    canvas->beginNode(m_imNodeId, m_title.c_str(),
                      m_subtitle.empty() ? nullptr : m_subtitle.c_str(), m_headerColor);
    drawChildren(ctx);
    canvas->endNode();
}

inline void InputPin::draw(UIContext&) {
    ImNodes::BeginInputAttribute(m_pinId);
    if (!m_label.empty()) ImGui::TextUnformatted(m_label.c_str());
    ImNodes::EndInputAttribute();
}

inline void OutputPin::draw(UIContext&) {
    ImNodes::BeginOutputAttribute(m_pinId);
    if (!m_label.empty()) ImGui::TextUnformatted(m_label.c_str());
    ImNodes::EndOutputAttribute();
}

}  // namespace Caffeine::UIKit::UI
