#pragma once

#include "assets/MaterialTypes.hpp"
#include "core/Types.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace Caffeine::Assets {

class MaterialCache {
public:
    static MaterialCache& instance();

    MaterialSurface resolve(const std::string& materialPath,
                            const std::string& projectRoot = "");
    void publish(const std::string& materialPath, const std::string& projectRoot,
                 const MaterialSurface& surface);
    void invalidate(const std::string& materialPath = {});
    u64 revision() const { return m_revision; }

private:
    MaterialCache() = default;

    struct Entry {
        MaterialSurface surface;
        std::filesystem::file_time_type stamp{};
        bool hasStamp = false;
    };

    std::string canonicalKey(const std::string& materialPath, const std::string& projectRoot) const;

    std::unordered_map<std::string, Entry> m_cache;
    u64 m_revision = 1;
};

}  // namespace Caffeine::Assets
