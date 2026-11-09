#pragma once

/**
 * Caffeine UI — panel registry.
 *
 * Single source of truth for which panels exist, whether they are visible and
 * how they are rendered. Doppio core panels and plugin panels live in the same
 * registry; removing a plugin removes every contribution it owned.
 */

#include "caffeine/ui/Panel.hpp"

#include <string>
#include <vector>

namespace Caffeine::UIKit {

class PanelRegistry {
public:
    static PanelRegistry& instance();

    /// Register or replace a panel by id. Replacing keeps ordering stable and
    /// lets a hot-reloaded plugin update its render callback in place.
    bool add(PanelDesc desc);

    bool remove(const std::string& id);
    void removeOwner(const std::string& owner);
    void clear();

    PanelDesc* find(const std::string& id);
    const PanelDesc* find(const std::string& id) const;

    const std::vector<PanelDesc>& panels() const { return m_panels; }

    /// Bumps when panels are added/removed so the host can rebuild docking.
    unsigned long long revision() const { return m_revision; }

    bool isVisible(const std::string& id) const;
    void setVisible(const std::string& id, bool visible);
    void toggleVisible(const std::string& id);
    /// Make a panel visible and call its open hook.
    void open(const std::string& id);

    /// Render every visible panel with size constraints applied.
    void renderAll();

    /// Draw a "Panels" submenu grouped by category. Call inside BeginMenu.
    void drawMenu();

    /// Default visible set from descriptors (used when a layout is first built).
    void applyDefaults();

private:
    PanelRegistry() = default;

    std::vector<PanelDesc> m_panels;
    std::string m_pendingFocus;
    unsigned long long m_revision = 1;
};

}  // namespace Caffeine::UIKit
