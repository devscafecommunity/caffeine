// Dryad natives for generic, reflection-driven component access. They mirror Lua's
// `caffeine.component`: whatever the reflection registry describes is scriptable here too.
//
//   caffeine_component_list()                      -> [names]
//   caffeine_component_names(entity)               -> [names the entity has]
//   caffeine_component_has / _add / _remove (entity, name)
//   caffeine_component_get(entity, name)           -> object of every field (null when missing)
//   caffeine_component_get_field(entity, name, field)
//   caffeine_component_set_field(entity, name, field, value)
//   caffeine_component_set(entity, name, object)   -> adds the component, writes the given fields
//   caffeine_component_get_number(entity, name, field[, index])  numbers, bools, enum indices, vector parts
//   caffeine_component_set_number(entity, name, field, value[, index])
//   caffeine_component_set_vec(entity, name, field, x, y[, z[, w]])  whole vectors and colours
//   caffeine_component_get_text / caffeine_component_set_text(entity, name, field[, text])  strings and enum labels
//
// Vectors and colours are objects ({x, y, z} / {x, y, z, w}); enums are option names; errors are
// logged and the native returns null/false.

#include "debug/LogSystem.hpp"
#include "ecs/World.hpp"
#include "scene/ComponentAccess.hpp"
#include "scene/ComponentReflection.hpp"

#include "dryad/compiler/interpreter.h"
#include "dryad/runtime/value.h"

#include <cstring>

namespace Caffeine::Script {
namespace {

namespace R = Caffeine::Scene::Reflection;
namespace A = Caffeine::Scene::Access;
using Args = std::vector<dryad::Value>;

double numberOf(const dryad::Value& v) {
    if (v.is_integer()) return static_cast<double>(v.as_integer());
    if (v.is_float()) return v.as_float();
    if (v.is_boolean()) return v.as_boolean() ? 1.0 : 0.0;
    return 0.0;
}

std::string textOf(const Args& args, size_t i) {
    return i < args.size() && args[i].is_string() ? args[i].as_string() : std::string();
}

dryad::Value toDryad(const R::FieldDesc& field, const R::FieldValue& v) {
    switch (field.kind) {
        case R::FieldKind::F32: return dryad::Value(static_cast<double>(v.vec[0]));
        case R::FieldKind::Bool: return dryad::Value(v.boolean != 0);
        case R::FieldKind::I32:
        case R::FieldKind::U32: return dryad::Value(static_cast<int64_t>(v.integer));
        case R::FieldKind::Enum: return dryad::Value(A::enumLabel(field, v.integer));
        case R::FieldKind::String: return dryad::Value(std::string(v.text));
        default: {
            const int parts = field.kind == R::FieldKind::Vec2 ? 2 : (field.kind == R::FieldKind::Vec3 ? 3 : 4);
            const char* names[4] = {"x", "y", "z", "w"};
            dryad::Value out = dryad::Value::create_object();
            for (int i = 0; i < parts; ++i) {
                out.object_set(names[i], dryad::Value(static_cast<double>(v.vec[i])));
            }
            return out;
        }
    }
}

bool apply(const R::FieldDesc& field, const dryad::Value& in, R::FieldValue& v, std::string& error) {
    switch (field.kind) {
        case R::FieldKind::F32:
            if (!in.is_number()) { error = "expects a number"; return false; }
            v.vec[0] = static_cast<float>(numberOf(in));
            return true;
        case R::FieldKind::Bool:
            if (!in.is_boolean()) { error = "expects true or false"; return false; }
            v.boolean = in.as_boolean() ? 1 : 0;
            return true;
        case R::FieldKind::I32:
        case R::FieldKind::U32:
            if (!in.is_number()) { error = "expects a number"; return false; }
            v.integer = static_cast<std::int32_t>(numberOf(in));
            return true;
        case R::FieldKind::Enum: {
            if (in.is_number()) { v.integer = static_cast<std::int32_t>(numberOf(in)); return true; }
            if (!in.is_string()) { error = "expects an option name or index"; return false; }
            const int index = A::enumIndex(field, in.as_string());
            if (index < 0) { error = "'" + in.as_string() + "' is not an option"; return false; }
            v.integer = index;
            return true;
        }
        case R::FieldKind::String:
            if (!in.is_string()) { error = "expects text"; return false; }
            std::strncpy(v.text, in.as_string().c_str(), sizeof(v.text) - 1);
            v.text[sizeof(v.text) - 1] = '\0';
            return true;
        default: {
            if (!in.is_object()) { error = "expects an object such as {x: 1, y: 2, z: 3}"; return false; }
            const char* names[4] = {"x", "y", "z", "w"};
            const char* alt[4] = {"r", "g", "b", "a"};
            for (int i = 0; i < 4; ++i) {
                if (in.object_has(names[i])) v.vec[i] = static_cast<float>(numberOf(in.object_get(names[i])));
                else if (in.object_has(alt[i])) v.vec[i] = static_cast<float>(numberOf(in.object_get(alt[i])));
            }
            return true;
        }
    }
}

}  // namespace

void bindComponentNatives(dryad::Interpreter& interp, ECS::World** worldPtr) {
    auto fail = [](const std::string& message) {
        CF_ERROR("Dryad", "caffeine_component: %s", message.c_str());
    };
    auto entityOf = [worldPtr, fail](const Args& args, ECS::Entity& out) {
        if (!worldPtr || !*worldPtr || args.empty()) {
            fail("there is no world or entity");
            return false;
        }
        out = ECS::Entity(static_cast<u32>(numberOf(args[0])), *worldPtr);
        if (!out.isValid() || !(*worldPtr)->isEntityAlive(out)) {
            fail("entity " + std::to_string(static_cast<u32>(numberOf(args[0]))) + " does not exist");
            return false;
        }
        return true;
    };

    interp.define_native("caffeine_component_list", [](const Args&) -> dryad::Value {
        dryad::Value result = dryad::Value::create_array();
        for (const R::ComponentDesc& desc : R::registry()) {
            result.array_push(dryad::Value(std::string(desc.name)));
        }
        return result;
    });

    interp.define_native("caffeine_component_names", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        dryad::Value result = dryad::Value::create_array();
        if (!entityOf(args, entity)) return result;
        for (const R::ComponentDesc* desc : R::presentComponents(**worldPtr, entity)) {
            result.array_push(dryad::Value(std::string(desc->name)));
        }
        return result;
    });

