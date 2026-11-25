#include "world/WorldComposition.hpp"

#include "core/Hash.hpp"
#include "terrain/TerrainMasks.hpp"

#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace Caffeine::World {
namespace {

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 63) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool validRef(const std::string& ref) { return !ref.empty() && ref.find_first_of(" \t#") == std::string::npos; }

bool parseOnOff(const std::string& word, bool& out) {
    if (word == "on") {
        out = true;
        return true;
    }
    if (word == "off") {
        out = false;
        return true;
    }
    return false;
}

const char* opName(OverrideOp op) {
    switch (op) {
        case OverrideOp::Remove: return "remove";
        case OverrideOp::Move: return "move";
        case OverrideOp::Pin: return "pin";
    }
    return "remove";
}

/// Words a class property can be; a mask cannot be named like one.
bool isClassKey(const std::string& word) {
    static const char* const kKeys[] = {"ref",   "variants", "priority", "cell", "radius", "density", "scale",
                                        "tilt",  "sink",     "tint",     "base", "unique", "adapt"};
    for (const char* key : kKeys) {
        if (word == key) return true;
    }
    return false;
}

}  // namespace

const char* recipeKindName(RecipeKind kind) {
    switch (kind) {
        case RecipeKind::Terrain: return "terrain";
        case RecipeKind::Nature: return "nature";
        case RecipeKind::Water: return "water";
        case RecipeKind::Material: return "material";
        case RecipeKind::Texture: return "texture";
    }
    return "terrain";
}

u32 classSalt(std::string_view id) {
    u64 hash = 0xcbf29ce484222325ull;
    for (const char c : id) {
        hash ^= static_cast<u64>(static_cast<unsigned char>(c));
        hash *= 0x00000100000001b3ull;
    }
    hash = Core::splitMix64(hash);
    return static_cast<u32>(hash ^ (hash >> 32));
}

const WorldClass* WorldComposition::findClass(std::string_view id) const {
    for (const WorldClass& cls : classes) {
        if (cls.id == id) return &cls;
    }
    return nullptr;
}

std::vector<std::string> WorldComposition::validate() const {
    std::vector<std::string> errors;
    if (terrain.ref.empty()) errors.push_back("the composition has no `terrain`");
    if (terrain.sizeX < 0.0f || terrain.sizeZ < 0.0f) errors.push_back("the terrain size must not be negative");
    if (classes.empty()) errors.push_back("the composition has no `class`");

    std::map<u32, std::string> salts;
    for (const WorldClass& cls : classes) {
        const std::string name = "class `" + cls.id + "`";
        if (!validId(cls.id)) errors.push_back(name + ": the id must be letters, digits, '_', '-' or '.'");
        if (cls.ref.empty()) errors.push_back(name + " has no `ref`");
        if (cls.variants == 0) errors.push_back(name + ": `variants` must be at least 1");
        if (!(cls.cell > 0.0f)) errors.push_back(name + ": `cell` must be above 0");
        if (cls.radius < 0.0f) errors.push_back(name + ": `radius` must not be negative");
        if (cls.minScale > cls.maxScale) errors.push_back(name + ": the scale range decreases");
        if (!(cls.minScale > 0.0f)) errors.push_back(name + ": the scale must be above 0");
        for (const WorldAdapt& a : cls.adapt) {
            if (a.target != "size" && a.target != "lean" && a.target != "tint") {
                errors.push_back(name + ": `adapt " + a.target + "` is not size, lean or tint");
            }
        }
        const u32 salt = classSalt(cls.id);
        const auto [it, inserted] = salts.emplace(salt, cls.id);
        if (!inserted) {
            errors.push_back(it->second == cls.id ? name + " is defined twice"
                                                  : name + " collides with class `" + it->second +
                                                        "` (same placement salt): rename one");
        }
    }
    for (const WorldWater& w : water) {
        if (w.ref.empty()) errors.push_back("a `water` line has no recipe");
        if (w.follow.empty() && !w.fillBasins) {
            errors.push_back("water `" + w.ref + "` needs `follow <mask>` or `fill basins`");
        }
    }
    for (const WorldOverride& o : overrides) {
        const WorldClass* cls = findClass(o.classId);
        if (!cls) {
            errors.push_back(std::string("override ") + opName(o.op) + " names unknown class `" + o.classId + "`");
        } else if (o.op == OverrideOp::Pin && o.variant >= cls->variants) {
            errors.push_back("override pin " + o.classId + ": variant " + std::to_string(o.variant) +
                             " is outside the pool of " + std::to_string(cls->variants));
        }
    }
    return errors;
}

