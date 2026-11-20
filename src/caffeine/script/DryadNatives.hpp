#pragma once

#include "dryad/compiler/interpreter.h"
#include "dryad/runtime/value.h"

#include <functional>
#include <string>
#include <vector>

namespace Caffeine::Script {

/// Every Dryad native is registered through this, so the set of natives can be listed
/// (`DryadHost::nativeNames()`) and checked against the documented API.
inline std::vector<std::string>*& dryadNativeRecorder() {
    static thread_local std::vector<std::string>* recorder = nullptr;
    return recorder;
}

inline void defineNative(dryad::Interpreter& interp, const std::string& name,
                         std::function<dryad::Value(const std::vector<dryad::Value>&)> fn) {
    if (auto* rec = dryadNativeRecorder()) rec->push_back(name);
    interp.define_native(name, std::move(fn));
}

}  // namespace Caffeine::Script
