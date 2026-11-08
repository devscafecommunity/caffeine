#include "script/DryadHost.hpp"

#include "debug/LogSystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"

#include "dryad/common/utils.h"
#include "dryad/compiler/interpreter.h"
#include "dryad/runtime/value.h"

#include <cstdlib>
#include <filesystem>
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

struct PositionAccess {
    ECS::Position3D* p3 = nullptr;
    ECS::Transform* transform = nullptr;

    bool valid() const { return p3 != nullptr || transform != nullptr; }
};

/// 3D entities store their transform in Position3D; 2D entities use Transform.
/// Writing to both when both exist keeps the viewport and the 2D systems in sync.
PositionAccess positionOf(ECS::World* world, u32 entityId, bool create) {
    PositionAccess access;
    if (!world) return access;
    ECS::Entity entity(entityId, world);
    if (!entity.isValid()) return access;

    access.p3 = entity.get<ECS::Position3D>();
    access.transform = entity.get<ECS::Transform>();
    if (access.valid() || !create) return access;

    // A script attached to a mesh or a 3D-only entity must move the component
    // the 3D renderer reads. Plain/2D entities keep the historical Transform.
    if (entity.has<ECS::MeshFilterComponent>()) {
        access.p3 = &entity.getOrAdd<ECS::Position3D>();
    } else {
        access.transform = &entity.getOrAdd<ECS::Transform>();
    }
    return access;
}

Vec3 readPosition(ECS::World* world, u32 entityId) {
    const PositionAccess access = positionOf(world, entityId, false);
    if (access.p3) return access.p3->position;
    if (access.transform) return access.transform->position;
    return Vec3{0.0f, 0.0f, 0.0f};
}

void writePosition(ECS::World* world, u32 entityId, const Vec3& position) {
    const PositionAccess access = positionOf(world, entityId, true);
    if (access.p3) access.p3->position = position;
    if (access.transform) access.transform->position = position;
}

} // namespace

struct DryadHost::Impl {
    struct Session {
        std::unique_ptr<dryad::Interpreter> interp;
        std::filesystem::file_time_type mtime{};
    };

    ECS::World* world = nullptr;
    std::string searchRoot;
    std::unordered_map<std::string, Session> sessions;

    void bindHost(dryad::Interpreter& interp) {
        ECS::World** worldPtr = &world;
        auto readAxis = [worldPtr](const std::vector<dryad::Value>& args, int axis) -> dryad::Value {
            if (args.empty()) return dryad::Value(0.0);
            const auto id = static_cast<u32>(asNumber(args[0]));
            const Vec3 position = readPosition(*worldPtr, id);
            const f32 v = axis == 0 ? position.x : (axis == 1 ? position.y : position.z);
            return dryad::Value(static_cast<double>(v));
        };

        interp.define_native("caffeine_set_position",
            [worldPtr](const std::vector<dryad::Value>& args) -> dryad::Value {
                if (args.size() < 4) return dryad::Value();
                const auto id = static_cast<u32>(asNumber(args[0]));
                writePosition(*worldPtr, id,
                              Vec3(static_cast<f32>(asNumber(args[1])),
                                   static_cast<f32>(asNumber(args[2])),
                                   static_cast<f32>(asNumber(args[3]))));
                return dryad::Value();
            });
        interp.define_native("caffeine_translate",
            [worldPtr](const std::vector<dryad::Value>& args) -> dryad::Value {
                if (args.size() < 4) return dryad::Value();
                const auto id = static_cast<u32>(asNumber(args[0]));
                Vec3 position = readPosition(*worldPtr, id);
                position.x += static_cast<f32>(asNumber(args[1]));
                position.y += static_cast<f32>(asNumber(args[2]));
                position.z += static_cast<f32>(asNumber(args[3]));
                writePosition(*worldPtr, id, position);
                return dryad::Value();
            });
        interp.define_native("caffeine_get_position_x",
            [readAxis](const std::vector<dryad::Value>& args) -> dryad::Value { return readAxis(args, 0); });
        interp.define_native("caffeine_get_position_y",
            [readAxis](const std::vector<dryad::Value>& args) -> dryad::Value { return readAxis(args, 1); });
        interp.define_native("caffeine_get_position_z",
            [readAxis](const std::vector<dryad::Value>& args) -> dryad::Value { return readAxis(args, 2); });
        interp.define_native("caffeine_log",
            [](const std::vector<dryad::Value>& args) -> dryad::Value {
                if (!args.empty()) {
                    CF_INFO("Dryad", "%s", args[0].to_string().c_str());
                }
                return dryad::Value();
            });
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

void DryadHost::setSearchRoot(const std::string& root) { m_impl->searchRoot = root; }

std::string DryadHost::searchRoot() const { return m_impl->searchRoot; }

std::string DryadHost::resolvePath(const std::string& path) const {
    if (path.empty()) return path;
    const std::filesystem::path candidate(path);
    if (candidate.is_absolute() || m_impl->searchRoot.empty()) return candidate.string();

    std::error_code ec;
    const std::filesystem::path rooted =
        std::filesystem::path(m_impl->searchRoot) / candidate;
    if (std::filesystem::exists(rooted, ec) && !ec) return rooted.string();
    if (std::filesystem::exists(candidate, ec) && !ec) return candidate.string();
    return rooted.string();
}

bool DryadHost::isLoaded(const std::string& path) const {
    return m_impl->sessions.find(resolvePath(path)) != m_impl->sessions.end();
}

bool DryadHost::load(const std::string& path, std::string* outError) {
    if (path.empty()) {
        if (outError) *outError = "empty script path";
        return false;
    }

    const std::string resolved = resolvePath(path);
    std::error_code timeError;
    const auto mtime = std::filesystem::last_write_time(resolved, timeError);

    auto existing = m_impl->sessions.find(resolved);
    if (existing != m_impl->sessions.end() && !timeError &&
        existing->second.mtime == mtime) {
        return true;
    }

    try {
        auto interp = std::make_unique<dryad::Interpreter>();
        m_impl->bindHost(*interp);
        interp->run_file(resolved);
        Impl::Session& session = m_impl->sessions[resolved];
        session.interp = std::move(interp);
        session.mtime = mtime;
        return true;
    } catch (const dryad::DryadException& ex) {
        if (outError) *outError = ex.what();
        return false;
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return false;
    }
}

bool DryadHost::reloadIfChanged(const std::string& path, std::string* outError) {
    const std::string resolved = resolvePath(path);
    auto it = m_impl->sessions.find(resolved);
    if (it == m_impl->sessions.end()) return false;

    std::error_code timeError;
    const auto mtime = std::filesystem::last_write_time(resolved, timeError);
    if (timeError || mtime == it->second.mtime) return false;

    if (!load(path, outError)) {
        // Do not retry a broken script every frame; wait for the next edit.
        it = m_impl->sessions.find(resolved);
        if (it != m_impl->sessions.end()) it->second.mtime = mtime;
        return false;
    }
    return true;
}

bool DryadHost::call(const std::string& path, const char* function, ECS::Entity entity, f32 dt,
                     std::string* outError) {
    auto it = m_impl->sessions.find(resolvePath(path));
    if (it == m_impl->sessions.end()) {
        if (outError) *outError = "script not loaded";
        return false;
    }
    dryad::Interpreter& interp = *it->second.interp;
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
