#include "editor/EntityPresetTypes.hpp"

#include <algorithm>

namespace Caffeine::Editor {

bool EntityPresetWizardState::getBool(const std::string& id, bool fallback) const {
    for (const auto& field : fields) {
        if (field.id == id) return field.boolValue;
    }
    return fallback;
}

float EntityPresetWizardState::getFloat(const std::string& id, float fallback) const {
    for (const auto& field : fields) {
        if (field.id == id) return field.floatValue;
    }
    return fallback;
}

int EntityPresetWizardState::getInt(const std::string& id, int fallback) const {
    for (const auto& field : fields) {
        if (field.id == id) return field.intValue;
    }
    return fallback;
}

std::string EntityPresetWizardState::getString(const std::string& id, const std::string& fallback) const {
    for (const auto& field : fields) {
        if (field.id == id) return field.stringValue;
    }
    return fallback;
}

bool EntityPresetWizardState::usesCppScript() const {
    return getEnumLabel("script_type", "Lua") == "C++ Native";
}

std::string EntityPresetWizardState::getEnumLabel(const std::string& id, const std::string& fallback) const {
    for (const auto& field : fields) {
        if (field.id != id) continue;
        if (field.enumOptions.empty()) return fallback;
        const int idx = std::clamp(field.enumIndex, 0, static_cast<int>(field.enumOptions.size()) - 1);
        return field.enumOptions[static_cast<usize>(idx)];
    }
    return fallback;
}

}  // namespace Caffeine::Editor
