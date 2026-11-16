#include "caffeine/ui/UI.hpp"
#include "editor/TilemapEditor.hpp"

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <cstdio>
#include <string>

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
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_open) return;

    Panel root("Tilemap Editor", "Paint tiles on layers", "assets", &m_open);
    UIContext ui;
    renderToolbar(root);
    root.emplace<Separator>();
    renderGrid(root);
    root.emplace<Separator>();
    renderLayers(root);
    root.emplace<Separator>();
    renderPalette(root);
    ui.draw(root);
#endif
}

void TilemapEditorPanel::renderToolbar(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("tile_tools");
    card.emplace<Label>("Tools", TextTone::Accent).setHeading(true);
    m_toolIndex = static_cast<int>(m_currentTool);
    auto& tools = card.emplace<SegmentedControl>(
        "tile_tool", &m_toolIndex,
        std::vector<std::string>{"Brush", "Bucket", "Eraser", "Picker"});
    tools.setOnChange([this](int value) {
        m_currentTool = static_cast<ToolMode>(value);
    });
    auto& grid = card.emplace<PropertyGrid>("tile_tool_grid");
    grid.typedRow<IntProperty>("Brush size", m_brushSize, 1, 8, 1.0f, "%d", true);
    grid.typedRow<BoolProperty>("Auto-tile", m_autoTileEnabled);

    auto& dimensions = card.emplace<Row>("tile_dimensions");
    dimensions.emplace<Label>("Tilemap: " + std::to_string(m_tilemap.width()) + " x " +
                                  std::to_string(m_tilemap.height()),
                              TextTone::Muted);
    dimensions.emplace<Button>("Resize").setOnClick([this] { m_tilemap.setSize(32, 32); });
#endif
}

