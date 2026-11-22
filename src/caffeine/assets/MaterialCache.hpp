#pragma once

#include "assets/MaterialTypes.hpp"
#include "core/Types.hpp"

#include <chrono>
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

    /// Every draw asks for its material every frame. Canonicalising the path and reading the file's timestamp cost
    /// a few file-system calls, so the canonical key is remembered (per path and root) and the disk is looked at
    /// again only every kRecheckMs: a material file rewritten on disk shows up that late. A material that does
    /// not exist is remembered too. `publish` takes effect at once (it writes the entry itself).
    struct Fast {
        std::string key;
        u64 recheckAtMs = 0;
        bool negative = false;  ///< the last look found no usable material
    };

    std::unordered_map<std::string, Entry> m_cache;
    std::unordered_map<std::string, Fast> m_fast;
    static constexpr u64 kRecheckMs = 500;
    u64 m_revision = 1;
};

}  // namespace Caffeine::Assets
