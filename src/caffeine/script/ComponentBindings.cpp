// `caffeine.component`: generic, reflection-driven access to every component the engine can
// describe (see scene/ComponentReflection). Whatever is registered there is scriptable here.

#include "ecs/World.hpp"
#include "scene/ComponentAccess.hpp"
#include "scene/ComponentReflection.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {
namespace {

namespace R = Caffeine::Scene::Reflection;
namespace A = Caffeine::Scene::Access;

const char* kindName(R::FieldKind kind) {
    switch (kind) {
        case R::FieldKind::F32: return "number";
        case R::FieldKind::Bool: return "boolean";
        case R::FieldKind::I32: return "integer";
        case R::FieldKind::U32: return "unsigned";
        case R::FieldKind::Vec2: return "vec2";
        case R::FieldKind::Vec3: return "vec3";
        case R::FieldKind::Vec4: return "vec4";
        case R::FieldKind::String: return "string";
        case R::FieldKind::Enum: return "enum";
        case R::FieldKind::Color: return "color";
    }
    return "?";
}

[[noreturn]] void raise(const std::string& message) {
    throw sol::error("caffeine.component: " + message);
}

sol::object toLua(sol::state_view lua, const R::FieldDesc& field, const R::FieldValue& v) {
    switch (field.kind) {
        case R::FieldKind::F32: return sol::make_object(lua, v.vec[0]);
        case R::FieldKind::Bool: return sol::make_object(lua, v.boolean != 0);
        case R::FieldKind::I32:
        case R::FieldKind::U32: return sol::make_object(lua, v.integer);
        case R::FieldKind::Enum: return sol::make_object(lua, A::enumLabel(field, v.integer));
        case R::FieldKind::String: return sol::make_object(lua, std::string(v.text));
        case R::FieldKind::Vec2: {
            sol::table t = lua.create_table();
            t["x"] = v.vec[0];
            t["y"] = v.vec[1];
            return t;
        }
        case R::FieldKind::Vec3: {
            sol::table t = lua.create_table();
            t["x"] = v.vec[0];
            t["y"] = v.vec[1];
            t["z"] = v.vec[2];
            return t;
        }
        case R::FieldKind::Vec4:
        case R::FieldKind::Color: {
            sol::table t = lua.create_table();
            t["x"] = v.vec[0];
            t["y"] = v.vec[1];
            t["z"] = v.vec[2];
            t["w"] = v.vec[3];
            t["r"] = v.vec[0];
            t["g"] = v.vec[1];
            t["b"] = v.vec[2];
            t["a"] = v.vec[3];
            return t;
        }
    }
    return sol::nil;
}

float part(const sol::table& t, const char* named, const char* alt, int index, float fallback) {
    if (sol::optional<float> v = t[named]) return *v;
    if (alt) {
        if (sol::optional<float> v = t[alt]) return *v;
    }
    if (sol::optional<float> v = t[index + 1]) return *v;
    return fallback;
}

/// Applies a Lua value to `value`; throws a script error when the type does not fit.
void fromLua(const R::FieldDesc& field, const sol::object& object, R::FieldValue& value) {
    auto bad = [&](const char* expected) {
        raise(std::string("field '") + field.name + "' expects " + expected + " (" +
              kindName(field.kind) + ")");
    };
    switch (field.kind) {
        case R::FieldKind::F32:
            if (object.get_type() != sol::type::number) bad("a number");
            value.vec[0] = object.as<float>();
            return;
        case R::FieldKind::Bool:
            if (object.get_type() != sol::type::boolean) bad("true or false");
            value.boolean = object.as<bool>() ? 1 : 0;
            return;
        case R::FieldKind::I32:
        case R::FieldKind::U32:
            if (object.get_type() != sol::type::number) bad("a number");
            value.integer = static_cast<std::int32_t>(object.as<double>());
            return;
        case R::FieldKind::Enum: {
            if (object.get_type() == sol::type::number) {
                value.integer = static_cast<std::int32_t>(object.as<double>());
                return;
            }
            if (object.get_type() != sol::type::string) bad("an option name or index");
            const int index = A::enumIndex(field, object.as<std::string>());
            if (index < 0) {
                std::string options;
                for (const char* option : field.options) {
                    if (!options.empty()) options += ", ";
                    options += option;
                }
                raise("'" + object.as<std::string>() + "' is not a value of '" + field.name +
                      "' (options: " + options + ")");
            }
            value.integer = index;
            return;
        }
        case R::FieldKind::String: {
            if (object.get_type() != sol::type::string) bad("text");
            const std::string text = object.as<std::string>();
            std::strncpy(value.text, text.c_str(), sizeof(value.text) - 1);
            value.text[sizeof(value.text) - 1] = '\0';
            return;
        }
        case R::FieldKind::Vec2:
        case R::FieldKind::Vec3:
        case R::FieldKind::Vec4:
        case R::FieldKind::Color: {
            if (object.get_type() != sol::type::table) bad("a table such as {x=, y=, z=}");
            const sol::table t = object.as<sol::table>();
            value.vec[0] = part(t, "x", "r", 0, value.vec[0]);
            value.vec[1] = part(t, "y", "g", 1, value.vec[1]);
            value.vec[2] = part(t, "z", "b", 2, value.vec[2]);
            value.vec[3] = part(t, "w", "a", 3, value.vec[3]);
            return;
        }
    }
}

const R::ComponentDesc& need(const std::string& name) {
    const R::ComponentDesc* desc = R::findByName(name);
    if (!desc) {
        raise("unknown component '" + name + "' (caffeine.component.list() shows every component)");
    }
    return *desc;
}

const R::FieldDesc& needField(const R::ComponentDesc& desc, const std::string& name) {
    const R::FieldDesc* field = R::findField(desc, name);
    if (!field) {
        std::string known;
        for (const R::FieldDesc& f : desc.fields) {
            if (!known.empty()) known += ", ";
            known += f.name;
        }
        raise(std::string("component '") + desc.name + "' has no field '" + name + "' (fields: " + known + ")");
    }
    return *field;
}

}  // namespace

void registerComponentScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["component"] = lua.create_table();
    sol::table ct = lua["caffeine"]["component"];

    auto world = [worldPtr]() -> ECS::World& {
        if (!worldPtr || !*worldPtr) raise("there is no world");
        return **worldPtr;
    };
    auto entityOf = [world](u32 id) -> ECS::Entity {
        ECS::World& w = world();
        ECS::Entity entity(id, &w);
        if (!entity.isValid() || !w.isEntityAlive(entity)) {
            raise("entity " + std::to_string(id) + " does not exist");
        }
        return entity;
    };

    // list(): every component type with its fields, for discovery and tooling.
    ct["list"] = [&lua]() {
        sol::table out = lua.create_table();
        int i = 1;
        for (const R::ComponentDesc& desc : R::registry()) {
            sol::table entry = lua.create_table();
            entry["name"] = std::string(desc.name);
            entry["id"] = desc.id;
            sol::table fields = lua.create_table();
            int f = 1;
            for (const R::FieldDesc& field : desc.fields) {
                sol::table info = lua.create_table();
                info["name"] = std::string(field.name);
                info["kind"] = std::string(kindName(field.kind));
                info["min"] = field.minValue;
                info["max"] = field.maxValue;
                if (!field.options.empty()) {
                    sol::table options = lua.create_table();
                    int o = 1;
                    for (const char* option : field.options) options[o++] = std::string(option);
                    info["options"] = options;
                }
                fields[f++] = info;
            }
            entry["fields"] = fields;
            out[i++] = entry;
        }
        return out;
    };

    // names(entity): the components the entity has.
    ct["names"] = [&lua, world, entityOf](u32 id) {
        sol::table out = lua.create_table();
        int i = 1;
        for (const R::ComponentDesc* desc : R::presentComponents(world(), entityOf(id))) {
            out[i++] = std::string(desc->name);
        }
        return out;
    };

    ct["has"] = [world, entityOf](u32 id, const std::string& name) {
        return need(name).has(world(), entityOf(id));
    };

    ct["add"] = [world, entityOf](u32 id, const std::string& name) {
        const R::ComponentDesc& desc = need(name);
        ECS::Entity entity = entityOf(id);
        if (!desc.has(world(), entity)) desc.add(world(), entity);
        return true;
    };

    ct["remove"] = [world, entityOf](u32 id, const std::string& name) {
        const R::ComponentDesc& desc = need(name);
        ECS::Entity entity = entityOf(id);
        if (!desc.has(world(), entity)) return false;
        desc.remove(world(), entity);
        return true;
    };

    // get(entity, name): a table of every field; nil when the entity lacks the component.
    ct["get"] = [&lua, world, entityOf](u32 id, const std::string& name) -> sol::object {
        const R::ComponentDesc& desc = need(name);
        ECS::Entity entity = entityOf(id);
        if (!desc.has(world(), entity)) return sol::nil;
        sol::table out = lua.create_table();
        for (const R::FieldDesc& field : desc.fields) {
            R::FieldValue value;
            if (field.get(world(), entity, value)) out[std::string(field.name)] = toLua(lua, field, value);
        }
        return out;
    };

    ct["getField"] = [&lua, world, entityOf](u32 id, const std::string& name,
                                              const std::string& fieldName) -> sol::object {
        const R::ComponentDesc& desc = need(name);
        const R::FieldDesc& field = needField(desc, fieldName);
        ECS::Entity entity = entityOf(id);
        if (!desc.has(world(), entity)) {
            raise("entity " + std::to_string(id) + " has no '" + desc.name + "' component");
        }
        R::FieldValue value;
        if (!field.get(world(), entity, value)) raise("could not read '" + fieldName + "'");
        return toLua(lua, field, value);
    };

    ct["setField"] = [world, entityOf](u32 id, const std::string& name, const std::string& fieldName,
                                       sol::object object) {
        const R::ComponentDesc& desc = need(name);
        const R::FieldDesc& field = needField(desc, fieldName);
        ECS::Entity entity = entityOf(id);
        if (!desc.has(world(), entity)) {
            raise("entity " + std::to_string(id) + " has no '" + desc.name +
                  "' component (caffeine.component.add first)");
        }
        R::FieldValue value;
        if (!field.get(world(), entity, value)) raise("could not read '" + fieldName + "'");
        fromLua(field, object, value);
        if (!field.set(world(), entity, value)) raise("could not write '" + fieldName + "'");
    };

    // set(entity, name, {field = value, ...}): adds the component when missing and writes the
    // given fields; fields left out keep their value.
    ct["set"] = [world, entityOf](u32 id, const std::string& name, sol::table values) {
        const R::ComponentDesc& desc = need(name);
        ECS::Entity entity = entityOf(id);
        if (!desc.has(world(), entity)) desc.add(world(), entity);
        for (const auto& pair : values) {
            if (pair.first.get_type() != sol::type::string) continue;
            const R::FieldDesc& field = needField(desc, pair.first.as<std::string>());
            R::FieldValue value;
            if (!field.get(world(), entity, value)) raise(std::string("could not read '") + field.name + "'");
            fromLua(field, pair.second, value);
            if (!field.set(world(), entity, value)) raise(std::string("could not write '") + field.name + "'");
        }
    };
}

}  // namespace Caffeine::Script
