#include "audio/AudioService.hpp"
#include "script/ScriptHostServices.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {

// `caffeine.audio`, `caffeine.scene` and `caffeine.session`.
void registerAudioSceneBindings(sol::state& lua, Audio::AudioService** audioPtr, SceneRequests* scenes) {
    auto audio = [audioPtr]() -> Audio::AudioService* { return audioPtr ? *audioPtr : nullptr; };

    sol::table a = lua["caffeine"]["audio"].get_or_create<sol::table>();
    a["isAvailable"] = [audio]() { auto* s = audio(); return s && s->ready(); };
    a["play"] = [audio](const std::string& path, sol::optional<f32> volume, sol::optional<f32> pan, sol::optional<bool> loop) -> u32 {
        auto* s = audio();
        return s ? s->play(path, volume.value_or(1.0f), pan.value_or(0.0f), loop.value_or(false)) : 0u;
    };
    a["playAt"] = [audio](const std::string& path, f32 x, f32 y, sol::optional<f32> maxDistance, sol::optional<f32> volume) -> u32 {
        auto* s = audio();
        return s ? s->playAt(path, x, y, maxDistance.value_or(500.0f), volume.value_or(1.0f)) : 0u;
    };
    a["stop"] = [audio](u32 h) { auto* s = audio(); return s && s->stop(h); };
    a["pause"] = [audio](u32 h) { auto* s = audio(); return s && s->pause(h); };
    a["resume"] = [audio](u32 h) { auto* s = audio(); return s && s->resume(h); };
    a["setVolume"] = [audio](u32 h, f32 v) { auto* s = audio(); return s && s->setVolume(h, v); };
    a["setLoop"] = [audio](u32 h, bool l) { auto* s = audio(); return s && s->setLoop(h, l); };
    a["isPlaying"] = [audio](u32 h) { auto* s = audio(); return s && s->isPlaying(h); };
    a["stopAll"] = [audio]() { if (auto* s = audio()) s->stopAll(); };
    a["setMasterVolume"] = [audio](f32 v) { if (auto* s = audio()) s->setMasterVolume(v); };
    a["masterVolume"] = [audio]() -> f32 { auto* s = audio(); return s ? s->masterVolume() : 0.0f; };
    a["setListener"] = [audio](f32 x, f32 y) { if (auto* s = audio()) s->setListener(x, y); };

    sol::table sc = lua["caffeine"]["scene"].get_or_create<sol::table>();
    sc["load"] = [scenes](const std::string& path) { if (scenes && !path.empty()) scenes->requestLoad(path); };
    sc["reload"] = [scenes]() { if (scenes) scenes->requestReload(); };
    sc["current"] = [scenes]() { return scenes ? scenes->current() : std::string(); };
    sc["isLoading"] = [scenes]() { return scenes && scenes->hasPending(); };

    sol::table ss = lua["caffeine"]["session"].get_or_create<sol::table>();
    ss["set"] = [](const std::string& key, sol::object value) {
        auto& store = SessionStore::instance();
        if (value.is<bool>()) store.setBoolean(key, value.as<bool>());
        else if (value.is<double>()) store.setNumber(key, value.as<double>());
        else if (value.is<std::string>()) store.setString(key, value.as<std::string>());
        else store.remove(key);  // nil removes
    };
    ss["get"] = [](const std::string& key, sol::this_state s) -> sol::object {
        const auto* v = SessionStore::instance().get(key);
        if (!v) return sol::nil;
        switch (v->kind) {
            case SessionStore::Value::Kind::Boolean: return sol::make_object(s, v->boolean);
            case SessionStore::Value::Kind::Number: return sol::make_object(s, v->number);
            default: return sol::make_object(s, v->string);
        }
    };
    ss["has"] = [](const std::string& key) { return SessionStore::instance().get(key) != nullptr; };
    ss["remove"] = [](const std::string& key) { return SessionStore::instance().remove(key); };
    ss["clear"] = []() { SessionStore::instance().clear(); };
}

}  // namespace Caffeine::Script
