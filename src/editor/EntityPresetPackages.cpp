#include "editor/EntityPresetPackages.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/EditorContext.hpp"
#include "editor/PrefabSystem.hpp"

#include <fstream>
#include <sstream>

namespace Caffeine::Editor {
namespace {

usize skipWhitespace(const std::string& json, usize pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t'))
        ++pos;
    return pos;
}

bool readQuotedString(const std::string& json, usize& pos, std::string& out) {
    pos = skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') return true;
        if (c == '\\' && pos < json.size()) {
            out.push_back(json[pos++]);
            continue;
        }
        out.push_back(c);
    }
    return false;
}

void skipJsonValue(const std::string& json, usize& pos) {
    pos = skipWhitespace(json, pos);
    if (pos >= json.size()) return;
    if (json[pos] == '"') {
        std::string tmp;
        readQuotedString(json, pos, tmp);
        return;
    }
    if (json[pos] == '{') {
        int depth = 0;
        while (pos < json.size()) {
            if (json[pos] == '{') ++depth;
            else if (json[pos] == '}') {
                --depth;
                ++pos;
                if (depth == 0) return;
                continue;
            }
            ++pos;
        }
        return;
    }
    if (json[pos] == '[') {
        int depth = 0;
        while (pos < json.size()) {
            if (json[pos] == '[') ++depth;
            else if (json[pos] == ']') {
                --depth;
                ++pos;
                if (depth == 0) return;
                continue;
            }
            ++pos;
        }
        return;
    }
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') ++pos;
}

bool readObjectStringField(const std::string& objectJson, const std::string& key, std::string& out) {
    usize pos = 0;
    while (pos < objectJson.size()) {
        pos = skipWhitespace(objectJson, pos);
        if (pos >= objectJson.size() || objectJson[pos] == '}') break;
        std::string foundKey;
        if (!readQuotedString(objectJson, pos, foundKey)) break;
        pos = skipWhitespace(objectJson, pos);
        if (pos >= objectJson.size() || objectJson[pos] != ':') break;
        ++pos;
        if (foundKey == key) return readQuotedString(objectJson, pos, out);
        skipJsonValue(objectJson, pos);
        pos = skipWhitespace(objectJson, pos);
        if (pos < objectJson.size() && objectJson[pos] == ',') ++pos;
    }
    return false;
}

bool extractTopLevelArray(const std::string& json, const std::string& key, std::string& outArray) {
    const std::string needle = "\"" + key + "\"";
    usize keyPos = json.find(needle);
    if (keyPos == std::string::npos) return false;
    usize pos = keyPos + needle.size();
    pos = skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != ':') return false;
    ++pos;
    pos = skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '[') return false;
    usize start = pos;
    int depth = 0;
    while (pos < json.size()) {
        if (json[pos] == '[') ++depth;
        else if (json[pos] == ']') {
            --depth;
            ++pos;
            if (depth == 0) {
                outArray = json.substr(start, pos - start);
                return true;
            }
            continue;
        }
        ++pos;
    }
    return false;
}

std::vector<std::string> splitTopLevelObjects(const std::string& arrayJson) {
    std::vector<std::string> objects;
    usize pos = 0;
    pos = skipWhitespace(arrayJson, pos);
    if (pos >= arrayJson.size() || arrayJson[pos] != '[') return objects;
    ++pos;
    while (pos < arrayJson.size()) {
        pos = skipWhitespace(arrayJson, pos);
        if (pos >= arrayJson.size()) break;
        if (arrayJson[pos] == ']') break;
        if (arrayJson[pos] != '{') break;
        usize start = pos;
        int depth = 0;
        while (pos < arrayJson.size()) {
            if (arrayJson[pos] == '{') ++depth;
            else if (arrayJson[pos] == '}') {
                --depth;
                ++pos;
                if (depth == 0) {
                    objects.push_back(arrayJson.substr(start, pos - start));
                    break;
                }
                continue;
            }
            ++pos;
        }
        pos = skipWhitespace(arrayJson, pos);
        if (pos < arrayJson.size() && arrayJson[pos] == ',') ++pos;
    }
    return objects;
}

}  // namespace

EntityPresetPackageLoadResult loadEntityPresetPackage(const std::filesystem::path& manifestPath) {
    EntityPresetPackageLoadResult result;
    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        result.error = "Could not open manifest: " + manifestPath.string();
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();
    readObjectStringField(json, "package_name", result.packageName);

    std::string presetsArray;
    if (!extractTopLevelArray(json, "presets", presetsArray)) {
        result.error = "Manifest has no presets array.";
        return result;
    }

    const auto packageRoot = manifestPath.parent_path();
    for (const auto& objectJson : splitTopLevelObjects(presetsArray)) {
        EntityPresetDescriptor preset;
        preset.builtIn = false;
        preset.packageRoot = packageRoot;
        preset.source = result.packageName.empty() ? packageRoot.filename().string() : result.packageName;

        if (!readObjectStringField(objectJson, "id", preset.id)) continue;
        if (!readObjectStringField(objectJson, "name", preset.displayName)) preset.displayName = preset.id;
        readObjectStringField(objectJson, "description", preset.description);
        if (!readObjectStringField(objectJson, "category", preset.category)) preset.category = "objects";
        readObjectStringField(objectJson, "default_entity_name", preset.defaultEntityName);
        if (preset.defaultEntityName.empty()) preset.defaultEntityName = preset.displayName;
        std::string prefabRel;
        if (readObjectStringField(objectJson, "prefab", prefabRel)) {
            preset.prefabPath = prefabRel;
        }

        preset.spawn = [preset](ECS::World& world, EditorContext& ctx,
                                const std::filesystem::path& projectRoot,
                                const EntityPresetWizardState& wizard) -> EntityPresetSpawnResult {
            EntityPresetSpawnResult result;
            if (!preset.prefabPath.empty()) {
                ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
                const auto prefabAbs = preset.packageRoot / preset.prefabPath;
                result.root = PrefabSystem::Instantiate(world, prefabAbs.string());
                if (!result.root.isValid()) {
                    ctx.endUndo(world);
                    result.error = "Failed to instantiate package prefab.";
                    return result;
                }
                setEntityName(world, result.root, wizard.entityName.c_str());
                ctx.selectEntity(result.root);
                ctx.endUndo(world);
                result.infoMessages.push_back("Instantiated package prefab.");
                return result;
            }

            result.error = "Package preset has no spawn handler.";
            return result;
        };

        result.presets.push_back(std::move(preset));
    }

    if (result.presets.empty() && result.error.empty()) {
        result.error = "No valid presets found in manifest.";
    }
    return result;
}

void scanEntityPresetPackages(const std::filesystem::path& rootDirectory,
                              const std::string& sourceLabel,
                              EntityPresetRegistry& registry) {
    if (rootDirectory.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(rootDirectory, ec)) return;

    for (auto& entry : std::filesystem::recursive_directory_iterator(rootDirectory, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() != "entity_presets.json") continue;

        auto loaded = loadEntityPresetPackage(entry.path());
        if (!loaded.error.empty() && loaded.presets.empty()) continue;

        for (auto& preset : loaded.presets) {
            if (preset.source.empty()) preset.source = sourceLabel;
            registry.registerPreset(std::move(preset));
        }
    }
}

}  // namespace Caffeine::Editor