    interp.define_native("caffeine_component_has", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity)) return dryad::Value(false);
        std::string error;
        const bool has = A::has(**worldPtr, entity, textOf(args, 1), &error);
        if (!error.empty()) fail(error);
        return dryad::Value(has);
    });

    interp.define_native("caffeine_component_add", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity)) return dryad::Value(false);
        std::string error;
        const bool ok = A::add(**worldPtr, entity, textOf(args, 1), &error);
        if (!ok) fail(error);
        return dryad::Value(ok);
    });

    interp.define_native("caffeine_component_remove", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity)) return dryad::Value(false);
        std::string error;
        const bool ok = A::remove(**worldPtr, entity, textOf(args, 1), &error);
        if (!error.empty()) fail(error);
        return dryad::Value(ok);
    });

    interp.define_native("caffeine_component_get", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        dryad::Value result;
        if (!entityOf(args, entity)) return result;
        const R::ComponentDesc* desc = R::findByName(textOf(args, 1));
        if (!desc) {
            fail("unknown component '" + textOf(args, 1) + "'");
            return result;
        }
        if (!desc->has(**worldPtr, entity)) return result;
        result = dryad::Value::create_object();
        for (const R::FieldDesc& field : desc->fields) {
            R::FieldValue value;
            if (field.get(**worldPtr, entity, value)) result.object_set(field.name, toDryad(field, value));
        }
        return result;
    });

    interp.define_native("caffeine_component_get_field", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity)) return dryad::Value();
        const R::ComponentDesc* desc = R::findByName(textOf(args, 1));
        if (!desc) {
            fail("unknown component '" + textOf(args, 1) + "'");
            return dryad::Value();
        }
        const R::FieldDesc* field = R::findField(*desc, textOf(args, 2));
        if (!field) {
            fail(std::string("component '") + desc->name + "' has no field '" + textOf(args, 2) + "'");
            return dryad::Value();
        }
        R::FieldValue value;
        if (!desc->has(**worldPtr, entity) || !field->get(**worldPtr, entity, value)) {
            fail(std::string("entity has no '") + desc->name + "' component");
            return dryad::Value();
        }
        return toDryad(*field, value);
    });

    auto write = [=](ECS::Entity entity, const R::ComponentDesc& desc, const R::FieldDesc& field,
                     const dryad::Value& in) {
        R::FieldValue value;
        if (!field.get(**worldPtr, entity, value)) {
            fail(std::string("could not read '") + field.name + "'");
            return false;
        }
        std::string error;
        if (!apply(field, in, value, error)) {
            fail(std::string("field '") + field.name + "' " + error);
            return false;
        }
        if (!field.set(**worldPtr, entity, value)) {
            fail(std::string("could not write '") + desc.name + "." + field.name + "'");
            return false;
        }
        return true;
    };

    interp.define_native("caffeine_component_set_field", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity) || args.size() < 4) return dryad::Value(false);
        const R::ComponentDesc* desc = R::findByName(textOf(args, 1));
        if (!desc) {
            fail("unknown component '" + textOf(args, 1) + "'");
            return dryad::Value(false);
        }
        const R::FieldDesc* field = R::findField(*desc, textOf(args, 2));
        if (!field) {
            fail(std::string("component '") + desc->name + "' has no field '" + textOf(args, 2) + "'");
            return dryad::Value(false);
        }
        if (!desc->has(**worldPtr, entity)) {
            fail(std::string("entity has no '") + desc->name + "' component (caffeine_component_add first)");
            return dryad::Value(false);
        }
        return dryad::Value(write(entity, *desc, *field, args[3]));
    });

    interp.define_native("caffeine_component_get_number", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity)) return dryad::Value();
        double out = 0.0;
        std::string error;
        const int index = args.size() > 3 ? static_cast<int>(numberOf(args[3])) : 0;
        if (!A::getNumber(**worldPtr, entity, textOf(args, 1), textOf(args, 2), index, out, &error)) {
            fail(error);
            return dryad::Value();
        }
        // Integers, enum indices and bools come back as Dryad integers/booleans so `== 6` and
        // `if (flag)` behave; floats and vector parts stay floats.
        if (const R::ComponentDesc* desc = R::findByName(textOf(args, 1))) {
            if (const R::FieldDesc* field = R::findField(*desc, textOf(args, 2))) {
                if (field->kind == R::FieldKind::Bool) return dryad::Value(out != 0.0);
                if (field->kind == R::FieldKind::I32 || field->kind == R::FieldKind::U32 ||
                    field->kind == R::FieldKind::Enum) {
                    return dryad::Value(static_cast<int64_t>(out));
                }
            }
        }
        return dryad::Value(out);
    });

    interp.define_native("caffeine_component_set_number", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity) || args.size() < 4) return dryad::Value(false);
        std::string error;
        const int index = args.size() > 4 ? static_cast<int>(numberOf(args[4])) : 0;
        const bool ok = args[3].is_boolean()
            ? A::setNumber(**worldPtr, entity, textOf(args, 1), textOf(args, 2), index,
                           args[3].as_boolean() ? 1.0 : 0.0, &error)
            : A::setNumber(**worldPtr, entity, textOf(args, 1), textOf(args, 2), index,
                           numberOf(args[3]), &error);
        if (!ok) fail(error);
        return dryad::Value(ok);
    });

    interp.define_native("caffeine_component_set_vec", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity) || args.size() < 5) return dryad::Value(false);
        bool ok = true;
        std::string error;
        for (size_t i = 3; i < args.size() && i < 7; ++i) {
            ok = A::setNumber(**worldPtr, entity, textOf(args, 1), textOf(args, 2),
                              static_cast<int>(i - 3), numberOf(args[i]), &error) && ok;
        }
        if (!ok) fail(error);
        return dryad::Value(ok);
    });

    interp.define_native("caffeine_component_get_text", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity)) return dryad::Value();
        std::string out, error;
        if (!A::getText(**worldPtr, entity, textOf(args, 1), textOf(args, 2), out, &error)) {
            fail(error);
            return dryad::Value();
        }
        return dryad::Value(out);
    });

    interp.define_native("caffeine_component_set_text", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity) || args.size() < 4) return dryad::Value(false);
        std::string error;
        const bool ok = A::setText(**worldPtr, entity, textOf(args, 1), textOf(args, 2), textOf(args, 3), &error);
        if (!ok) fail(error);
        return dryad::Value(ok);
    });

    interp.define_native("caffeine_component_set", [=](const Args& args) -> dryad::Value {
        ECS::Entity entity;
        if (!entityOf(args, entity) || args.size() < 3 || !args[2].is_object()) return dryad::Value(false);
        const R::ComponentDesc* desc = R::findByName(textOf(args, 1));
        if (!desc) {
            fail("unknown component '" + textOf(args, 1) + "'");
            return dryad::Value(false);
        }
        if (!desc->has(**worldPtr, entity)) desc->add(**worldPtr, entity);
        bool ok = true;
        for (const std::string& key : args[2].object_keys()) {
            const R::FieldDesc* field = R::findField(*desc, key);
            if (!field) {
                fail(std::string("component '") + desc->name + "' has no field '" + key + "'");
                ok = false;
                continue;
            }
            ok = write(entity, *desc, *field, args[2].object_get(key)) && ok;
        }
        return dryad::Value(ok);
    });
}

}  // namespace Caffeine::Script
