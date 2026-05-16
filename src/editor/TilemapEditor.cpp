#include "editor/TilemapEditor.hpp"

namespace Caffeine::Editor {

TileLayer::TileLayer(const std::string& name, i32 width, i32 height)
    : m_name(name), m_width(width), m_height(height) {
    m_cells.resize(static_cast<usize>(width) * height);
}

TileCell& TileLayer::getCell(i32 x, i32 y) {
    return m_cells[static_cast<usize>(y * m_width + x)];
}

const TileCell& TileLayer::getCell(i32 x, i32 y) const {
    return m_cells[static_cast<usize>(y * m_width + x)];
}

void TileLayer::resize(i32 width, i32 height) {
    m_width = width;
    m_height = height;
    m_cells.resize(static_cast<usize>(width) * height);
}

void Tilemap::setSize(i32 width, i32 height) {
    m_width = width;
    m_height = height;
    for (auto& layer : m_layers) {
        layer->resize(width, height);
    }
}

TileLayer& Tilemap::layer(usize index) {
    return *m_layers[index];
}

const TileLayer& Tilemap::layer(usize index) const {
    return *m_layers[index];
}

void Tilemap::addLayer(const std::string& name) {
    auto layer = std::make_unique<TileLayer>(name, m_width, m_height);
    m_layers.push_back(std::move(layer));
}

void Tilemap::removeLayer(usize index) {
    if (index < m_layers.size()) {
        m_layers.erase(m_layers.begin() + index);
    }
}

void Tilemap::moveLayer(usize from, usize to) {
    if (from >= m_layers.size() || to >= m_layers.size()) return;
    auto layer = std::move(m_layers[from]);
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(layer));
}

TilemapEditorPanel::TilemapEditorPanel() {
    m_tilemap.addLayer("Layer 1");
    m_tilemap.addLayer("Layer 2");
}

bool TilemapEditorPanel::hasNeighbor(i32 layerIdx, i32 x, i32 y, i32 tileID) const {
    if (layerIdx >= static_cast<i32>(m_tilemap.layerCount())) return false;

    const auto& layer = m_tilemap.layer(layerIdx);
    if (x < 0 || x >= layer.width() || y < 0 || y >= layer.height()) return false;

    return layer.getCell(x, y).tileID == tileID;
}

void TilemapEditorPanel::applyAutoTile(i32 layerIdx, i32 x, i32 y) {
    if (layerIdx < 0 || layerIdx >= static_cast<i32>(m_tilemap.layerCount())) return;
    if (x < 0 || x >= m_tilemap.width() || y < 0 || y >= m_tilemap.height()) return;

    auto& layer = m_tilemap.layer(layerIdx);
    i32 currentID = layer.getCell(x, y).tileID;
    if (currentID < 0) return;

    u8 neighborMask = 0;
    if (hasNeighbor(layerIdx, x, y - 1, currentID)) neighborMask |= 1 << 0;
    if (hasNeighbor(layerIdx, x + 1, y, currentID)) neighborMask |= 1 << 1;
    if (hasNeighbor(layerIdx, x, y + 1, currentID)) neighborMask |= 1 << 2;
    if (hasNeighbor(layerIdx, x - 1, y, currentID)) neighborMask |= 1 << 3;

    i32 ruleMatch = currentID;
    switch (neighborMask) {
        case 0: ruleMatch = currentID + 1; break;
        case 1: case 2: case 4: case 8: ruleMatch = currentID + 2; break;
        default: ruleMatch = currentID; break;
    }

    layer.getCell(x, y).tileID = ruleMatch;
}

void TilemapEditorPanel::paintTile(i32 layerIdx, i32 x, i32 y, i32 tileID) {
    if (layerIdx < 0 || layerIdx >= static_cast<i32>(m_tilemap.layerCount())) return;
    if (x < 0 || x >= m_tilemap.width() || y < 0 || y >= m_tilemap.height()) return;

    auto& layer = m_tilemap.layer(layerIdx);
    if (layer.isLocked()) return;

    for (i32 bx = 0; bx < m_brushSize; ++bx) {
        for (i32 by = 0; by < m_brushSize; ++by) {
            i32 px = x + bx;
            i32 py = y + by;
            if (px < m_tilemap.width() && py < m_tilemap.height()) {
                layer.getCell(px, py).tileID = tileID;

                if (m_autoTileEnabled) {
                    applyAutoTile(layerIdx, px, py);
                }
            }
        }
    }
}

