#include "scene/ComponentAccess.hpp"

#include "ecs/World.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Caffeine::Scene::Access {
namespace {

using namespace Reflection;

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

const ComponentDesc* component(const std::string& name, std::string* error) {
    const ComponentDesc* desc = findByName(name);
    if (!desc) {
        fail(error, "unknown component '" + name +
                        "' (caffeine.component.list() shows every component)");
    }
    return desc;
}

const FieldDesc* field(const ComponentDesc& desc, const std::string& name, std::string* error) {
    const FieldDesc* f = findField(desc, name);
    if (!f) {
        std::string known;
        for (const FieldDesc& candidate : desc.fields) {
            if (!known.empty()) known += ", ";
            known += candidate.name;
        }
        fail(error, "component '" + std::string(desc.name) + "' has no field '" + name +
                        "' (fields: " + known + ")");
    }
    return f;
}

bool readField(ECS::World& world, ECS::Entity entity, const std::string& componentName,
               const std::string& fieldName, const ComponentDesc*& desc, const FieldDesc*& f,
               FieldValue& value, std::string* error) {
    desc = component(componentName, error);
    if (!desc) return false;
    f = field(*desc, fieldName, error);
    if (!f) return false;
    if (!desc->has(world, entity)) {
        fail(error, "entity " + std::to_string(entity.id()) + " has no '" + desc->name +
                        "' component (add it with caffeine.component.add)");
        return false;
    }
    if (!f->get(world, entity, value)) {
        fail(error, "could not read '" + std::string(desc->name) + "." + f->name + "'");
        return false;
    }
    return true;
}

}  // namespace

bool has(ECS::World& world, ECS::Entity entity, const std::string& name, std::string* error) {
    const ComponentDesc* desc = component(name, error);
    return desc && desc->has(world, entity);
}

bool add(ECS::World& world, ECS::Entity entity, const std::string& name, std::string* error) {
    const ComponentDesc* desc = component(name, error);
    if (!desc) return false;
    if (!desc->has(world, entity)) desc->add(world, entity);
    return true;
}

bool remove(ECS::World& world, ECS::Entity entity, const std::string& name, std::string* error) {
    const ComponentDesc* desc = component(name, error);
    if (!desc) return false;
    if (!desc->has(world, entity)) return false;
    desc->remove(world, entity);
    return true;
}

std::string enumLabel(const FieldDesc& f, int index) {
    if (index < 0 || index >= static_cast<int>(f.options.size())) return std::to_string(index);
    return f.options[static_cast<size_t>(index)];
}

int enumIndex(const FieldDesc& f, const std::string& label) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        return s;
    };
    const std::string wanted = lower(label);
    for (size_t i = 0; i < f.options.size(); ++i) {
        if (lower(f.options[i]) == wanted) return static_cast<int>(i);
    }
    return -1;
}

bool getNumber(ECS::World& world, ECS::Entity entity, const std::string& componentName,
               const std::string& fieldName, int index, double& out, std::string* error) {
    const ComponentDesc* desc = nullptr;
    const FieldDesc* f = nullptr;
    FieldValue value;
    if (!readField(world, entity, componentName, fieldName, desc, f, value, error)) return false;
    switch (f->kind) {
        case FieldKind::F32: out = value.vec[0]; return true;
        case FieldKind::Bool: out = value.boolean ? 1.0 : 0.0; return true;
        case FieldKind::I32:
        case FieldKind::U32:
        case FieldKind::Enum: out = value.integer; return true;
        case FieldKind::Vec2:
        case FieldKind::Vec3:
        case FieldKind::Vec4:
        case FieldKind::Color: {
            const int parts = f->kind == FieldKind::Vec2 ? 2 : (f->kind == FieldKind::Vec3 ? 3 : 4);
            if (index < 0 || index >= parts) {
                fail(error, "'" + std::string(f->name) + "' has " + std::to_string(parts) +
                                " parts: index 0.." + std::to_string(parts - 1));
                return false;
            }
            out = value.vec[index];
            return true;
        }
        case FieldKind::String:
            fail(error, "'" + std::string(f->name) + "' is text: read it with a text getter");
            return false;
    }
    return false;
}

bool setNumber(ECS::World& world, ECS::Entity entity, const std::string& componentName,
               const std::string& fieldName, int index, double number, std::string* error) {
    const ComponentDesc* desc = nullptr;
    const FieldDesc* f = nullptr;
    FieldValue value;
    if (!readField(world, entity, componentName, fieldName, desc, f, value, error)) return false;
    switch (f->kind) {
        case FieldKind::F32: value.vec[0] = static_cast<float>(number); break;
        case FieldKind::Bool: value.boolean = number != 0.0 ? 1 : 0; break;
        case FieldKind::I32:
        case FieldKind::U32:
        case FieldKind::Enum: value.integer = static_cast<std::int32_t>(number); break;
        case FieldKind::Vec2:
        case FieldKind::Vec3:
        case FieldKind::Vec4:
        case FieldKind::Color: {
            const int parts = f->kind == FieldKind::Vec2 ? 2 : (f->kind == FieldKind::Vec3 ? 3 : 4);
            if (index < 0 || index >= parts) {
                fail(error, "'" + std::string(f->name) + "' has " + std::to_string(parts) +
                                " parts: index 0.." + std::to_string(parts - 1));
                return false;
            }
            value.vec[index] = static_cast<float>(number);
            break;
        }
        case FieldKind::String:
            fail(error, "'" + std::string(f->name) + "' is text: set it with a text setter");
            return false;
    }
    if (!f->set(world, entity, value)) {
        fail(error, "could not write '" + std::string(desc->name) + "." + f->name + "'");
        return false;
    }
    return true;
}

bool getText(ECS::World& world, ECS::Entity entity, const std::string& componentName,
             const std::string& fieldName, std::string& out, std::string* error) {
    const ComponentDesc* desc = nullptr;
    const FieldDesc* f = nullptr;
    FieldValue value;
    if (!readField(world, entity, componentName, fieldName, desc, f, value, error)) return false;
    if (f->kind == FieldKind::String) {
        out = value.text;
        return true;
    }
    if (f->kind == FieldKind::Enum) {
        out = enumLabel(*f, value.integer);
        return true;
    }
    fail(error, "'" + std::string(f->name) + "' is not text or an enum: read it with a number getter");
    return false;
}

bool setText(ECS::World& world, ECS::Entity entity, const std::string& componentName,
             const std::string& fieldName, const std::string& text, std::string* error) {
    const ComponentDesc* desc = nullptr;
    const FieldDesc* f = nullptr;
    FieldValue value;
    if (!readField(world, entity, componentName, fieldName, desc, f, value, error)) return false;
    if (f->kind == FieldKind::String) {
        std::strncpy(value.text, text.c_str(), sizeof(value.text) - 1);
        value.text[sizeof(value.text) - 1] = '\0';
    } else if (f->kind == FieldKind::Enum) {
        const int index = enumIndex(*f, text);
        if (index < 0) {
            std::string options;
            for (const char* option : f->options) {
                if (!options.empty()) options += ", ";
                options += option;
            }
            fail(error, "'" + text + "' is not a value of '" + f->name + "' (options: " + options + ")");
            return false;
        }
        value.integer = index;
    } else {
        fail(error, "'" + std::string(f->name) + "' is not text or an enum: set it with a number");
        return false;
    }
    if (!f->set(world, entity, value)) {
        fail(error, "could not write '" + std::string(desc->name) + "." + f->name + "'");
        return false;
    }
    return true;
}

}  // namespace Caffeine::Scene::Access