std::string WorldComposition::toText() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9);
    out << "seed " << seed << '\n';
    if (!terrain.ref.empty()) {
        out << "terrain " << terrain.ref;
        if (terrain.sizeX > 0.0f || terrain.sizeZ > 0.0f) out << " size " << terrain.sizeX << ' ' << terrain.sizeZ;
        out << '\n';
    }
    for (const WorldWater& w : water) {
        out << "water " << w.ref;
        if (!w.follow.empty()) out << " follow " << w.follow << " min " << w.min;
        if (w.fillBasins) out << " fill basins level " << w.level;
        out << '\n';
    }
    for (const WorldMaterial& m : materials) out << "material " << m.slot << ' ' << m.ref << '\n';
    for (const WorldClass& c : classes) {
        out << "class " << c.id << " ref " << c.ref << " variants " << c.variants << " priority " << c.priority
            << " cell " << c.cell << " radius " << c.radius << " density " << c.density << " scale " << c.minScale
            << ' ' << c.maxScale << " tilt " << c.tilt << " sink " << c.sink << " tint " << c.tint << " base "
            << c.base;
        if (c.unique) out << " unique on";
        out << '\n';
        for (const Terrain::BiomeTerm& t : c.where) {
            out << "  " << t.mask << ' ' << t.rise0 << ' ' << t.rise1 << ' ' << t.fall1 << ' ' << t.fall0 << '\n';
        }
        for (const WorldAdapt& a : c.adapt) out << "  adapt " << a.target << ' ' << a.mask << '\n';
    }
    for (const WorldOverride& o : overrides) {
        out << "override " << opName(o.op) << ' ' << o.classId << ' ' << o.cellX << ' ' << o.cellZ;
        if (o.op == OverrideOp::Move) out << ' ' << o.x << ' ' << o.z;
        if (o.op == OverrideOp::Pin) out << ' ' << o.variant;
        out << '\n';
    }
    return out.str();
}

