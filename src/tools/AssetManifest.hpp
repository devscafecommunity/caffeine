#pragma once

#include "tools/PipelineTypes.hpp"
#include "core/io/CafTypes.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <cstdio>
#include <algorithm>
#include <cstring>

namespace Caffeine::Tools {
using namespace Caffeine;

class AssetManifest {
public:
    void addEntry(AssetManifestEntry entry) {
        m_entries.push_back(entry);
    }
    
    void removeEntry(std::string_view id) {
        auto it = std::remove_if(m_entries.begin(), m_entries.end(),
            [id](const AssetManifestEntry& e) { return e.id == id; });
        m_entries.erase(it, m_entries.end());
    }
    
    const AssetManifestEntry* find(std::string_view id) const {
        for (const auto& entry : m_entries) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    }
    
    const std::vector<AssetManifestEntry>& entries() const { 
        return m_entries; 
    }
    
    usize entryCount() const { 
        return m_entries.size(); 
    }

    bool save(std::string_view manifestPath) const {
        FILE* f = std::fopen(manifestPath.data(), "w");
        if (!f) return false;
        
        std::fprintf(f, "{\n");
        std::fprintf(f, "  \"version\": 1,\n");
        std::fprintf(f, "  \"assets\": [\n");
        
        for (usize i = 0; i < m_entries.size(); ++i) {
            const auto& e = m_entries[i];
            std::fprintf(f, "    {\n");
            std::fprintf(f, "      \"id\": \"%s\",\n", e.id.c_str());
            std::fprintf(f, "      \"path\": \"%s\",\n", e.path.c_str());
            std::fprintf(f, "      \"type\": \"%s\",\n", assetTypeName(e.type));
            std::fprintf(f, "      \"sizeBytes\": %llu,\n", static_cast<unsigned long long>(e.sizeBytes));
            std::fprintf(f, "      \"crc32\": %u\n", e.crc32);
            if (i < m_entries.size() - 1) {
                std::fprintf(f, "    },\n");
            } else {
                std::fprintf(f, "    }\n");
            }
        }
        
        std::fprintf(f, "  ]\n");
        std::fprintf(f, "}\n");
        
        std::fclose(f);
        return true;
    }

    bool load(std::string_view manifestPath) {
        FILE* f = std::fopen(manifestPath.data(), "r");
        if (!f) return false;
        
        m_entries.clear();
        
        char line[2048];
        AssetManifestEntry currentEntry;
        bool inEntry = false;
        int depth = 0;
        
        while (std::fgets(line, sizeof(line), f)) {
            const char* p = line;
            
            while (*p == ' ' || *p == '\t') ++p;
            
            if (*p == '{') {
                ++depth;
                if (depth == 2) {
                    inEntry = true;
                    currentEntry = AssetManifestEntry();
                }
            }
            else if (*p == '}') {
                if (depth == 2 && inEntry) {
                    inEntry = false;
                    m_entries.push_back(currentEntry);
                }
                --depth;
            }
            else if (std::strstr(p, "\"id\":")) {
                const char* val = std::strchr(p, ':');
                if (val) {
                    ++val;
                    while (*val == ' ' || *val == '\t') ++val;
                    if (*val == '"') {
                        ++val;
                        const char* end = std::strchr(val, '"');
                        if (end) {
                            currentEntry.id.assign(val, end - val);
                        }
                    }
                }
            }
            else if (std::strstr(p, "\"path\":")) {
                const char* val = std::strchr(p, ':');
                if (val) {
                    ++val;
                    while (*val == ' ' || *val == '\t') ++val;
                    if (*val == '"') {
                        ++val;
                        const char* end = std::strchr(val, '"');
                        if (end) {
                            currentEntry.path.assign(val, end - val);
                        }
                    }
                }
            }
            else if (std::strstr(p, "\"type\":")) {
                const char* val = std::strchr(p, ':');
                if (val) {
                    ++val;
                    while (*val == ' ' || *val == '\t') ++val;
                    if (*val == '"') {
                        ++val;
                        const char* end = std::strchr(val, '"');
                        if (end) {
                            std::string typeName(val, end - val);
                            currentEntry.type = assetTypeFromName(typeName.c_str());
                        }
                    }
                }
            }
            else if (std::strstr(p, "\"sizeBytes\":")) {
                unsigned long long val;
                if (std::sscanf(p, " \"sizeBytes\": %llu", &val) == 1) {
                    currentEntry.sizeBytes = val;
                }
            }
            else if (std::strstr(p, "\"crc32\":")) {
                unsigned int val;
                if (std::sscanf(p, " \"crc32\": %u", &val) == 1) {
                    currentEntry.crc32 = val;
                }
            }
        }
        
        std::fclose(f);
        return true;
    }

private:
    std::vector<AssetManifestEntry> m_entries;

    static const char* assetTypeName(AssetType t) {
        switch (t) {
            case AssetType::Unknown:   return "Unknown";
            case AssetType::Texture:   return "Texture";
            case AssetType::Audio:     return "Audio";
            case AssetType::Mesh:      return "Mesh";
            case AssetType::Prefab:    return "Prefab";
            case AssetType::Scene:     return "Scene";
            case AssetType::Shader:    return "Shader";
            case AssetType::Animation: return "Animation";
            case AssetType::Font:      return "Font";
            default:                   return "Unknown";
        }
    }
    
    static AssetType assetTypeFromName(const char* name) {
        if (std::strcmp(name, "Texture") == 0)   return AssetType::Texture;
        if (std::strcmp(name, "Audio") == 0)     return AssetType::Audio;
        if (std::strcmp(name, "Mesh") == 0)      return AssetType::Mesh;
        if (std::strcmp(name, "Prefab") == 0)    return AssetType::Prefab;
        if (std::strcmp(name, "Scene") == 0)     return AssetType::Scene;
        if (std::strcmp(name, "Shader") == 0)    return AssetType::Shader;
        if (std::strcmp(name, "Animation") == 0) return AssetType::Animation;
        if (std::strcmp(name, "Font") == 0)      return AssetType::Font;
        return AssetType::Unknown;
    }
};

} // namespace Caffeine::Tools
