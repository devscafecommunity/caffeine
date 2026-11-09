#include "caffeine/ui/Icons.hpp"

namespace Caffeine::UIKit {

IconProvider& iconProviderRef() {
    static IconProvider provider;
    return provider;
}

void setIconProvider(IconProvider provider) {
    iconProviderRef() = std::move(provider);
}

void clearIconProvider() { iconProviderRef() = {}; }

bool hasIcon(const char* name) {
    if (!name || name[0] == '\0') return false;
    IconProvider& provider = iconProviderRef();
    return provider.exists && provider.exists(name);
}

ImTextureRef iconTexture(const char* name) {
    IconProvider& provider = iconProviderRef();
    if (!name || !provider.texture) return ImTextureRef();
    return provider.texture(name);
}

}  // namespace Caffeine::UIKit
