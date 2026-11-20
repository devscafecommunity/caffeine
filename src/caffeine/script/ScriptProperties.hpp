#pragma once

#include "script/ScriptEngine.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace Caffeine::Script {

/// Per-entity overrides for a script's exposed variables, stored as text:
/// `speed=4.5; god=true; label=hello` (separators: `;` or newline). A value is a boolean when it
/// is `true`/`false`, a number when it parses fully, and a string otherwise. Malformed entries
/// (no `=`, empty name) are skipped.
inline std::vector<ScriptEngine::ExposedVar> parseScriptProperties(const std::string& text) {
    std::vector<ScriptEngine::ExposedVar> out;
    auto trim = [](std::string s) {
        const char* ws = " \t\r";
        const auto b = s.find_first_not_of(ws);
        if (b == std::string::npos) return std::string();
        return s.substr(b, s.find_last_not_of(ws) - b + 1);
    };
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(";\n", start);
        if (end == std::string::npos) end = text.size();
        const std::string entry = text.substr(start, end - start);
        start = end + 1;
        const auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        ScriptEngine::ExposedVar var;
        var.name = trim(entry.substr(0, eq));
        const std::string value = trim(entry.substr(eq + 1));
        if (var.name.empty()) continue;
        if (value == "true" || value == "false") {
            var.kind = ScriptEngine::ExposedVar::Kind::Boolean;
            var.boolean = value == "true";
        } else {
            char* parsed = nullptr;
            const double number = std::strtod(value.c_str(), &parsed);
            if (!value.empty() && parsed && *parsed == '\0') {
                var.kind = ScriptEngine::ExposedVar::Kind::Number;
                var.number = number;
            } else {
                var.kind = ScriptEngine::ExposedVar::Kind::String;
                var.string = value;
            }
        }
        out.push_back(std::move(var));
    }
    return out;
}

}  // namespace Caffeine::Script
