#include "editor/TilemapEditor.hpp"

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

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

bool TilemapEditorPanel::loadTileset(const std::string& path, void* renderer) {
#ifdef CF_HAS_SDL3
    if (m_tileset.isLoaded()) m_tileset.destroy();
    SDL_Renderer* r = static_cast<SDL_Renderer*>(renderer);
    SDL_Surface* surf = SDL_LoadBMP(path.c_str());
    if (!surf) return false;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (!tex) return false;
    float fw = 0, fh = 0;
    SDL_GetTextureSize(tex, &fw, &fh);
    m_tileset.path    = path;
    m_tileset.textureHandle = tex;
    m_tileset.textureW = static_cast<i32>(fw);
    m_tileset.textureH = static_cast<i32>(fh);
    m_tileset.tileWidth  = static_cast<i32>(m_tilemap.tileSize());
    m_tileset.tileHeight = static_cast<i32>(m_tilemap.tileSize());
    m_tileset.computeUVs();
    return true;
#else
    (void)path; (void)renderer;
    return false;
#endif
}

}

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void TilemapEditorPanel::render() {
    if (!m_open) return;

    if (ImGui::Begin("Tilemap Editor", &m_open)) {
        renderToolbar();
        ImGui::Separator();

        renderGrid();
        ImGui::Separator();

        renderLayers();
        ImGui::Separator();
        renderPalette();
    }
    ImGui::End();
}

void TilemapEditorPanel::renderGrid() {
    if (m_currentLayer < 0 || m_currentLayer >= static_cast<i32>(m_tilemap.layerCount())) {
        ImGui::TextDisabled("No layer selected");
        return;
    }

    auto& layer = m_tilemap.layer(m_currentLayer);
    if (!layer.isVisible()) {
        ImGui::TextDisabled("Layer is hidden");
        return;
    }

     f32 tileSize = m_tilemap.tileSize();
     ImVec2 canvasPos = ImGui::GetCursorScreenPos();
     f32 canvasWidth = ImGui::GetContentRegionAvail().x;
     [[maybe_unused]] f32 gridWidth = layer.width() * tileSize;
     f32 gridHeight = layer.height() * tileSize;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasWidth, canvasPos.y + gridHeight),
                            IM_COL32(30, 30, 40, 255));

    for (i32 y = 0; y < layer.height(); ++y) {
        for (i32 x = 0; x < layer.width(); ++x) {
            ImVec2 cellTopLeft(canvasPos.x + x * tileSize, canvasPos.y + y * tileSize);
            ImVec2 cellBottomRight(cellTopLeft.x + tileSize, cellTopLeft.y + tileSize);

            const TileCell& cell = layer.getCell(x, y);
            u32 bgColor = (cell.tileID >= 0) ? IM_COL32(80, 120, 200, 200) : IM_COL32(40, 40, 50, 200);
            u32 borderColor = IM_COL32(100, 100, 120, 180);

            drawList->AddRectFilled(cellTopLeft, cellBottomRight, bgColor);
            drawList->AddRect(cellTopLeft, cellBottomRight, borderColor, 0.0f, 0, 1.0f);

            if (cell.tileID >= 0) {
                char tileLabel[8];
                snprintf(tileLabel, sizeof(tileLabel), "%d", cell.tileID);
                ImVec2 textSize = ImGui::CalcTextSize(tileLabel);
                ImVec2 textPos(
                    cellTopLeft.x + (tileSize - textSize.x) * 0.5f,
                    cellTopLeft.y + (tileSize - textSize.y) * 0.5f
                );
                drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), tileLabel);
            }
        }
    }

    ImGui::Dummy(ImVec2(canvasWidth, gridHeight));

    if (ImGui::IsItemHovered()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        i32 gridX = static_cast<i32>((mousePos.x - canvasPos.x) / tileSize);
        i32 gridY = static_cast<i32>((mousePos.y - canvasPos.y) / tileSize);

        if (gridX >= 0 && gridX < layer.width() && gridY >= 0 && gridY < layer.height()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                switch (m_currentTool) {
                    case ToolMode::Brush:
                        paintTile(m_currentLayer, gridX, gridY, m_selectedTileID);
                        break;
                    case ToolMode::Bucket:
                        floodFill(m_currentLayer, gridX, gridY, layer.getCell(gridX, gridY).tileID, m_selectedTileID);
                        break;
                    case ToolMode::Eraser:
                        eraseTile(m_currentLayer, gridX, gridY);
                        break;
                    case ToolMode::Picker:
                        m_selectedTileID = layer.getCell(gridX, gridY).tileID;
                        break;
                }
            }
        }
    }
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

    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availW - 55.0f);
    ImGui::InputText("##tilesetPath", m_tilesetPathBuf, sizeof(m_tilesetPathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Load##ts")) {
        ImGui::OpenPopup("TilesetNote");
    }
    if (ImGui::BeginPopup("TilesetNote")) {
        ImGui::TextWrapped("Call loadTileset(\"%s\", sdlRenderer) from SceneEditor.", m_tilesetPathBuf);
        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (m_tileset.isLoaded()) {
        ImTextureID texId = reinterpret_cast<ImTextureID>(m_tileset.textureHandle);
        float disp = static_cast<float>(m_tileDisplaySize);
        i32 perRow = std::max(1, static_cast<i32>(ImGui::GetContentRegionAvail().x / (disp + 4.0f)));
        for (i32 i = 0; i < m_tileset.tileCount(); ++i) {
            if (i % perRow != 0) ImGui::SameLine();
            const TileUV& uv = m_tileset.tiles[i];
            bool sel = (i == m_selectedTileID);
            ImVec4 bg   = sel ? ImVec4(0.3f, 0.6f, 1.0f, 0.5f) : ImVec4(0,0,0,0);
            ImVec4 tint = ImVec4(1,1,1,1);
            ImGui::PushID(i);
            if (ImGui::ImageButton("##t", texId, ImVec2(disp, disp),
                                   ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1), bg, tint)) {
                m_selectedTileID = i;
            }
            ImGui::PopID();
        }
    } else {
        static const i32 perRow = 8;
        static const i32 total  = 64;
        for (i32 i = 0; i < total; ++i) {
            if (i % perRow != 0) ImGui::SameLine();
            bool sel = (i == m_selectedTileID);
            std::string lbl = std::to_string(i);
            if (ImGui::Selectable(lbl.c_str(), sel, 0, ImVec2(28, 28)))
                m_selectedTileID = i;
            if (i % perRow == perRow - 1) ImGui::NewLine();
        }
    }

    ImGui::Separator();
    ImGui::Text("Selected: %d", m_selectedTileID);
}

}

#endif