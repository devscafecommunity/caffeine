#pragma once

#include "editor/EntityPresetTypes.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "scene/SceneComponents.hpp"

#ifdef CF_HAS_SCRIPTING
#include "script/ScriptTypes.hpp"
#endif

#include <fstream>

namespace Caffeine::Editor::EntityPresetUtils {

inline void parentEntity(ECS::World& world, ECS::Entity child, ECS::Entity parent) {
    if (!child.isValid() || !parent.isValid()) return;
    auto& pc = world.add<Scene::Parent>(child);
    pc.parent = parent;
    pc.dirty = true;
}

inline ECS::Entity make3DPrimitive(ECS::World& world, const char* name, ECS::MeshPrimitive primitive,
                                   const Vec3& position = Vec3(0, 0, 0),
                                   const Vec3& scale = Vec3(1, 1, 1)) {
    ECS::Entity e = world.create();
    setEntityName(world, e, name);
    world.add<ECS::Position3D>(e, ECS::Position3D{position});
    world.add<ECS::Rotation3D>(e);
    world.add<ECS::Scale3D>(e, ECS::Scale3D{scale});
    ECS::MeshFilterComponent mf;
    mf.primitive = primitive;
    world.add<ECS::MeshFilterComponent>(e, mf);
    world.add<ECS::MeshRendererComponent>(e);
    return e;
}

inline ECS::Entity makeCamera3D(ECS::World& world, const char* name, bool active = true) {
    ECS::Entity e = world.create();
    setEntityName(world, e, name);
    world.add<ECS::Camera3DComponent>(e);
    world.add<ECS::Position3D>(e);
    world.add<ECS::Rotation3D>(e);
    world.add<ECS::Scale3D>(e);
    if (active) {
        world.add<ECS::CameraActiveComponent>(e, ECS::CameraActiveComponent{false});
    }
    return e;
}

inline ECS::Entity makeCamera2D(ECS::World& world, const char* name, bool active = true) {
    ECS::Entity e = world.create();
    setEntityName(world, e, name);
    world.add<ECS::Transform>(e);
    world.add<ECS::Camera2DComponent>(e);
    if (active) {
        world.add<ECS::CameraActiveComponent>(e, ECS::CameraActiveComponent{true});
    }
    return e;
}

inline bool writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << content;
    return out.good();
}

inline bool copyFileIfExists(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (!std::filesystem::exists(from, ec)) return false;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

#ifdef CF_HAS_SCRIPTING
inline std::string makeCppScriptSource(const std::string& className,
                                       const std::string& extraIncludes,
                                       const std::string& classBody) {
    std::string source = "#pragma once\n\n#include \"script/CppScript.hpp\"\n"
                         "#include \"ecs/World.hpp\"\n"
                         "#include \"ecs/Components.hpp\"\n";
    if (!extraIncludes.empty()) source += extraIncludes;
    source += "\nclass " + className + " : public Caffeine::Script::CppScript {\npublic:\n";
    source += classBody;
    source += "\n};\n\nREGISTER_CPP_SCRIPT(" + className + ")\n";
    return source;
}

inline std::string attachLuaScript(ECS::World& world, ECS::Entity entity,
                                   const std::filesystem::path& projectRoot,
                                   const std::filesystem::path& relativePath,
                                   const std::string& source) {
    const auto absPath = std::filesystem::absolute(projectRoot / relativePath);
    if (!writeTextFile(absPath, source)) return {};
    auto& sc = world.add<Script::ScriptComponent>(entity);
    sc.scriptPath = absPath.string();
    return sc.scriptPath;
}

inline std::string attachCppScript(ECS::World& world, ECS::Entity entity,
                                   const std::filesystem::path& projectRoot,
                                   const std::filesystem::path& relativePath,
                                   const std::string& className,
                                   const std::string& source) {
    const auto absPath = std::filesystem::absolute(projectRoot / relativePath);
    if (!writeTextFile(absPath, source)) return {};
    auto& csc = world.add<Script::CppScriptComponent>(entity);
    csc.className = className;
    csc.instance.reset();
    csc.initialized = false;
    return absPath.string();
}

inline std::string attachPresetScript(ECS::World& world, ECS::Entity entity,
                                      const std::filesystem::path& projectRoot,
                                      const EntityPresetWizardState& wizard,
                                      EntityPresetSpawnResult& result,
                                      const std::filesystem::path& luaRelPath,
                                      const std::string& luaSource,
                                      const std::filesystem::path& cppRelPath,
                                      const std::string& cppClassName,
                                      const std::string& cppSource) {
    if (wizard.usesCppScript()) {
        const auto path = attachCppScript(world, entity, projectRoot, cppRelPath, cppClassName, cppSource);
        if (!path.empty()) {
            result.createdScriptPaths.push_back(path);
            result.needsRebuild = true;
        }
        return path;
    }

    const auto path = attachLuaScript(world, entity, projectRoot, luaRelPath, luaSource);
    if (!path.empty()) result.createdScriptPaths.push_back(path);
    return path;
}
#endif

inline EntityPresetWizardField makeBoolField(const char* id, const char* label, bool value,
                                             const char* hint = "") {
    EntityPresetWizardField f;
    f.id = id;
    f.label = label;
    f.hint = hint;
    f.type = EntityPresetWizardField::Type::Bool;
    f.boolValue = value;
    return f;
}

inline EntityPresetWizardField makeFloatField(const char* id, const char* label, float value,
                                              float min, float max, const char* hint = "") {
    EntityPresetWizardField f;
    f.id = id;
    f.label = label;
    f.hint = hint;
    f.type = EntityPresetWizardField::Type::Float;
    f.floatValue = value;
    f.floatMin = min;
    f.floatMax = max;
    return f;
}

inline EntityPresetWizardField makeIntField(const char* id, const char* label, int value,
                                            int min, int max, const char* hint = "") {
    EntityPresetWizardField f;
    f.id = id;
    f.label = label;
    f.hint = hint;
    f.type = EntityPresetWizardField::Type::Int;
    f.intValue = value;
    f.intMin = min;
    f.intMax = max;
    return f;
}

inline EntityPresetWizardField makeEnumField(const char* id, const char* label,
                                             const std::vector<std::string>& options, int index = 0,
                                             const char* hint = "") {
    EntityPresetWizardField f;
    f.id = id;
    f.label = label;
    f.hint = hint;
    f.type = EntityPresetWizardField::Type::Enum;
    f.enumOptions = options;
    f.enumIndex = index;
    return f;
}

}  // namespace Caffeine::Editor::EntityPresetUtils
