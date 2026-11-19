#pragma once

#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace Caffeine::Script {

/// Classification data read from the top of a script. Header comment lines (`--`, `//` or `#`)
/// within the first 40 lines carry the tags:
///
///   -- @group Gameplay/Combat/Melee        groups and subgroups, separated by `/`
///   -- @name Sword swing                   display name (default: the file name)
///   -- @description Swings and hits.
///   -- @tags weapon, player
///
/// Without `@group` the group is the folder path under `scripts/` (`scripts/Gameplay/AI/x.lua`
/// is group `Gameplay/AI`), so a folder tree is already a group tree.
struct ScriptMeta {
    std::string path;  ///< project-relative, forward slashes
    std::string name;
    std::string description;
    std::vector<std::string> group;  ///< {"Gameplay", "Combat", "Melee"}; empty = Ungrouped
    std::vector<std::string> tags;
};

inline std::vector<std::string> splitScriptGroup(const std::string& text) {
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&] {
        const auto b = cur.find_first_not_of(" \t");
        if (b != std::string::npos) parts.push_back(cur.substr(b, cur.find_last_not_of(" \t") - b + 1));
        cur.clear();
    };
    for (char c : text) {
        if (c == '/' || c == '\\' || c == '>') flush();
        else cur += c;
    }
    flush();
    return parts;
}

inline ScriptMeta parseScriptMeta(const std::string& path, const std::string& source) {
    ScriptMeta meta;
    meta.path = path;
    std::istringstream in(source);
    std::string line;
    bool hasGroup = false;
    for (int i = 0; i < 40 && std::getline(in, line); ++i) {
        const auto b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        size_t p = b;
        if (line.compare(p, 2, "--") == 0 || line.compare(p, 2, "//") == 0) p += 2;
        else if (line[p] == '#') p += 1;
        else continue;
        p = line.find_first_not_of(" \t", p);
        if (p == std::string::npos || line[p] != '@') continue;
        const auto sp = line.find_first_of(" \t", p);
        const std::string key = line.substr(p + 1, sp == std::string::npos ? std::string::npos : sp - p - 1);
        std::string value = sp == std::string::npos ? "" : line.substr(sp + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        const auto vb = value.find_first_not_of(" \t");
        value = vb == std::string::npos ? "" : value.substr(vb);
        if (key == "group") { meta.group = splitScriptGroup(value); hasGroup = true; }
        else if (key == "name") meta.name = value;
        else if (key == "description") meta.description = value;
        else if (key == "tags") {
            std::string tag;
            std::istringstream ts(value);
            while (std::getline(ts, tag, ',')) {
                const auto tb = tag.find_first_not_of(" \t");
                if (tb != std::string::npos) meta.tags.push_back(tag.substr(tb, tag.find_last_not_of(" \t") - tb + 1));
            }
        }
    }
    if (!hasGroup) {
        // Folder tree under scripts/ is the default group.
        std::vector<std::string> folders = splitScriptGroup(path);
        if (!folders.empty()) folders.pop_back();  // file name
        if (!folders.empty() && folders.front() == "scripts") folders.erase(folders.begin());
        meta.group = std::move(folders);
    }
    if (meta.name.empty()) {
        meta.name = std::filesystem::path(path).stem().string();
    }
    return meta;
}

struct ScriptGroupNode {
    std::string name;
    std::map<std::string, std::unique_ptr<ScriptGroupNode>> children;  ///< sorted by name
    std::vector<ScriptMeta> scripts;                                   ///< sorted by name

    const ScriptGroupNode* find(const std::string& slashPath) const {
        const ScriptGroupNode* node = this;
        for (const auto& part : splitScriptGroup(slashPath)) {
            auto it = node->children.find(part);
            if (it == node->children.end()) return nullptr;
            node = it->second.get();
        }
        return node;
    }
    /// Scripts in this group and every subgroup.
    size_t totalScripts() const {
        size_t n = scripts.size();
        for (const auto& [_, c] : children) n += c->totalScripts();
        return n;
    }
};

/// Builds the group tree. Scripts without a group go to a top-level node named "Ungrouped".
inline ScriptGroupNode buildScriptTree(const std::vector<ScriptMeta>& metas) {
    ScriptGroupNode root;
    for (const auto& meta : metas) {
        ScriptGroupNode* node = &root;
        const std::vector<std::string> group =
            meta.group.empty() ? std::vector<std::string>{"Ungrouped"} : meta.group;
        for (const auto& part : group) {
            auto& child = node->children[part];
            if (!child) { child = std::make_unique<ScriptGroupNode>(); child->name = part; }
            node = child.get();
        }
        node->scripts.push_back(meta);
    }
    std::function<void(ScriptGroupNode&)> sortNode = [&](ScriptGroupNode& n) {
        std::sort(n.scripts.begin(), n.scripts.end(),
                  [](const ScriptMeta& a, const ScriptMeta& b) { return a.name < b.name; });
        for (auto& [_, c] : n.children) sortNode(*c);
    };
    sortNode(root);
    return root;
}

/// Scans `<projectRoot>` for `.lua` and `.dyd` files under `scripts/` and reads their headers.
inline std::vector<ScriptMeta> scanScripts(const std::string& projectRoot) {
    namespace fs = std::filesystem;
    std::vector<ScriptMeta> out;
    std::error_code ec;
    const fs::path root(projectRoot);
    const fs::path scripts = root / "scripts";
    if (!fs::exists(scripts, ec)) return out;
    for (fs::recursive_directory_iterator it(scripts, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = it->path().extension().string();
        if (ext != ".lua" && ext != ".dyd") continue;
        std::ifstream f(it->path());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string rel = fs::relative(it->path(), root, ec).generic_string();
        out.push_back(parseScriptMeta(rel, ss.str()));
    }
    std::sort(out.begin(), out.end(), [](const ScriptMeta& a, const ScriptMeta& b) { return a.path < b.path; });
    return out;
}

}  // namespace Caffeine::Script
