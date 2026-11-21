#include "terrain/TerrainBiome.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>

namespace Caffeine::Terrain {
namespace {

f32 ramp(f32 a, f32 b, f32 x) {
    if (b <= a) return x >= b ? 1.0f : 0.0f;  // a step
    const f32 t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

f32 BiomeTerm::weight(f32 value) const {
    return ramp(rise0, rise1, value) * (1.0f - ramp(fall1, fall0, value));
}

void BiomeTable::set(const BiomeRule& rule) {
    for (BiomeRule& existing : m_rules) {
        if (existing.classId == rule.classId) {
            existing = rule;
            return;
        }
    }
    m_rules.push_back(rule);
}

bool BiomeTable::remove(u32 classId) {
    const auto it = std::find_if(m_rules.begin(), m_rules.end(), [&](const BiomeRule& r) { return r.classId == classId; });
    if (it == m_rules.end()) return false;
    m_rules.erase(it);
    return true;
}

const BiomeRule* BiomeTable::find(u32 classId) const {
    for (const BiomeRule& rule : m_rules) {
        if (rule.classId == classId) return &rule;
    }
    return nullptr;
}

f32 BiomeTable::density(const TerrainMasks& masks, const TerrainFrame& frame, u32 classId, f32 x, f32 z) const {
    const BiomeRule* rule = find(classId);
    if (!rule) return 1.0f;
    f32 density = std::clamp(rule->base, 0.0f, 1.0f);
    for (const BiomeTerm& term : rule->terms) {
        f32 value = 0.0f;
        if (!masks.sample(frame, term.mask, x, z, value)) return 0.0f;  // an unknown mask places nothing
        density *= term.weight(value);
        if (density <= 0.0f) return 0.0f;
    }
    return density;
}

std::function<f32(u32, f32, f32)> BiomeTable::densityFunction(const TerrainMasks& masks, const TerrainFrame& frame) const {
    return [this, &masks, frame](u32 classId, f32 x, f32 z) { return density(masks, frame, classId, x, z); };
}

std::vector<std::string> BiomeTable::missingMasks(const TerrainMasks& masks) const {
    std::set<std::string> missing;
    for (const BiomeRule& rule : m_rules) {
        for (const BiomeTerm& term : rule.terms) {
            if (!masks.has(term.mask)) missing.insert(term.mask);
        }
    }
    return {missing.begin(), missing.end()};
}

std::string BiomeTable::toText() const {
    std::ostringstream out;
    char line[160];
    for (const BiomeRule& rule : m_rules) {
        std::snprintf(line, sizeof(line), "class %u base %g\n", rule.classId, static_cast<double>(rule.base));
        out << line;
        for (const BiomeTerm& t : rule.terms) {
            std::snprintf(line, sizeof(line), "  %s %g %g %g %g\n", t.mask.c_str(), static_cast<double>(t.rise0),
                          static_cast<double>(t.rise1), static_cast<double>(t.fall1), static_cast<double>(t.fall0));
            out << line;
        }
    }
    return out.str();
}

bool BiomeTable::fromText(const std::string& text, BiomeTable& out, std::string* error) {
    BiomeTable table;
    std::istringstream in(text);
    std::string raw;
    int lineNumber = 0;
    BiomeRule current;
    bool open = false;
    auto fail = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    auto close = [&]() {
        if (open) table.set(current);
        open = false;
    };
    while (std::getline(in, raw)) {
        ++lineNumber;
        const size_t hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        std::istringstream words(raw);
        std::string first;
        if (!(words >> first)) continue;
        if (first == "class") {
            close();
            long id = -1;
            std::string baseWord;
            f32 base = 1.0f;
            if (!(words >> id) || id < 0) return fail("a class needs a number: `class 2 base 0.9`");
            if (words >> baseWord) {
                if (baseWord != "base" || !(words >> base)) return fail("expected `base <number>` after the class");
            }
            current = BiomeRule{static_cast<u32>(id), base, {}};
            open = true;
        } else {
            if (!open) return fail("a term must come after a `class` line");
            BiomeTerm term;
            term.mask = first;
            if (!TerrainMasks::validName(term.mask)) return fail("`" + first + "` is not a mask name");
            if (!(words >> term.rise0 >> term.rise1 >> term.fall1 >> term.fall0)) {
                return fail("a term is `<mask> <rise0> <rise1> <fall1> <fall0>`");
            }
            if (term.rise0 > term.rise1 || term.rise1 > term.fall1 || term.fall1 > term.fall0) {
                return fail("the four numbers of a term must not decrease");
            }
            current.terms.push_back(term);
        }
        std::string extra;
        if (words >> extra) return fail("unexpected `" + extra + "`");
    }
    close();
    out = std::move(table);
    return true;
}

}  // namespace Caffeine::Terrain
