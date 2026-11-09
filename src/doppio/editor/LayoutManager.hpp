#pragma once
#include "editor/LayoutProfile.hpp"
#include <filesystem>
#include <vector>
#include <optional>

namespace Caffeine::Editor {

// ============================================================================
// LayoutManager — manages saving and loading of layout profiles.
// ============================================================================
class LayoutManager {
public:
    LayoutManager();
    ~LayoutManager() = default;

    // Load all saved profiles from disk
    bool loadProfiles();

    // Save a profile to disk
    bool saveProfile(const LayoutProfile& profile);

    // Delete a saved profile
    bool deleteProfile(const std::string& name);

    // Get all available profiles
    const std::vector<LayoutProfile>& profiles() const { return m_profiles; }

    // Get a profile by name
    std::optional<LayoutProfile> getProfile(const std::string& name) const;

    // Apply a profile (returns false if not found)
    bool applyProfile(const std::string& name);

    // Get the current active profile
    const LayoutProfile& currentProfile() const { return m_currentProfile; }

    // Update current profile
    void updateCurrentProfile(const LayoutProfile& profile) { m_currentProfile = profile; }

private:
    std::vector<LayoutProfile> m_profiles;
    LayoutProfile m_currentProfile;

    std::filesystem::path getProfilesDirectory() const;
    std::filesystem::path getProfilePath(const std::string& name) const;

    // Serialization helpers
    std::string serializeProfile(const LayoutProfile& profile) const;
    std::optional<LayoutProfile> deserializeProfile(const std::string& json) const;
};

} // namespace Caffeine::Editor
