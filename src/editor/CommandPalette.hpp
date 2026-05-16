#pragma once
#include "core/Types.hpp"
#include "containers/FixedString.hpp"
#include <functional>
#include <vector>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

struct CommandPaletteItem {
    FixedString<64> id;
    FixedString<128> label;
    FixedString<128> category;
    std::function<void()> callback;
    bool enabled = true;
};

class CommandPalette {
public:
    using Item = CommandPaletteItem;

    CommandPalette() = default;

    void registerCommand(const FixedString<64>& id,
                         const FixedString<128>& label,
                         const FixedString<128>& category,
                         std::function<void()> callback,
                         bool enabled = true);

    void unregisterCommand(const FixedString<64>& id);

    void open();
    void close();
    bool isOpen() const { return m_open; }
    void toggle();

    bool handleInput();

    void render();

    void clearResults();
    void filterResults(const char* query);

    usize commandCount() const { return m_commands.size(); }
    const Item& command(usize index) const { return m_commands[index]; }

    usize resultCount() const { return m_filteredResults.size(); }
    const Item* result(usize index) const;

    void setSelected(usize index);
    usize selected() const { return m_selectedIndex; }

    void executeSelected();

    void nextItem();
    void previousItem();

private:
    bool m_open = false;
    char m_searchBuffer[256] = {};
    std::vector<Item> m_commands;
    std::vector<const Item*> m_filteredResults;
    usize m_selectedIndex = 0;
};

}