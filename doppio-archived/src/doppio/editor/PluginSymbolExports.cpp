#include "editor/PluginSymbolExports.hpp"

#include "terrain/TerrainHeightmapImporter.hpp"

#include <cstdlib>

namespace Caffeine::Editor {

// dlopen'd plugins resolve core symbols from the doppio executable (-rdynamic).
// Keep global references so the linker pulls in TerrainHeightmapImporter and exports
// the symbols (function-local statics were optimized away).
[[gnu::used]] void* g_pluginTerrainSymbols[] = {
    reinterpret_cast<void*>(&Terrain::importHeightmapFile),
    reinterpret_cast<void*>(&Terrain::applyAbsoluteHeights),
    reinterpret_cast<void*>(&Terrain::loadHeightmapTextFile),
};

void anchorPluginHostSymbols() {
    for (void* symbol : g_pluginTerrainSymbols) {
        if (symbol == nullptr) {
            std::abort();
        }
    }
}

}  // namespace Caffeine::Editor
