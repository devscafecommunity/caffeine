#pragma once

#include <imgui.h>

#include <string>

struct ImNodesEditorContext;

namespace Caffeine::UIKit {

/// Reusable node-editor surface used by Doppio tools.
///
/// Owns the imnodes context, theme, zoom and view transforms. The host tool
/// draws node bodies between beginNode()/endNode(); graph semantics stay in the
/// host. Zoom is implemented by scaling node positions together with the window
/// font and imnodes style, so the whole canvas scales as one unit.
class NodeCanvas {
public:
    NodeCanvas();
    ~NodeCanvas();

    NodeCanvas(const NodeCanvas&) = delete;
    NodeCanvas& operator=(const NodeCanvas&) = delete;

    void ensureReady();
    void activate();

    void begin();
    void end();

    float zoom() const { return m_zoom; }
    void setZoom(float zoom);
    bool consumeZoomChanged();

    /// Multiplies a pixel measurement by the canvas zoom.
    float scaled(float pixels) const { return pixels * m_zoom; }

    ImVec2 worldToCanvas(const ImVec2& world) const;
    ImVec2 canvasToWorld(const ImVec2& canvas) const;

    /// Converts a screen position to world space using an existing node as the
    /// anchor (imnodes does not expose the canvas origin publicly).
    ImVec2 screenToWorld(const ImVec2& screen, int anchorImNode,
                         const ImVec2& anchorWorld) const;

    /// Centers world-space bounds in the canvas view. Returns false when the
    /// canvas has not been laid out yet, so the caller can retry next frame.
    bool centerOnWorldBounds(const ImVec2& min, const ImVec2& max);

    /// Standard node chrome: coloured title bar, minimum width and subtitle.
    void beginNode(int imId, const char* title, const char* subtitle, unsigned int headerColor);
    void endNode();

    /// Accepts a drag payload carrying a node type name dropped on the canvas.
    bool acceptNodeDrop(const char* payloadId, std::string& outType);

    ImNodesEditorContext* context() const { return m_context; }

private:
    ImNodesEditorContext* m_context = nullptr;
    bool m_ownsRootContext = false;
    float m_zoom = 1.0f;
    bool m_zoomChanged = false;
    float m_restoreFontScale = 1.0f;
};

} // namespace Caffeine::UIKit