bool WorldComposition::fromText(const std::string& text, WorldComposition& out, std::string* error) {
    WorldComposition result;
    std::istringstream in(text);
    std::string raw;
    int lineNumber = 0;
    WorldClass* current = nullptr;
    auto fail = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    while (std::getline(in, raw)) {
        ++lineNumber;
        const size_t hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        std::istringstream words(raw);
        words.imbue(std::locale::classic());
        std::string first;
        if (!(words >> first)) continue;
        const bool indented = raw[0] == ' ' || raw[0] == '\t';

        if (indented) {
            if (!current) return fail("an indented line must come after a `class` line");
            if (first == "adapt") {
                WorldAdapt a;
                if (!(words >> a.target >> a.mask)) return fail("`adapt` is `adapt <size|lean|tint> <mask>`");
                if (!Terrain::TerrainMasks::validName(a.mask)) return fail("`" + a.mask + "` is not a mask name");
                current->adapt.push_back(a);
            } else {
                if (isClassKey(first)) return fail("`" + first + "` is a property: put it on the `class` line");
                Terrain::BiomeTerm term;
                term.mask = first;
                if (!Terrain::TerrainMasks::validName(term.mask)) return fail("`" + first + "` is not a mask name");
                if (!(words >> term.rise0 >> term.rise1 >> term.fall1 >> term.fall0)) {
                    return fail("a term is `<mask> <rise0> <rise1> <fall1> <fall0>`");
                }
                if (term.rise0 > term.rise1 || term.rise1 > term.fall1 || term.fall1 > term.fall0) {
                    return fail("the four numbers of a term must not decrease");
                }
                current->where.push_back(term);
            }
        } else if (first == "seed") {
            current = nullptr;
            if (!(words >> result.seed)) return fail("`seed` takes one whole number");
        } else if (first == "terrain") {
            current = nullptr;
            if (!(words >> result.terrain.ref) || !validRef(result.terrain.ref)) {
                return fail("`terrain` needs a recipe path");
            }
            std::string key;
            if (words >> key) {
                if (key != "size" || !(words >> result.terrain.sizeX >> result.terrain.sizeZ)) {
                    return fail("expected `size <x> <z>` after the terrain recipe");
                }
            }
        } else if (first == "water") {
            current = nullptr;
            WorldWater w;
            if (!(words >> w.ref) || !validRef(w.ref)) return fail("`water` needs a recipe path");
            std::string key;
            while (words >> key) {
                if (key == "follow") {
                    if (!(words >> w.follow) || !Terrain::TerrainMasks::validName(w.follow)) {
                        return fail("`follow` needs a mask name");
                    }
                } else if (key == "min") {
                    if (!(words >> w.min)) return fail("`min` needs a number");
                } else if (key == "fill") {
                    std::string what;
                    if (!(words >> what) || what != "basins") return fail("expected `fill basins`");
                    w.fillBasins = true;
                } else if (key == "level") {
                    if (!(words >> w.level)) return fail("`level` needs a number");
                } else {
                    return fail("unknown water property `" + key + "`");
                }
            }
            result.water.push_back(w);
            continue;
        } else if (first == "material") {
            current = nullptr;
            WorldMaterial m;
            if (!(words >> m.slot >> m.ref) || !validRef(m.ref)) return fail("`material` is `material <slot> <recipe>`");
            result.materials.push_back(m);
        } else if (first == "class") {
            WorldClass cls;
            if (!(words >> cls.id)) return fail("a class needs an id: `class oak ref ...`");
            std::string key;
            while (words >> key) {
                bool ok = true;
                if (key == "ref") {
                    ok = static_cast<bool>(words >> cls.ref) && validRef(cls.ref);
                } else if (key == "variants") {
                    long v = 0;
                    ok = static_cast<bool>(words >> v) && v >= 0;
                    cls.variants = static_cast<u32>(v);
                } else if (key == "priority") {
                    ok = static_cast<bool>(words >> cls.priority);
                } else if (key == "cell") {
                    ok = static_cast<bool>(words >> cls.cell);
                } else if (key == "radius") {
                    ok = static_cast<bool>(words >> cls.radius);
                } else if (key == "density") {
                    ok = static_cast<bool>(words >> cls.density);
                } else if (key == "scale") {
                    ok = static_cast<bool>(words >> cls.minScale >> cls.maxScale);
                } else if (key == "tilt") {
                    ok = static_cast<bool>(words >> cls.tilt);
                } else if (key == "sink") {
                    ok = static_cast<bool>(words >> cls.sink);
                } else if (key == "tint") {
                    ok = static_cast<bool>(words >> cls.tint);
                } else if (key == "base") {
                    ok = static_cast<bool>(words >> cls.base);
                } else if (key == "unique") {
                    std::string word;
                    ok = static_cast<bool>(words >> word) && parseOnOff(word, cls.unique);
                } else {
                    return fail("unknown class property `" + key + "`");
                }
                if (!ok) return fail("bad value for `" + key + "`");
            }
            result.classes.push_back(cls);
            current = &result.classes.back();
            continue;
        } else if (first == "override") {
            current = nullptr;
            WorldOverride o;
            std::string op;
            if (!(words >> op >> o.classId >> o.cellX >> o.cellZ)) {
                return fail("`override` is `override <remove|move|pin> <class> <cell x> <cell z> ...`");
            }
            if (op == "remove") {
                o.op = OverrideOp::Remove;
            } else if (op == "move") {
                o.op = OverrideOp::Move;
                if (!(words >> o.x >> o.z)) return fail("`override move` ends with the new `<x> <z>`");
            } else if (op == "pin") {
                o.op = OverrideOp::Pin;
                long v = -1;
                if (!(words >> v) || v < 0) return fail("`override pin` ends with the variant number");
                o.variant = static_cast<u32>(v);
            } else {
                return fail("`" + op + "` is not remove, move or pin");
            }
            result.overrides.push_back(o);
        } else {
            return fail("unknown line `" + first + "`");
        }
        std::string extra;
        if (words >> extra) return fail("unexpected `" + extra + "`");
    }
    out = std::move(result);
    return true;
}

}  // namespace Caffeine::World
