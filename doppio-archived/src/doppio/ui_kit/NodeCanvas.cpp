#include "caffeine/ui/NodeCanvas.hpp"

#include "caffeine/ui/Theme.hpp"

#include <imnodes.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>

namespace Caffeine::UIKit {

namespace {

ImU32 toColor(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImU32 lighten(ImU32 color, int amount) {
    auto channel = [&](int shift) {
        const int value = static_cast<int>((color >> shift) & 0xFF) + amount;
        return value > 255 ? 255 : value;
    };
    return IM_COL32(channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT),
                    channel(IM_COL32_B_SHIFT), 255);
}

constexpr float kZoomMin = 0.35f;
constexpr float kZoomMax = 2.5f;

}  // namespace

NodeCanvas::NodeCanvas() = default;

NodeCanvas::~NodeCanvas() {
    if (m_context && ImNodes::GetCurrentContext()) {
        ImNodes::EditorContextFree(m_context);
        m_context = nullptr;
    }
    if (m_ownsRootContext) {
        ImNodes::DestroyContext();
        m_ownsRootContext = false;
    }
}

void NodeCanvas::ensureReady() {
    if (!ImNodes::GetCurrentContext()) {
        ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
        ImNodes::CreateContext();
        ImNodes::StyleColorsDark();
        m_ownsRootContext = true;
    }
    if (!m_context) {
        m_context = ImNodes::EditorContextCreate();
    }

    const ThemeTokens& t = theme();
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = t.rounding;
    style.NodePadding = ImVec2(t.paddingX, t.paddingY);
    style.NodeBorderThickness = t.borderSize;
    style.LinkThickness = 2.0f;
    style.LinkLineSegmentsPerLength = 0.08f;
    style.PinCircleRadius = 4.0f;
    style.PinHoverRadius = 9.0f;
    style.GridSpacing = 24.0f;
    style.Colors[ImNodesCol_GridBackground] = toColor(t.background);
    style.Colors[ImNodesCol_GridLine] = toColor(t.border);
    style.Colors[ImNodesCol_NodeBackground] = toColor(t.surfaceRaised);
    style.Colors[ImNodesCol_NodeBackgroundHovered] = toColor(t.header);
    style.Colors[ImNodesCol_NodeBackgroundSelected] = toColor(t.surfaceOverlay);
    style.Colors[ImNodesCol_NodeOutline] = toColor(t.borderStrong);
    style.Colors[ImNodesCol_Link] = toColor(t.info);
    style.Colors[ImNodesCol_LinkHovered] = toColor(t.accent);
    style.Colors[ImNodesCol_LinkSelected] = toColor(t.warning);
    style.Colors[ImNodesCol_Pin] = toColor(t.textMuted);
    style.Colors[ImNodesCol_PinHovered] = toColor(t.accentHovered);
    style.Colors[ImNodesCol_BoxSelector] = toColor(t.selection);
    style.Colors[ImNodesCol_BoxSelectorOutline] = toColor(t.accent);
}

void NodeCanvas::activate() {
    ensureReady();
    ImNodes::EditorContextSet(m_context);
}

void NodeCanvas::begin() {
    ensureReady();
    ImNodes::EditorContextSet(m_context);

    const float z = m_zoom;
    ImNodes::PushStyleVar(ImNodesStyleVar_GridSpacing, 24.0f * z);
    ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding,
                          ImVec2(theme().paddingX * z, theme().paddingY * z));
    ImNodes::PushStyleVar(ImNodesStyleVar_NodeBorderThickness, theme().borderSize);
    ImNodes::PushStyleVar(ImNodesStyleVar_LinkThickness, 2.0f * z);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinCircleRadius, 4.0f * z);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinHoverRadius, 9.0f * z);

    m_restoreFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
    ImNodes::BeginNodeEditor();
    ImGui::SetWindowFontScale(z);
}

void NodeCanvas::end() {
    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    ImGui::SetWindowFontScale(m_restoreFontScale);
    ImNodes::PopStyleVar(6);

    if (ImNodes::IsEditorHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            setZoom(m_zoom + wheel * 0.1f);
        }
    }
}

void NodeCanvas::setZoom(float zoom) {
    const float clamped = std::clamp(zoom, kZoomMin, kZoomMax);
    if (clamped == m_zoom) return;
    m_zoom = clamped;
    m_zoomChanged = true;
}

bool NodeCanvas::consumeZoomChanged() {
    const bool changed = m_zoomChanged;
    m_zoomChanged = false;
    return changed;
}

ImVec2 NodeCanvas::worldToCanvas(const ImVec2& world) const {
    return ImVec2(world.x * m_zoom, world.y * m_zoom);
}

ImVec2 NodeCanvas::canvasToWorld(const ImVec2& canvas) const {
    return ImVec2(canvas.x / m_zoom, canvas.y / m_zoom);
}

ImVec2 NodeCanvas::screenToWorld(const ImVec2& screen, int anchorImNode,
                                 const ImVec2& anchorWorld) const {
    const ImVec2 anchorScreen = ImNodes::GetNodeScreenSpacePos(anchorImNode);
    const ImVec2 delta(screen.x - anchorScreen.x, screen.y - anchorScreen.y);
    return canvasToWorld(ImVec2(worldToCanvas(anchorWorld).x + delta.x,
                                worldToCanvas(anchorWorld).y + delta.y));
}

bool NodeCanvas::centerOnWorldBounds(const ImVec2& min, const ImVec2& max) {
    const ImVec2 view = ImGui::GetWindowSize();
    if (view.x < 32.0f || view.y < 32.0f) return false;
    const ImVec2 centerWorld((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const ImVec2 centerCanvas = worldToCanvas(centerWorld);
    ImNodes::EditorContextResetPanning(
        ImVec2(view.x * 0.5f - centerCanvas.x, view.y * 0.5f - centerCanvas.y));
    return true;
}

void NodeCanvas::beginNode(int imId, const char* title, const char* subtitle,
                           unsigned int headerColor) {
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, headerColor);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, lighten(headerColor, 18));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, lighten(headerColor, 30));
    ImNodes::BeginNode(imId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(title);
    ImNodes::EndNodeTitleBar();
    ImGui::Dummy(ImVec2(scaled(168.0f), 1.0f));
    if (subtitle && subtitle[0] != '\0') {
        ImGui::TextDisabled("%s", subtitle);
        ImGui::Spacing();
    }
}

void NodeCanvas::endNode() {
    ImNodes::EndNode();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

bool NodeCanvas::acceptNodeDrop(const char* payloadId, std::string& outType) {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImRect rect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##caffeine_node_drop"))) {
        return false;
    }
    bool accepted = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId)) {
        const char* type = static_cast<const char*>(payload->Data);
        if (type && type[0] != '\0') {
            outType = type;
            accepted = true;
        }
    }
    ImGui::EndDragDropTarget();
    return accepted;
}

}  // namespace Caffeine::UIKit
