#include "terrain/edit/BrushPreset.hpp"

#include "assets/TextureMapGenerator.hpp"
#include "terrain/edit/BrushStamps.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace Caffeine::Terrain {
namespace {

struct ModeName {
    TerrainBrushMode mode;
    const char* name;
};
constexpr ModeName kModes[] = {
    {TerrainBrushMode::Raise, "raise"},           {TerrainBrushMode::Lower, "lower"},
    {TerrainBrushMode::Smooth, "smooth"},         {TerrainBrushMode::Flatten, "flatten"},
    {TerrainBrushMode::Noise, "noise"},           {TerrainBrushMode::Ramp, "ramp"},
    {TerrainBrushMode::Terrace, "terrace"},       {TerrainBrushMode::Pinch, "pinch"},
    {TerrainBrushMode::Bulge, "bulge"},           {TerrainBrushMode::StampAbsolute, "stamp_absolute"},
    {TerrainBrushMode::StampMax, "stamp_max"},    {TerrainBrushMode::StampMin, "stamp_min"},
    {TerrainBrushMode::Clone, "clone"},           {TerrainBrushMode::Smudge, "smudge"},
    {TerrainBrushMode::ErodeThermal, "erode_thermal"}, {TerrainBrushMode::ErodeHydraulic, "erode_hydraulic"},
};

const char* falloffName(FalloffCurve curve) {
    switch (curve) {
        case FalloffCurve::Smooth: return "smooth";
        case FalloffCurve::Linear: return "linear";
        case FalloffCurve::Sharp: return "sharp";
        case FalloffCurve::Constant: return "constant";
    }
    return "smooth";
}

bool falloffFromName(const std::string& name, FalloffCurve& out) {
    for (const FalloffCurve c : {FalloffCurve::Smooth, FalloffCurve::Linear, FalloffCurve::Sharp, FalloffCurve::Constant}) {
        if (name == falloffName(c)) {
            out = c;
            return true;
        }
    }
    return false;
}

const char* symmetryName(Symmetry s) {
    switch (s) {
        case Symmetry::None: return "none";
        case Symmetry::MirrorX: return "mirror_x";
        case Symmetry::MirrorZ: return "mirror_z";
        case Symmetry::MirrorBoth: return "mirror_both";
        case Symmetry::Radial: return "radial";
    }
    return "none";
}

bool symmetryFromName(const std::string& name, Symmetry& out) {
    for (const Symmetry s : {Symmetry::None, Symmetry::MirrorX, Symmetry::MirrorZ, Symmetry::MirrorBoth, Symmetry::Radial}) {
        if (name == symmetryName(s)) {
            out = s;
            return true;
        }
    }
    return false;
}

std::string num(f32 v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(v));
    return buffer;
}

std::string trim(std::string text) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

}  // namespace

const char* brushModeName(TerrainBrushMode mode) {
    for (const ModeName& m : kModes) {
        if (m.mode == mode) return m.name;
    }
    return "raise";
}

bool brushModeFromName(const std::string& name, TerrainBrushMode& out) {
    for (const ModeName& m : kModes) {
        if (name == m.name) {
            out = m.mode;
            return true;
        }
    }
    return false;
}

std::string brushToText(const BrushPreset& p) {
    const StrokeBrush& b = p.brush;
    std::ostringstream out;
    out << "# caffeine brush 1\n";
    out << "name " << p.name << "\n";
    out << "category " << p.category << "\n";
    out << "mode " << brushModeName(b.mode) << "\n";
    out << "radius " << num(b.radiusM) << "\n";
    out << "strength " << num(b.strength) << "\n";
    out << "spacing " << num(b.spacingM) << "\n";
    out << "falloff " << falloffName(b.falloff) << "\n";
    out << "rotation " << num(b.rotationDegrees) << "\n";
    out << "target_height " << num(b.targetHeightM) << "\n";
    out << "seed " << b.seed << "\n";
    switch (p.source) {
        case StampSource::None: out << "stamp none\n"; break;
        case StampSource::Procedural: out << "stamp procedural " << p.stamp << " " << p.stampSize << "\n"; break;
        case StampSource::Image: out << "stamp image " << p.stamp << " " << p.stampSize << "\n"; break;
    }
    out << "signed " << (b.signedStamp ? 1 : 0) << "\n";
    out << "step " << num(b.stepM) << "\n";
    out << "talus " << num(b.talusDegrees) << "\n";
    out << "iterations " << b.iterations << "\n";
    out << "droplets " << b.droplets << "\n";
    out << "offset " << num(b.offsetX) << " " << num(b.offsetZ) << "\n";
    out << "jitter " << num(b.positionJitter) << " " << num(b.rotationJitterDegrees) << " " << num(b.sizeJitter) << " "
        << num(b.opacityJitter) << "\n";
    out << "symmetry " << symmetryName(b.symmetry) << " " << b.radialCount << " " << num(b.symmetryCenterX) << " "
        << num(b.symmetryCenterZ) << "\n";
    out << "pressure " << num(b.pressureToStrength) << "\n";
    for (const BiomeTerm& t : b.protect) {
        out << "protect " << t.mask << " " << num(t.rise0) << " " << num(t.rise1) << " " << num(t.fall1) << " " << num(t.fall0)
            << "\n";
    }
    return out.str();
}

