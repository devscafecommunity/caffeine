#pragma once

#include "input/InputManager.hpp"

#include <string>

namespace Caffeine::Script {

/// Name tables shared by every scripting language, so Lua and Dryad accept the same
/// strings ("Space", "Horizontal", "Jump"). Each returns false for an unknown name.
bool lookupScriptKey(const std::string& name, Input::Key& out);
bool lookupScriptAxis(const std::string& name, Input::Axis& out);
bool lookupScriptAction(const std::string& name, Input::Action& out);
/// 1 = left, 2 = middle, 3 = right.
bool lookupScriptMouseButton(unsigned index, Input::MouseButton& out);

}  // namespace Caffeine::Script