void TilemapEditorPanel::floodFill(i32 layerIdx, i32 x, i32 y, i32 targetID, i32 replacementID) {
    if (layerIdx < 0 || layerIdx >= static_cast<i32>(m_tilemap.layerCount())) return;
    if (targetID == replacementID) return;

    auto& layer = m_tilemap.layer(layerIdx);
    if (layer.isLocked()) return;
    if (x < 0 || x >= m_tilemap.width() || y < 0 || y >= m_tilemap.height()) return;
    if (layer.getCell(x, y).tileID != targetID) return;

    std::vector<std::pair<i32, i32>> stack = {{x, y}};

    while (!stack.empty()) {
        auto [cx, cy] = stack.back();
        stack.pop_back();

        if (cx < 0 || cx >= m_tilemap.width() || cy < 0 || cy >= m_tilemap.height()) continue;
        if (layer.getCell(cx, cy).tileID != targetID) continue;

        layer.getCell(cx, cy).tileID = replacementID;

        stack.push_back({cx + 1, cy});
        stack.push_back({cx - 1, cy});
        stack.push_back({cx, cy + 1});
        stack.push_back({cx, cy - 1});
    }
}

void TilemapEditorPanel::eraseTile(i32 layerIdx, i32 x, i32 y) {
    if (layerIdx < 0 || layerIdx >= static_cast<i32>(m_tilemap.layerCount())) return;
    if (x < 0 || x >= m_tilemap.width() || y < 0 || y >= m_tilemap.height()) return;

    auto& layer = m_tilemap.layer(layerIdx);
    if (layer.isLocked()) return;

    for (i32 bx = 0; bx < m_brushSize; ++bx) {
        for (i32 by = 0; by < m_brushSize; ++by) {
            i32 px = x + bx;
            i32 py = y + by;
            if (px < m_tilemap.width() && py < m_tilemap.height()) {
                layer.getCell(px, py).tileID = -1;
            }
        }
    }
}

}

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void TilemapEditorPanel::render() {
    if (!m_open) return;

    if (ImGui::Begin("Tilemap Editor", &m_open)) {
        renderToolbar();
        ImGui::Separator();
        renderLayers();
        ImGui::Separator();
        renderPalette();
    }
    ImGui::End();
}

void TilemapEditorPanel::renderToolbar() {
    ImGui::Text("Tools:");

    const char* tools[] = { "Brush", "Bucket", "Eraser", "Picker" };
    i32 currentTool = static_cast<i32>(m_currentTool);
    if (ImGui::Combo("##tool", &currentTool, tools, 4)) {
        m_currentTool = static_cast<ToolMode>(currentTool);
    }

    ImGui::SameLine();
    ImGui::Text("Brush Size:");
    ImGui::SameLine();
    ImGui::SliderInt("##brushSize", &m_brushSize, 1, 8);

    ImGui::SameLine();
    ImGui::Checkbox("Auto-tile", &m_autoTileEnabled);

    ImGui::Separator();

    ImGui::Text("Tilemap: %d x %d", m_tilemap.width(), m_tilemap.height());
    ImGui::SameLine();
    if (ImGui::Button("Resize")) {
        m_tilemap.setSize(32, 32);
    }
}

void TilemapEditorPanel::renderLayers() {
    ImGui::Text("Layers");

    for (usize i = 0; i < m_tilemap.layerCount(); ++i) {
        auto& layer = m_tilemap.layer(i);
        bool isSelected = (static_cast<i32>(i) == m_currentLayer);

        std::string label = layer.isVisible() ? "[v] " : "[ ] ";
        label += layer.name();

        if (ImGui::Selectable(label.c_str(), isSelected)) {
            m_currentLayer = static_cast<i32>(i);
        }

        ImGui::SameLine();
        ImGui::Checkbox(("##visible" + std::to_string(i)).c_str(), &layer.visible());
        ImGui::SameLine();
        ImGui::Checkbox(("##locked" + std::to_string(i)).c_str(), &layer.locked());
    }

    if (ImGui::Button("+ Add Layer")) {
        m_tilemap.addLayer("Layer " + std::to_string(m_tilemap.layerCount() + 1));
    }

    ImGui::SameLine();
    if (ImGui::Button("- Remove Layer") && m_tilemap.layerCount() > 0) {
        m_tilemap.removeLayer(m_currentLayer);
        if (m_currentLayer >= static_cast<i32>(m_tilemap.layerCount())) {
            m_currentLayer = static_cast<i32>(m_tilemap.layerCount()) - 1;
        }
    }
}

void TilemapEditorPanel::renderPalette() {
    ImGui::Text("Tile Palette");

    static const i32 tilesPerRow = 8;
    static const i32 paletteSize = 64;

    for (i32 i = 0; i < paletteSize; ++i) {
        if (i % tilesPerRow != 0) ImGui::SameLine();

        bool isSelected = (i == m_selectedTileID);
        std::string label = std::to_string(i);

        if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_selectedTileID = i;
            } else {
                m_selectedTileID = i;
            }
        }

        if (i % tilesPerRow == tilesPerRow - 1) {
            ImGui::NewLine();
        }
    }

    ImGui::Separator();
    ImGui::Text("Selected Tile: %d", m_selectedTileID);
}

}

#endif