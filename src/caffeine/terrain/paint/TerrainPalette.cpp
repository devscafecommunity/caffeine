#include "terrain/paint/TerrainPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace Caffeine::Terrain {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    usize a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    return text.substr(a, b - a);
}

std::string num(f32 value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
    return buffer;
}

// The layers of the older four-layer splat map were named by their pictures' folders; plain names read better.
const char* const kLegacyNames[ECS::kTerrainSplatLayerCount] = {"Grass", "Rock", "Dirt", "Mud"};

}  // namespace

bool PaletteLayer::operator==(const PaletteLayer& other) const {
    return name == other.name && albedo == other.albedo && normal == other.normal && tileSizeM == other.tileSizeM &&
           rotationDegrees == other.rotationDegrees && tint[0] == other.tint[0] && tint[1] == other.tint[1] &&
           tint[2] == other.tint[2] && resolution == other.resolution && locked == other.locked;
}

i32 TerrainPalette::indexOf(const std::string& name) const {
    const std::string wanted = lower(name);
    for (usize i = 0; i < m_layers.size(); ++i) {
        if (lower(m_layers[i].name) == wanted) return static_cast<i32>(i);
    }
    return -1;
}

i32 TerrainPalette::add(const PaletteLayer& layer) {
    if (m_layers.size() >= kMaxPaletteLayers) return -1;
    PaletteLayer copy = layer;
    copy.resolution = resolutionClass(copy.resolution);
    copy.tileSizeM = std::clamp(copy.tileSizeM, 0.1f, 1000.0f);
    m_layers.push_back(copy);
    return static_cast<i32>(m_layers.size() - 1);
}

bool TerrainPalette::remove(u32 index) {
    if (index >= m_layers.size()) return false;
    m_layers.erase(m_layers.begin() + index);
    return true;
}

bool TerrainPalette::move(u32 from, u32 to) {
    if (from >= m_layers.size() || to >= m_layers.size()) return false;
    PaletteLayer layer = m_layers[from];
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, layer);
    return true;
}

u32 TerrainPalette::resolutionClass(u32 resolution) {
    // The nearest power of two on a log scale: the threshold between two classes is the square root of two.
    u32 candidate = kMinLayerResolution;
    while (candidate < kMaxLayerResolution &&
           static_cast<u64>(resolution) * resolution >= 2ull * candidate * candidate) {
        candidate *= 2;
    }
    return candidate;
}

std::string TerrainPalette::toText() const {
    std::ostringstream out;
    out << "# caffeine terrain palette 1\n";
    for (const PaletteLayer& layer : m_layers) {
        out << "layer " << layer.name << "\n";
        if (!layer.albedo.empty()) out << "  albedo " << layer.albedo << "\n";
        if (!layer.normal.empty()) out << "  normal " << layer.normal << "\n";
        out << "  tile " << num(layer.tileSizeM) << "\n";
        out << "  rotation " << num(layer.rotationDegrees) << "\n";
        out << "  tint " << num(layer.tint[0]) << " " << num(layer.tint[1]) << " " << num(layer.tint[2]) << "\n";
        out << "  resolution " << layer.resolution << "\n";
        if (layer.locked) out << "  locked 1\n";
    }
    return out.str();
}

bool TerrainPalette::fromText(const std::string& text, TerrainPalette& out, std::string* error) {
    TerrainPalette palette;
    std::istringstream in(text);
    std::string raw;
    int lineNumber = 0;
    bool open = false;
    auto fail = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    while (std::getline(in, raw)) {
        ++lineNumber;
        const usize hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        const std::string line = trim(raw);
        if (line.empty()) continue;
        const usize space = line.find_first_of(" \t");
        const std::string key = space == std::string::npos ? line : line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : trim(line.substr(space));
        if (key == "layer") {
            if (palette.size() >= kMaxPaletteLayers) return fail("a palette holds at most 64 layers");
            PaletteLayer layer;
            layer.name = rest.empty() ? "Layer" : rest;
            palette.m_layers.push_back(layer);
            open = true;
            continue;
        }
        if (!open) return fail("`" + key + "` must come after a `layer` line");
        PaletteLayer& layer = palette.m_layers.back();
        std::istringstream words(rest);
        if (key == "albedo") {
            layer.albedo = rest;
        } else if (key == "normal") {
            layer.normal = rest;
        } else if (key == "tile") {
            f32 value = 0.0f;
            if (!(words >> value) || value <= 0.0f) return fail("`tile` needs a positive number of metres");
            layer.tileSizeM = std::clamp(value, 0.1f, 1000.0f);
        } else if (key == "rotation") {
            if (!(words >> layer.rotationDegrees)) return fail("`rotation` needs a number of degrees");
        } else if (key == "tint") {
            if (!(words >> layer.tint[0] >> layer.tint[1] >> layer.tint[2])) return fail("`tint` needs three numbers");
        } else if (key == "resolution") {
            u32 value = 0;
            if (!(words >> value)) return fail("`resolution` needs a number of pixels");
            layer.resolution = resolutionClass(value);
        } else if (key == "locked") {
            int value = 0;
            if (!(words >> value)) return fail("`locked` needs 0 or 1");
            layer.locked = value != 0;
        }  // an unknown key is skipped, so an older build opens a newer file
    }
    out = std::move(palette);
    return true;
}

TerrainPalette TerrainPalette::fromLegacyLayers(const ECS::TerrainComponent& terrain) {
    TerrainPalette palette;
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        PaletteLayer layer;
        layer.name = kLegacyNames[i];
        layer.albedo = terrain.splatLayerPaths[i];
        layer.tileSizeM = terrain.splatTileSize;
        palette.m_layers.push_back(layer);
    }
    return palette;
}

}  // namespace Caffeine::Terrain
