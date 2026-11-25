#include "terrain/vegetation/VegetationSpecies.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace Caffeine::Terrain {
namespace {

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

}  // namespace

bool VegetationSpecies::operator==(const VegetationSpecies& other) const {
    if (uid != other.uid || name != other.name || meshes != other.meshes || material != other.material || recipe != other.recipe ||
        densityPerHa != other.densityPerHa || radius != other.radius || priority != other.priority || minScale != other.minScale ||
        maxScale != other.maxScale || tilt != other.tilt || sinkM != other.sinkM || tintJitter != other.tintJitter ||
        tint[0] != other.tint[0] || tint[1] != other.tint[1] || tint[2] != other.tint[2] || maxDistance != other.maxDistance ||
        procedural != other.procedural || visible != other.visible || rules.size() != other.rules.size()) {
        return false;
    }
    for (usize i = 0; i < rules.size(); ++i) {
        const BiomeTerm &a = rules[i], &b = other.rules[i];
        if (a.mask != b.mask || a.rise0 != b.rise0 || a.rise1 != b.rise1 || a.fall1 != b.fall1 || a.fall0 != b.fall0) return false;
    }
    return true;
}

std::string vegetationSpeciesToText(const std::vector<VegetationSpecies>& list) {
    std::ostringstream out;
    out << "# caffeine terrain vegetation species 1\n";
    for (const VegetationSpecies& s : list) {
        out << "species " << s.name << "\n";
        out << "  uid " << s.uid << "\n";
        for (const std::string& mesh : s.meshes) out << "  mesh " << mesh << "\n";
        if (!s.material.empty()) out << "  material " << s.material << "\n";
        if (!s.recipe.empty()) out << "  recipe " << s.recipe << "\n";
        out << "  density " << num(s.densityPerHa) << "\n";
        out << "  radius " << num(s.radius) << "\n";
        out << "  priority " << s.priority << "\n";
        out << "  scale " << num(s.minScale) << " " << num(s.maxScale) << "\n";
        out << "  tilt " << num(s.tilt) << "\n";
        out << "  sink " << num(s.sinkM) << "\n";
        out << "  tint_jitter " << num(s.tintJitter) << "\n";
        out << "  tint " << num(s.tint[0]) << " " << num(s.tint[1]) << " " << num(s.tint[2]) << "\n";
        out << "  distance " << num(s.maxDistance) << "\n";
        out << "  procedural " << (s.procedural ? 1 : 0) << "\n";
        out << "  visible " << (s.visible ? 1 : 0) << "\n";
        for (const BiomeTerm& t : s.rules) {
            out << "  rule " << t.mask << " " << num(t.rise0) << " " << num(t.rise1) << " " << num(t.fall1) << " " << num(t.fall0) << "\n";
        }
    }
    return out.str();
}

bool vegetationSpeciesFromText(const std::string& text, std::vector<VegetationSpecies>& out, std::string* error) {
    std::vector<VegetationSpecies> list;
    std::istringstream in(text);
    std::string raw;
    int lineNumber = 0;
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
        if (key == "species") {
            if (list.size() >= kMaxVegetationSpecies) return fail("a terrain holds at most 256 species");
            VegetationSpecies s;
            s.name = rest.empty() ? "Species" : rest;
            list.push_back(s);
            continue;
        }
        if (list.empty()) return fail("`" + key + "` must come after a `species` line");
        VegetationSpecies& s = list.back();
        std::istringstream words(rest);
        auto number = [&](f32& value, const char* what) {
            if (!(words >> value)) return fail(std::string("`") + key + "` needs " + what);
            return true;
        };
        if (key == "uid") {
            u32 uid = 0;
            if (!(words >> uid)) return fail("`uid` needs a number");
            s.uid = uid;
        } else if (key == "mesh") {
            if (rest.empty()) return fail("`mesh` needs a project path");
            s.meshes.push_back(rest);
        } else if (key == "material") {
            s.material = rest;
        } else if (key == "recipe") {
            s.recipe = rest;
        } else if (key == "density") {
            if (!number(s.densityPerHa, "a number per hectare")) return false;
            s.densityPerHa = std::clamp(s.densityPerHa, 0.0f, 1.0e6f);
        } else if (key == "radius") {
            if (!number(s.radius, "a number of metres")) return false;
            s.radius = std::clamp(s.radius, 0.0f, 1000.0f);
        } else if (key == "priority") {
            if (!(words >> s.priority)) return fail("`priority` needs a number");
        } else if (key == "scale") {
            if (!(words >> s.minScale >> s.maxScale)) return fail("`scale` needs a minimum and a maximum");
            if (s.minScale <= 0.0f || s.maxScale < s.minScale) return fail("`scale` must be positive, the maximum not under the minimum");
        } else if (key == "tilt") {
            if (!number(s.tilt, "a number from 0 to 1")) return false;
            s.tilt = std::clamp(s.tilt, 0.0f, 1.0f);
        } else if (key == "sink") {
            if (!number(s.sinkM, "a number of metres")) return false;
        } else if (key == "tint_jitter") {
            if (!number(s.tintJitter, "a number from 0 to 1")) return false;
            s.tintJitter = std::clamp(s.tintJitter, 0.0f, 1.0f);
        } else if (key == "tint") {
            if (!(words >> s.tint[0] >> s.tint[1] >> s.tint[2])) return fail("`tint` needs three numbers");
        } else if (key == "distance") {
            if (!number(s.maxDistance, "a number of metres")) return false;
            s.maxDistance = std::max(s.maxDistance, 1.0f);
        } else if (key == "procedural" || key == "visible") {
            int value = 0;
            if (!(words >> value)) return fail("`" + key + "` needs 0 or 1");
            (key == "procedural" ? s.procedural : s.visible) = value != 0;
        } else if (key == "rule") {
            BiomeTerm term;
            if (!(words >> term.mask >> term.rise0 >> term.rise1 >> term.fall1 >> term.fall0)) {
                return fail("`rule` is `<mask> <from> <full from> <full to> <to>`");
            }
            if (term.rise0 > term.rise1 || term.rise1 > term.fall1 || term.fall1 > term.fall0) {
                return fail("the four numbers of a rule must not decrease");
            }
            s.rules.push_back(term);
        }  // an unknown key is skipped, so an older build opens a newer file
    }
    // A species written without a uid (by hand) takes the next free one, so ids stay unique.
    u32 next = 1;
    for (const VegetationSpecies& s : list) next = std::max(next, s.uid + 1);
    for (VegetationSpecies& s : list) {
        if (s.uid == 0) s.uid = next++;
    }
    for (usize i = 0; i < list.size(); ++i) {
        for (usize j = 0; j < i; ++j) {
            if (list[i].uid == list[j].uid) return fail("two species share the uid " + std::to_string(list[i].uid));
        }
    }
    out = std::move(list);
    return true;
}

}  // namespace Caffeine::Terrain