void TilemapEditorPanel::renderGrid(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("tile_grid");
    card.emplace<Label>("Map", TextTone::Accent).setHeading(true);
    if (m_currentLayer < 0 ||
        m_currentLayer >= static_cast<i32>(m_tilemap.layerCount())) {
        card.emplace<Label>("No layer selected", TextTone::Muted);
        return;
    }
    auto& layer = m_tilemap.layer(m_currentLayer);
    if (!layer.isVisible()) {
        card.emplace<Label>("Layer is hidden", TextTone::Muted);
        return;
    }

    auto& canvas = card.emplace<Canvas>("tile_canvas");
    canvas.setHeight(static_cast<float>(layer.height()) * m_tilemap.tileSize());
    canvas.setOnDraw([this](UIContext&, ImDrawList* drawList, const ImVec2& origin,
                            const ImVec2& size) {
        if (m_currentLayer < 0 ||
            m_currentLayer >= static_cast<i32>(m_tilemap.layerCount())) {
            return;
        }
        auto& layer = m_tilemap.layer(m_currentLayer);
        const f32 tileSize = m_tilemap.tileSize();
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                IM_COL32(30, 30, 40, 255));

        for (i32 y = 0; y < layer.height(); ++y) {
            for (i32 x = 0; x < layer.width(); ++x) {
                const ImVec2 topLeft(origin.x + x * tileSize, origin.y + y * tileSize);
                const ImVec2 bottomRight(topLeft.x + tileSize, topLeft.y + tileSize);
                const TileCell& cell = layer.getCell(x, y);
                const u32 bg = (cell.tileID >= 0) ? IM_COL32(80, 120, 200, 200)
                                                  : IM_COL32(40, 40, 50, 200);
                drawList->AddRectFilled(topLeft, bottomRight, bg);
                drawList->AddRect(topLeft, bottomRight, IM_COL32(100, 100, 120, 180),
                                  0.0f, 0, 1.0f);
                if (cell.tileID >= 0) {
                    char label[8];
                    std::snprintf(label, sizeof(label), "%d", cell.tileID);
                    const ImVec2 textSize = ImGui::CalcTextSize(label);
                    drawList->AddText(
                        ImVec2(topLeft.x + (tileSize - textSize.x) * 0.5f,
                               topLeft.y + (tileSize - textSize.y) * 0.5f),
                        IM_COL32(255, 255, 255, 255), label);
                }
            }
        }

        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const i32 gridX = static_cast<i32>((mouse.x - origin.x) / tileSize);
            const i32 gridY = static_cast<i32>((mouse.y - origin.y) / tileSize);
            if (gridX >= 0 && gridX < layer.width() && gridY >= 0 &&
                gridY < layer.height() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                switch (m_currentTool) {
                    case ToolMode::Brush:
                        paintTile(m_currentLayer, gridX, gridY, m_selectedTileID);
                        break;
                    case ToolMode::Bucket:
                        floodFill(m_currentLayer, gridX, gridY,
                                  layer.getCell(gridX, gridY).tileID, m_selectedTileID);
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
    });
#endif
}

void TilemapEditorPanel::renderLayers(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("tile_layers");
    card.emplace<Label>("Layers", TextTone::Accent).setHeading(true);
    auto& list = card.emplace<ListBox>("tile_layer_list");
    list.setHeight(140.0f);
    for (usize i = 0; i < m_tilemap.layerCount(); ++i) {
        auto& layer = m_tilemap.layer(i);
        std::string label = layer.isVisible() ? "[v] " : "[ ] ";
        label += layer.name();
        auto& item = list.emplace<ListItem>(label, static_cast<i32>(i) == m_currentLayer);
        item.setId("layer_" + std::to_string(i));
        item.setOnClick([this, i] { m_currentLayer = static_cast<i32>(i); });
    }

    if (m_currentLayer >= 0 &&
        m_currentLayer < static_cast<i32>(m_tilemap.layerCount())) {
        auto& layer = m_tilemap.layer(m_currentLayer);
        auto& grid = card.emplace<PropertyGrid>("tile_layer_props");
        grid.typedRow<BoolProperty>("Visible", layer.visible());
        grid.typedRow<BoolProperty>("Locked", layer.locked());
    }

    auto& actions = card.emplace<Row>("tile_layer_actions");
    actions.emplace<Button>("+ Add Layer").setOnClick([this] {
        m_tilemap.addLayer("Layer " + std::to_string(m_tilemap.layerCount() + 1));
    });
    actions.emplace<Button>("- Remove Layer").setOnClick([this] {
        if (m_tilemap.layerCount() == 0) return;
        m_tilemap.removeLayer(static_cast<usize>(m_currentLayer));
        if (m_currentLayer >= static_cast<i32>(m_tilemap.layerCount())) {
            m_currentLayer = static_cast<i32>(m_tilemap.layerCount()) - 1;
        }
    });
#endif
}

void TilemapEditorPanel::renderPalette(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("tile_palette");
    card.emplace<Label>("Tile Palette", TextTone::Accent).setHeading(true);
    auto& pathRow = card.emplace<Row>("tileset_path_row");
    auto& path = pathRow.emplace<TextInput>(m_tilesetPathBuf, sizeof(m_tilesetPathBuf));
    path.setGrow(true);
    pathRow.emplace<Button>("Load").setOnClick([this] { m_showTilesetHelp = true; });
    if (m_showTilesetHelp) {
        card.emplace<Elements::Paragraph>(
            "Call loadTileset(path, sdlRenderer) from SceneEditor.");
    }

    if (m_tileset.isLoaded()) {
        const float display = static_cast<float>(m_tileDisplaySize);
        const int perRow = 8;
        auto& grid = card.emplace<Grid>(perRow);
        const ImTextureRef texture(
            reinterpret_cast<ImTextureID>(m_tileset.textureHandle));
        for (i32 i = 0; i < m_tileset.tileCount(); ++i) {
            const TileUV& uv = m_tileset.tiles[i];
            const bool selected = (i == m_selectedTileID);
            const ImVec4 background =
                selected ? ImVec4(0.3f, 0.6f, 1.0f, 0.5f) : ImVec4(0, 0, 0, 0);
            auto& button = grid.emplace<ImageButton>(
                texture, ImVec2(display, display), ImVec2(uv.u0, uv.v0),
                ImVec2(uv.u1, uv.v1), background);
            button.setId("tile_" + std::to_string(i));
            button.setOnClick([this, i] { m_selectedTileID = i; });
        }
    } else {
        auto& grid = card.emplace<Grid>(8);
        for (i32 i = 0; i < 64; ++i) {
            auto& button = grid.emplace<Button>(std::to_string(i),
                                                ButtonVariant::Segmented);
            button.setWidth(30.0f);
            button.setHeight(26.0f);
            button.setSelected(i == m_selectedTileID);
            button.setId("tile_fallback_" + std::to_string(i));
            button.setOnClick([this, i] { m_selectedTileID = i; });
        }
    }

    card.emplace<Label>("Selected: " + std::to_string(m_selectedTileID),
                        TextTone::Muted);
#endif
}

}

#endif