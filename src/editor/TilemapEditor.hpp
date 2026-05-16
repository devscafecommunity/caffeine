#pragma once
#include "core/Types.hpp"
#include <string>
#include <vector>
#include <memory>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

enum class ToolMode {
    Brush,
    Bucket,
    Eraser,
    Picker
};

struct TileCell {
    i32 tileID = -1;
    u32 flags = 0;
};

class TileLayer {
public:
    TileLayer() = default;
    TileLayer(const std::string& name, i32 width, i32 height);

    const std::string& name() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    i32 width() const { return m_width; }
    i32 height() const { return m_height; }

    bool isVisible() const { return m_isVisible; }
    void setVisible(bool visible) { m_isVisible = visible; }
    bool& visible() { return m_isVisible; }

    bool isLocked() const { return m_isLocked; }
    void setLocked(bool locked) { m_isLocked = locked; }
    bool& locked() { return m_isLocked; }

    TileCell& getCell(i32 x, i32 y);
    const TileCell& getCell(i32 x, i32 y) const;

    void resize(i32 width, i32 height);

private:
    std::string m_name;
    std::vector<TileCell> m_cells;
    i32 m_width = 0;
    i32 m_height = 0;
    bool m_isVisible = true;
    bool m_isLocked = false;
};

class Tilemap {
public:
    Tilemap() = default;

    i32 width() const { return m_width; }
    i32 height() const { return m_height; }
    void setSize(i32 width, i32 height);

    f32 tileSize() const { return m_tileSize; }
    void setTileSize(f32 size) { m_tileSize = size; }

    usize layerCount() const { return m_layers.size(); }
    TileLayer& layer(usize index);
    const TileLayer& layer(usize index) const;

    void addLayer(const std::string& name);
    void removeLayer(usize index);
    void moveLayer(usize from, usize to);

private:
    std::vector<std::unique_ptr<TileLayer>> m_layers;
    i32 m_width = 32;
    i32 m_height = 32;
    f32 m_tileSize = 32.0f;
};

class TilemapEditorPanel {
public:
    TilemapEditorPanel();

    void render();
    void renderPalette();
    void renderToolbar();
    void renderLayers();
    void renderGrid();

    void paintTile(i32 layerIdx, i32 x, i32 y, i32 tileID);
    void floodFill(i32 layerIdx, i32 x, i32 y, i32 targetID, i32 replacementID);
    void eraseTile(i32 layerIdx, i32 x, i32 y);

    void applyAutoTile(i32 layerIdx, i32 x, i32 y);

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open() { m_open = true; }

    Tilemap& tilemap() { return m_tilemap; }
    const Tilemap& tilemap() const { return m_tilemap; }

private:
    bool hasNeighbor(i32 layerIdx, i32 x, i32 y, i32 tileID) const;

    Tilemap m_tilemap;
    i32 m_selectedTileID = 0;
    i32 m_currentLayer = 0;
    ToolMode m_currentTool = ToolMode::Brush;
    bool m_autoTileEnabled = false;
    bool m_open = true;

    i32 m_brushSize = 1;
};

}