#pragma once

#include <functional>
#include <string>

namespace sol {
class state;
}

namespace Caffeine { namespace Input { class InputManager; } }
namespace Caffeine { namespace UI { class UISystem; } }
namespace Caffeine { namespace ECS { class World; } }

namespace Caffeine::Script {

class AssetTextLoader;

void registerRuntimeScriptBindings(
    sol::state& lua, ECS::World** worldPtr, Input::InputManager** inputPtr, UI::UISystem** uiPtr,
    AssetTextLoader* assetLoader, const std::string* searchRoot,
    const std::function<bool(const std::string& path, std::string* err)>& loadModuleFn);

}  // namespace Caffeine::Script