bool brushFromText(const std::string& text, BrushPreset& out, std::string* error) {
    BrushPreset p;
    std::istringstream in(text);
    std::string line;
    int lineNumber = 0;
    auto fail = [&](const std::string& message) {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNumber;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;
        std::istringstream words(line);
        std::string key;
        words >> key;
        std::string rest;
        std::getline(words, rest);
        rest = trim(rest);
        std::istringstream args(rest);
        StrokeBrush& b = p.brush;
        auto number = [&](f32& value) {
            return static_cast<bool>(args >> value);
        };
        if (key == "name") {
            p.name = rest.empty() ? "Brush" : rest;
        } else if (key == "category") {
            p.category = rest.empty() ? "Custom" : rest;
        } else if (key == "mode") {
            if (!brushModeFromName(rest, b.mode)) return fail("unknown mode \"" + rest + "\"");
        } else if (key == "radius") {
            if (!number(b.radiusM) || !(b.radiusM > 0.0f)) return fail("radius needs a positive number");
        } else if (key == "strength") {
            if (!number(b.strength)) return fail("strength needs a number");
        } else if (key == "spacing") {
            if (!number(b.spacingM)) return fail("spacing needs a number");
        } else if (key == "falloff") {
            if (!falloffFromName(rest, b.falloff)) return fail("unknown falloff \"" + rest + "\"");
        } else if (key == "rotation") {
            if (!number(b.rotationDegrees)) return fail("rotation needs a number");
        } else if (key == "target_height") {
            if (!number(b.targetHeightM)) return fail("target_height needs a number");
        } else if (key == "seed") {
            unsigned long long seed = 0;
            if (!(args >> seed)) return fail("seed needs a whole number");
            b.seed = seed;
        } else if (key == "stamp") {
            std::string kind;
            args >> kind;
            if (kind == "none") {
                p.source = StampSource::None;
                p.stamp.clear();
            } else if (kind == "procedural" || kind == "image") {
                std::string path;
                u32 size = 128;
                // The path is the rest of the line but for a trailing size.
                std::string tail;
                std::getline(args, tail);
                tail = trim(tail);
                const size_t space = tail.find_last_of(' ');
                if (space != std::string::npos && !tail.empty() &&
                    std::all_of(tail.begin() + static_cast<std::ptrdiff_t>(space) + 1, tail.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    size = static_cast<u32>(std::stoul(tail.substr(space + 1)));
                    path = trim(tail.substr(0, space));
                } else {
                    path = tail;
                }
                if (path.empty()) return fail("stamp needs a name or a path");
                p.source = kind == "procedural" ? StampSource::Procedural : StampSource::Image;
                p.stamp = path;
                p.stampSize = std::clamp(size, 4u, 1024u);
            } else {
                return fail("stamp is none, procedural or image");
            }
        } else if (key == "signed") {
            int v = 0;
            if (!(args >> v)) return fail("signed is 0 or 1");
            b.signedStamp = v != 0;
        } else if (key == "step") {
            if (!number(b.stepM)) return fail("step needs a number");
        } else if (key == "talus") {
            if (!number(b.talusDegrees)) return fail("talus needs a number");
        } else if (key == "iterations") {
            if (!(args >> b.iterations)) return fail("iterations needs a whole number");
        } else if (key == "droplets") {
            if (!(args >> b.droplets)) return fail("droplets needs a whole number");
        } else if (key == "offset") {
            if (!number(b.offsetX) || !number(b.offsetZ)) return fail("offset needs two numbers");
        } else if (key == "jitter") {
            if (!number(b.positionJitter) || !number(b.rotationJitterDegrees) || !number(b.sizeJitter) || !number(b.opacityJitter)) {
                return fail("jitter needs four numbers");
            }
        } else if (key == "symmetry") {
            std::string kind;
            args >> kind;
            if (!symmetryFromName(kind, b.symmetry)) return fail("unknown symmetry \"" + kind + "\"");
            if (!(args >> b.radialCount)) b.radialCount = 4;
            f32 cx = 0.0f, cz = 0.0f;
            if (number(cx)) b.symmetryCenterX = cx;
            if (number(cz)) b.symmetryCenterZ = cz;
        } else if (key == "pressure") {
            if (!number(b.pressureToStrength)) return fail("pressure needs a number");
        } else if (key == "protect") {
            BiomeTerm term;
            args >> term.mask;
            if (term.mask.empty() || !number(term.rise0) || !number(term.rise1) || !number(term.fall1) || !number(term.fall0)) {
                return fail("protect needs a mask and four numbers");
            }
            b.protect.push_back(term);
        }
        // Anything else is a key a newer version knows: skipped.
    }
    out = std::move(p);
    return true;
}

bool resolveBrushStamp(BrushPreset& preset, const std::function<std::string(const std::string&)>& absolutePath,
                       std::string* error) {
    preset.brush.stamp = {};
    switch (preset.source) {
        case StampSource::None: return true;
        case StampSource::Procedural:
            if (!generateProceduralStamp(preset.stamp, preset.stampSize, preset.brush.seed, preset.brush.stamp)) {
                if (error) *error = "unknown stamp \"" + preset.stamp + "\"";
                return false;
            }
            return true;
        case StampSource::Image: {
            const std::string file = absolutePath ? absolutePath(preset.stamp) : preset.stamp;
            Assets::TextureImage image;
            if (file.empty() || !Assets::loadTextureImage(file, image)) {
                if (error) *error = "cannot read the picture \"" + preset.stamp + "\"";
                return false;
            }
            const u32 size = std::clamp(preset.stampSize, 4u, 1024u);
            preset.brush.stamp.size = size;
            preset.brush.stamp.alpha.resize(static_cast<size_t>(size) * size);
            for (u32 z = 0; z < size; ++z) {
                for (u32 x = 0; x < size; ++x) {
                    // The picture, stretched to the square: bilinear on its grey.
                    const f32 fx = static_cast<f32>(x) / static_cast<f32>(size - 1) * static_cast<f32>(image.width - 1);
                    const f32 fz = static_cast<f32>(z) / static_cast<f32>(size - 1) * static_cast<f32>(image.height - 1);
                    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
                    const u32 x1 = std::min(x0 + 1, image.width - 1), z1 = std::min(z0 + 1, image.height - 1);
                    const f32 tx = fx - static_cast<f32>(x0), tz = fz - static_cast<f32>(z0);
                    auto grey = [&](u32 px, u32 pz) {
                        const u8* p = image.pixel(px, pz);
                        return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f * (static_cast<f32>(p[3]) / 255.0f);
                    };
                    const f32 top = grey(x0, z0) + (grey(x1, z0) - grey(x0, z0)) * tx;
                    const f32 bottom = grey(x0, z1) + (grey(x1, z1) - grey(x0, z1)) * tx;
                    preset.brush.stamp.alpha[static_cast<size_t>(z) * size + x] = std::clamp(top + (bottom - top) * tz, 0.0f, 1.0f);
                }
            }
            return true;
        }
    }
    return true;
}

const std::vector<BrushPreset>& builtinBrushes() {
    static const std::vector<BrushPreset> brushes = [] {
        std::vector<BrushPreset> out;
        for (const StampInfo& info : proceduralStamps()) {
            BrushPreset p;
            p.name = info.name;
            p.category = info.category;
            p.source = StampSource::Procedural;
            p.stamp = info.id;
            p.stampSize = 128;
            p.brush.mode = info.mode;
            p.brush.radiusM = info.radiusM;
            p.brush.strength = info.strength;
            p.brush.spacingM = info.radiusM * info.spacingRatio;
            p.brush.signedStamp = info.signedStamp;
            p.brush.falloff = FalloffCurve::Constant;  // the stamp is the shape
            out.push_back(p);
        }
        auto tool = [&](const char* name, TerrainBrushMode mode, f32 radius, f32 strength, auto&& adjust) {
            BrushPreset p;
            p.name = name;
            p.category = "Tools";
            p.brush.mode = mode;
            p.brush.radiusM = radius;
            p.brush.strength = strength;
            adjust(p.brush);
            out.push_back(p);
        };
        auto none = [](StrokeBrush&) {};
        tool("Raise", TerrainBrushMode::Raise, 10.0f, 0.5f, none);
        tool("Lower", TerrainBrushMode::Lower, 10.0f, 0.5f, none);
        tool("Smooth", TerrainBrushMode::Smooth, 12.0f, 0.5f, none);
        tool("Flatten", TerrainBrushMode::Flatten, 12.0f, 0.6f, none);
        tool("Noise", TerrainBrushMode::Noise, 10.0f, 0.4f, none);
        tool("Ramp", TerrainBrushMode::Ramp, 8.0f, 1.0f, [](StrokeBrush& b) { b.falloff = FalloffCurve::Linear; });
        tool("Terrace", TerrainBrushMode::Terrace, 16.0f, 0.8f, [](StrokeBrush& b) { b.stepM = 2.0f; });
        tool("Pinch", TerrainBrushMode::Pinch, 14.0f, 0.5f, none);
        tool("Bulge", TerrainBrushMode::Bulge, 14.0f, 0.5f, none);
        tool("Stamp absolute", TerrainBrushMode::StampAbsolute, 10.0f, 3.0f, none);
        tool("Stamp max", TerrainBrushMode::StampMax, 10.0f, 3.0f, none);
        tool("Stamp min", TerrainBrushMode::StampMin, 10.0f, 3.0f, none);
        tool("Clone", TerrainBrushMode::Clone, 12.0f, 0.8f, [](StrokeBrush& b) { b.offsetX = 30.0f; });
        tool("Smudge", TerrainBrushMode::Smudge, 12.0f, 0.6f, none);
        tool("Thermal erosion", TerrainBrushMode::ErodeThermal, 20.0f, 0.6f, none);
        tool("Hydraulic erosion", TerrainBrushMode::ErodeHydraulic, 20.0f, 0.6f, none);
        return out;
    }();
    return brushes;
}

}  // namespace Caffeine::Terrain
