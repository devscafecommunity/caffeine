#include "caffeine/postprocess/PostProcessPresets.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "ecs/PostProcessFields.hpp"
#include "ecs/World.hpp"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace Caffeine::Script {
namespace {

using ECS::PostProcessComponent;
using ECS::PostProcessField;

ECS::World* currentWorld(ECS::World** worldPtr) {
    return worldPtr ? *worldPtr : nullptr;
}

PostProcessComponent* postProcessForEntity(ECS::World* world, u32 entityId) {
    if (!world || !world->isEntityAlive(ECS::Entity(entityId, world))) return nullptr;
    ECS::Entity entity(entityId, world);
    return entity.get<PostProcessComponent>();
}

sol::object fieldToLua(sol::state_view lua, const PostProcessComponent& fx, const PostProcessField& field) {
    const f32 value = *ECS::getPostProcessValue(fx, field);
    switch (field.kind) {
        case PostProcessField::Kind::Bool: return sol::make_object(lua, value != 0.0f);
        case PostProcessField::Kind::UInt: return sol::make_object(lua, static_cast<u32>(value));
        case PostProcessField::Kind::Float: break;
    }
    return sol::make_object(lua, value);
}

bool luaToFieldValue(const sol::object& value, f32& out) {
    if (value.is<bool>()) {
        out = value.as<bool>() ? 1.0f : 0.0f;
        return true;
    }
    if (value.is<double>()) {
        out = static_cast<f32>(value.as<double>());
        return true;
    }
    return false;
}

sol::table componentToTable(sol::state_view lua, const PostProcessComponent& fx) {
    sol::table t = lua.create_table();
    t["enabled"] = fx.enabled;
    t["customEffectScript"] = std::string(fx.customEffectScript);
    for (const PostProcessField& field : ECS::postProcessFields()) {
        const std::string effect(field.effect);
        sol::optional<sol::table> group = t[effect];
        if (!group) t[effect] = lua.create_table();
        sol::table g = t[effect];
        g[std::string(field.name)] = fieldToLua(lua, fx, field);
    }
    return t;
}

void setCustomScript(PostProcessComponent& fx, const std::string& path) {
    std::strncpy(fx.customEffectScript, path.c_str(), sizeof(fx.customEffectScript) - 1);
    fx.customEffectScript[sizeof(fx.customEffectScript) - 1] = '\0';
}

/// Deep-merges a Lua table shaped like componentToTable() into the component.
void applyTable(PostProcessComponent& fx, const sol::table& t) {
    if (sol::optional<bool> enabled = t["enabled"]) fx.enabled = *enabled;
    if (sol::optional<std::string> script = t["customEffectScript"]) setCustomScript(fx, *script);
    for (const PostProcessField& field : ECS::postProcessFields()) {
        sol::optional<sol::table> group = t[std::string(field.effect)];
        if (!group) continue;
        sol::object value = (*group)[std::string(field.name)];
        f32 v = 0.0f;
        if (luaToFieldValue(value, v)) ECS::setPostProcessValue(fx, field, v);
    }
}

bool applyPreset(PostProcessComponent& fx, const std::string& name) {
    if (name == "cinematic") PostProcess::applyBenchmarkCinematic(fx);
    else if (name == "horror") PostProcess::applyBenchmarkHorror(fx);
    else if (name == "arcade") PostProcess::applyBenchmarkArcade(fx);
    else if (name == "reset" || name == "default") PostProcess::resetAllEffects(fx);
    else return false;
    return true;
}

/// Sets an intensity-like field and toggles the effect on when it becomes visible.
void setIntensity(PostProcessComponent* fx, const char* path, const char* effect, f32 value) {
    if (!fx) return;
    ECS::setPostProcessValue(*fx, path, value);
    ECS::setPostProcessEffectEnabled(*fx, effect, value > 0.001f);
}

/// Per-entity handle given to onPostProcess(entityId, dt, api): api.get/set/enable/... without ids.
sol::table makeHandle(sol::state_view lua, ECS::World** worldPtr, u32 entityId) {
    sol::table api = lua.create_table();
    api["entity"] = entityId;
    api["get"] = [worldPtr, entityId](const std::string& path, sol::this_state s) -> sol::object {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        const PostProcessField* field = ECS::findPostProcessField(path);
        if (!fx || !field) return sol::lua_nil;
        return fieldToLua(s, *fx, *field);
    };
    api["set"] = [worldPtr, entityId](const std::string& path, sol::object value) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        f32 v = 0.0f;
        return fx && luaToFieldValue(value, v) && ECS::setPostProcessValue(*fx, path, v);
    };
    api["enable"] = [worldPtr, entityId](const std::string& effect, sol::optional<bool> enabled) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        return fx && ECS::setPostProcessEffectEnabled(*fx, effect, enabled.value_or(true));
    };
    api["isEnabled"] = [worldPtr, entityId](const std::string& effect) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        return fx && ECS::isPostProcessEffectEnabled(*fx, effect).value_or(false);
    };
    api["snapshot"] = [worldPtr, entityId](sol::this_state s) -> sol::object {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        if (!fx) return sol::lua_nil;
        return componentToTable(s, *fx);
    };
    api["apply"] = [worldPtr, entityId](const sol::table& values) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) applyTable(*fx, values);
    };
    api["preset"] = [worldPtr, entityId](const std::string& name) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        return fx && applyPreset(*fx, name);
    };
    return api;
}

void registerPostProcessBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["postprocess"] = lua.create_table();
    sol::table pp = lua["caffeine"]["postprocess"];

    pp["has"] = [worldPtr](u32 entityId) {
        return postProcessForEntity(currentWorld(worldPtr), entityId) != nullptr;
    };
    pp["add"] = [worldPtr](u32 entityId) -> bool {
        ECS::World* world = currentWorld(worldPtr);
        if (!world || !world->isEntityAlive(ECS::Entity(entityId, world))) return false;
        ECS::Entity(entityId, world).getOrAdd<PostProcessComponent>();
        return true;
    };
    pp["remove"] = [worldPtr](u32 entityId) {
        ECS::World* world = currentWorld(worldPtr);
        if (!postProcessForEntity(world, entityId)) return;
        ECS::Entity(entityId, world).remove<PostProcessComponent>();
    };

    pp["get"] = [worldPtr](u32 entityId, sol::this_state s) -> sol::table {
        sol::state_view view(s);
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        if (!fx) return view.create_table();
        sol::table t = componentToTable(view, *fx);
        // Flat aliases kept for older scripts.
        t["exposure"] = fx->colorGrading.exposure;
        t["vignetteIntensity"] = fx->vignette.intensity;
        t["bloomIntensity"] = fx->bloom.intensity;
        return t;
    };
    pp["set"] = [worldPtr](u32 entityId, const sol::table& values) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        if (!fx) return false;
        applyTable(*fx, values);
        return true;
    };
    pp["getValue"] = [worldPtr](u32 entityId, const std::string& path, sol::this_state s) -> sol::object {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        const PostProcessField* field = ECS::findPostProcessField(path);
        if (!fx || !field) return sol::lua_nil;
        return fieldToLua(s, *fx, *field);
    };
    pp["setValue"] = [worldPtr](u32 entityId, const std::string& path, sol::object value) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        f32 v = 0.0f;
        return fx && luaToFieldValue(value, v) && ECS::setPostProcessValue(*fx, path, v);
    };

    pp["effects"] = [](sol::this_state s) {
        sol::state_view view(s);
        sol::table out = view.create_table();
        std::string_view last;
        int i = 1;
        for (const PostProcessField& field : ECS::postProcessFields()) {
            if (field.effect == last) continue;
            last = field.effect;
            out[i++] = std::string(field.effect);
        }
        return out;
    };
    pp["fields"] = [](const std::string& effect, sol::this_state s) {
        sol::state_view view(s);
        sol::table out = view.create_table();
        const std::string_view name = ECS::canonicalPostProcessEffect(effect);
        int i = 1;
        for (const PostProcessField& field : ECS::postProcessFields()) {
            if (field.effect != name) continue;
            sol::table f = view.create_table();
            f["name"] = std::string(field.name);
            f["min"] = field.minValue;
            f["max"] = field.maxValue;
            f["type"] = field.kind == PostProcessField::Kind::Bool    ? "bool"
                        : field.kind == PostProcessField::Kind::UInt ? "int"
                                                                      : "number";
            out[i++] = f;
        }
        return out;
    };

    pp["setEnabled"] = [worldPtr](u32 entityId, bool enabled) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) fx->enabled = enabled;
    };
    pp["enableEffect"] = [worldPtr](u32 entityId, const std::string& effect, bool enabled) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        return fx && ECS::setPostProcessEffectEnabled(*fx, effect, enabled);
    };
    pp["isEffectEnabled"] = [worldPtr](u32 entityId, const std::string& effect) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        return fx && ECS::isPostProcessEffectEnabled(*fx, effect).value_or(false);
    };
    pp["applyPreset"] = [worldPtr](u32 entityId, const std::string& name) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        return fx && applyPreset(*fx, name);
    };
    pp["blend"] = [worldPtr](u32 entityId, const sol::table& from, const sol::table& to, f32 t) -> bool {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        if (!fx) return false;
        PostProcessComponent a = *fx;
        PostProcessComponent b = *fx;
        applyTable(a, from);
        applyTable(b, to);
        const std::string script = fx->customEffectScript;
        *fx = ECS::lerpPostProcess(a, b, std::clamp(t, 0.0f, 1.0f));
        setCustomScript(*fx, script);
        return true;
    };

    // Convenience setters (each enables its effect when the value becomes visible).
    pp["setExposure"] = [worldPtr](u32 entityId, f32 exposure) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) {
            fx->colorGrading.exposure = exposure;
            fx->colorGrading.enabled = true;
        }
    };
    pp["setColorGrading"] = [worldPtr](u32 entityId, f32 contrast, f32 saturation, sol::optional<f32> temperature,
                                       sol::optional<f32> tint) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) {
            ECS::setPostProcessValue(*fx, "colorGrading.contrast", contrast);
            ECS::setPostProcessValue(*fx, "colorGrading.saturation", saturation);
            if (temperature) ECS::setPostProcessValue(*fx, "colorGrading.temperature", *temperature);
            if (tint) ECS::setPostProcessValue(*fx, "colorGrading.tint", *tint);
            fx->colorGrading.enabled = true;
        }
    };
    pp["setBloom"] = [worldPtr](u32 entityId, f32 intensity, sol::optional<f32> threshold) {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        setIntensity(fx, "bloom.intensity", "bloom", intensity);
        if (fx && threshold) ECS::setPostProcessValue(*fx, "bloom.threshold", *threshold);
    };
    pp["setVignette"] = [worldPtr](u32 entityId, f32 intensity) {
        setIntensity(postProcessForEntity(currentWorld(worldPtr), entityId), "vignette.intensity", "vignette",
                     intensity);
    };
    pp["setChromaticAberration"] = [worldPtr](u32 entityId, f32 intensity) {
        setIntensity(postProcessForEntity(currentWorld(worldPtr), entityId), "chromaticAberration.intensity",
                     "chromaticAberration", intensity);
    };
    pp["setGrain"] = [worldPtr](u32 entityId, f32 intensity) {
        setIntensity(postProcessForEntity(currentWorld(worldPtr), entityId), "grain.intensity", "grain", intensity);
    };
    pp["setLensDistortion"] = [worldPtr](u32 entityId, f32 intensity) {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        if (!fx) return;
        ECS::setPostProcessValue(*fx, "lensDistortion.intensity", intensity);
        fx->lensDistortion.enabled = std::abs(intensity) > 0.001f;
    };
    pp["setMotionBlur"] = [worldPtr](u32 entityId, f32 intensity) {
        setIntensity(postProcessForEntity(currentWorld(worldPtr), entityId), "motionBlur.intensity", "motionBlur",
                     intensity);
    };
    pp["setDepthOfField"] = [worldPtr](u32 entityId, f32 focusDistance, f32 aperture) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) {
            ECS::setPostProcessValue(*fx, "depthOfField.focusDistance", focusDistance);
            ECS::setPostProcessValue(*fx, "depthOfField.aperture", aperture);
            fx->depthOfField.enabled = true;
        }
    };
    pp["setFog"] = [worldPtr](u32 entityId, f32 density, sol::optional<f32> r, sol::optional<f32> g,
                              sol::optional<f32> b) {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        setIntensity(fx, "deferredFog.density", "deferredFog", density);
        if (fx && r && g && b) {
            ECS::setPostProcessValue(*fx, "deferredFog.colorR", *r);
            ECS::setPostProcessValue(*fx, "deferredFog.colorG", *g);
            ECS::setPostProcessValue(*fx, "deferredFog.colorB", *b);
        }
    };
    pp["setAmbientOcclusion"] = [worldPtr](u32 entityId, f32 intensity, sol::optional<f32> radius) {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        setIntensity(fx, "ambientOcclusion.intensity", "ambientOcclusion", intensity);
        if (fx && radius) ECS::setPostProcessValue(*fx, "ambientOcclusion.radius", *radius);
    };
    pp["setAntiAliasing"] = [worldPtr](u32 entityId, const std::string& mode) {
        auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId);
        if (!fx) return;
        fx->antiAliasing.enabled = mode != "off" && mode != "none";
        fx->antiAliasing.mode = mode == "fxaa" ? 0u : 1u;
    };
    pp["setAutoExposure"] = [worldPtr](u32 entityId, bool enabled, sol::optional<f32> compensation) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) {
            fx->autoExposure.enabled = enabled;
            if (compensation) ECS::setPostProcessValue(*fx, "autoExposure.compensation", *compensation);
        }
    };

    pp["setCustomScript"] = [worldPtr](u32 entityId, const std::string& path) {
        if (auto* fx = postProcessForEntity(currentWorld(worldPtr), entityId)) setCustomScript(*fx, path);
    };
    pp["handle"] = [worldPtr](u32 entityId, sol::this_state s) { return makeHandle(s, worldPtr, entityId); };
}

}  // namespace

void registerPostProcessScriptBindings(sol::state& lua, ECS::World** worldPtr);
sol::table makePostProcessScriptHandle(sol::state_view lua, ECS::World** worldPtr, u32 entityId);

void registerPostProcessScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    registerPostProcessBindings(lua, worldPtr);
}

sol::table makePostProcessScriptHandle(sol::state_view lua, ECS::World** worldPtr, u32 entityId) {
    return makeHandle(lua, worldPtr, entityId);
}

}  // namespace Caffeine::Script
