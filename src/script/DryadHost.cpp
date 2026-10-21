#include "script/DryadHost.hpp"

#include "ecs/Components.hpp"
#include "ecs/World.hpp"

#include "dryad/common/utils.h"
#include "dryad/compiler/interpreter.h"
#include "dryad/runtime/value.h"

#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <utility>

namespace Caffeine::Script {

namespace {

double asNumber(const dryad::Value& value) {
    if (value.is_integer()) return static_cast<double>(value.as_integer());
    if (value.is_float()) return value.as_float();
    return 0.0;
}

ECS::Transform* transformOf(ECS::World* world, u32 entityId) {
    if (!world) return nullptr;
    ECS::Entity entity(entityId, world);
    if (!entity.isValid()) return nullptr;
    if (entity.has<ECS::Transform>()) return entity.get<ECS::Transform>();
    return &entity.getOrAdd<ECS::Transform>();
}

} // namespace

struct DryadHost::Impl {
    ECS::World* world = nullptr;
    std::unordered_map<std::string, std::unique_ptr<dryad::Interpreter>> sessions;

    void bindHost(dryad::Interpreter& interp) {
        ECS::World** worldPtr = &world;
        auto readAxis = [worldPtr](const std::vector<dryad::Value>& args, int axis) -> dryad::Value {
            if (args.empty()) return dryad::Value(0.0);
            const auto id = static_cast<u32>(asNumber(args[0]));
            if (auto* transform = transformOf(*worldPtr, id)) {
                const f32 v = axis == 0 ? transform->position.x
                                          : (axis == 1 ? transform->position.y : transform->position.z);
                return dryad::Value(static_cast<double>(v));
            }
            return dryad::Value(0.0);
        };

        interp.define_native("caffeine_set_position",
            [worldPtr](const std::vector<dryad::Value>& args) -> dryad::Value {
                if (args.size() < 4) return dryad::Value();
                const auto id = static_cast<u32>(asNumber(args[0]));
                if (auto* transform = transformOf(*worldPtr, id)) {
                    transform->position.x = static_cast<f32>(asNumber(args[1]));
                    transform->position.y = static_cast<f32>(asNumber(args[2]));
                    transform->position.z = static_cast<f32>(asNumber(args[3]));
                }
                return dryad::Value();
            });
        interp.define_native("caffeine_get_position_x",
            [readAxis](const std::vector<dryad::Value>& args) -> dryad::Value { return readAxis(args, 0); });
        interp.define_native("caffeine_get_position_y",
            [readAxis](const std::vector<dryad::Value>& args) -> dryad::Value { return readAxis(args, 1); });
        interp.define_native("caffeine_get_position_z",
            [readAxis](const std::vector<dryad::Value>& args) -> dryad::Value { return readAxis(args, 2); });
        interp.set_network_allowed(false);
        if (const char* stdlib = std::getenv("DRYAD_STDLIB_DIR")) {
            interp.set_stdlib_root(stdlib);
        }
#ifdef DRYAD_STDLIB_DIR
        else {
            interp.set_stdlib_root(DRYAD_STDLIB_DIR);
        }
#endif
    }
};

DryadHost::DryadHost() : m_impl(new Impl) {}

DryadHost::~DryadHost() { delete m_impl; }

void DryadHost::setWorld(ECS::World* world) { m_impl->world = world; }

bool DryadHost::isLoaded(const std::string& path) const {
    return m_impl->sessions.find(path) != m_impl->sessions.end();
}

bool DryadHost::load(const std::string& path, std::string* outError) {
    try {
        auto interp = std::make_unique<dryad::Interpreter>();
        m_impl->bindHost(*interp);
        interp->run_file(path);
        m_impl->sessions[path] = std::move(interp);
        return true;
    } catch (const dryad::DryadException& ex) {
        if (outError) *outError = ex.what();
        return false;
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return false;
    }
}

bool DryadHost::call(const std::string& path, const char* function, ECS::Entity entity, f32 dt,
                     std::string* outError) {
    auto it = m_impl->sessions.find(path);
    if (it == m_impl->sessions.end()) {
        if (outError) *outError = "script not loaded";
        return false;
    }
    dryad::Interpreter& interp = *it->second;
    if (!interp.global_env()->has(function)) return true;
    dryad::Value fn = interp.global_env()->get(function);
    if (!fn.is_function()) return true;

    std::vector<dryad::Value> args;
    args.emplace_back(static_cast<int64_t>(entity.id()));
    args.emplace_back(static_cast<double>(dt));
    try {
        interp.call_global(function, args);
        return true;
    } catch (const dryad::DryadException& ex) {
        if (outError) *outError = ex.what();
        return false;
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return false;
    }
}

bool isDryadScriptPath(const std::string& path) {
    return path.size() >= 4 && path.compare(path.size() - 4, 4, ".dyd") == 0;
}

} // namespace Caffeine::Script
