#pragma once

#include "caffeine/ui/UI.hpp"

#include <memory>

namespace Caffeine::Editor {

/// Living catalogue of the composable UI kit.
///
/// The gallery is built once from Element instances: cards, rows, controls,
/// property grids, a canvas, a node canvas and the element debug tree. It is
/// intentionally small enough to read as documentation and it exercises every
/// public element in the Doppio build.
class UiGalleryPanel {
public:
    UiGalleryPanel();
    ~UiGalleryPanel();

    void render();

    void open() { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

private:
    void build();

    bool m_open = false;
    bool m_built = false;
    bool m_debugOverlay = false;
    bool m_toggle = true;

    float m_slider = 0.45f;
    float m_progress = 0.35f;
    float m_color[4] = {0.86f, 0.35f, 0.55f, 1.0f};
    int m_mode = 1;
    int m_segment = 0;
    int m_count = 12;
    std::string m_search = "node";
    std::string m_name = "Natural Fluids";
    std::string m_status = "Ready";
    char m_notes[256] = "Notes support multiline editing.";

    Caffeine::UIKit::Elements::UIContext m_context;
    std::unique_ptr<Caffeine::UIKit::Elements::Panel> m_root;
    Caffeine::UIKit::Elements::Label* m_statusLabel = nullptr;
    Caffeine::UIKit::Elements::Label* m_tickLabel = nullptr;
};

}  // namespace Caffeine::Editor
