// Dryad natives for audio, scene changes and the session store; same behaviour as Lua's
// `caffeine.audio`, `caffeine.scene` and `caffeine.session`.

#include "audio/AudioService.hpp"
#include "script/DryadNatives.hpp"
#include "script/ScriptHostServices.hpp"

namespace Caffeine::Script {

using Args = std::vector<dryad::Value>;

namespace {
double num(const Args& a, size_t i, double fallback = 0.0) {
    if (i >= a.size()) return fallback;
    if (a[i].is_integer()) return static_cast<double>(a[i].as_integer());
    if (a[i].is_float()) return a[i].as_float();
    return fallback;
}
std::string str(const Args& a, size_t i) { return i < a.size() && a[i].is_string() ? a[i].as_string() : std::string(); }
bool flag(const Args& a, size_t i) { return i < a.size() && a[i].is_boolean() && a[i].as_boolean(); }
dryad::Value handle(u32 h) { return dryad::Value(static_cast<int64_t>(h)); }
}  // namespace

void bindAudioSceneNatives(dryad::Interpreter& interp, Audio::AudioService** audioPtr, SceneRequests* scenes) {
    auto audio = [audioPtr]() -> Audio::AudioService* { return audioPtr ? *audioPtr : nullptr; };

    defineNative(interp, "caffeine_audio_available", [=](const Args&) { auto* s = audio(); return dryad::Value(s && s->ready()); });
    defineNative(interp, "caffeine_audio_play", [=](const Args& a) {
        auto* s = audio();
        return handle(s ? s->play(str(a, 0), static_cast<f32>(num(a, 1, 1.0)), static_cast<f32>(num(a, 2, 0.0)), flag(a, 3)) : 0u); });
    defineNative(interp, "caffeine_audio_play_at", [=](const Args& a) {
        auto* s = audio();
        return handle(s ? s->playAt(str(a, 0), static_cast<f32>(num(a, 1)), static_cast<f32>(num(a, 2)),
                                    static_cast<f32>(num(a, 3, 500.0)), static_cast<f32>(num(a, 4, 1.0))) : 0u); });
    defineNative(interp, "caffeine_audio_stop", [=](const Args& a) { auto* s = audio(); return dryad::Value(s && s->stop(static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_audio_pause", [=](const Args& a) { auto* s = audio(); return dryad::Value(s && s->pause(static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_audio_resume", [=](const Args& a) { auto* s = audio(); return dryad::Value(s && s->resume(static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_audio_set_volume", [=](const Args& a) { auto* s = audio(); return dryad::Value(s && s->setVolume(static_cast<u32>(num(a, 0)), static_cast<f32>(num(a, 1)))); });
    defineNative(interp, "caffeine_audio_set_loop", [=](const Args& a) { auto* s = audio(); return dryad::Value(s && s->setLoop(static_cast<u32>(num(a, 0)), flag(a, 1))); });
    defineNative(interp, "caffeine_audio_is_playing", [=](const Args& a) { auto* s = audio(); return dryad::Value(s && s->isPlaying(static_cast<u32>(num(a, 0)))); });
    defineNative(interp, "caffeine_audio_stop_all", [=](const Args&) { if (auto* s = audio()) s->stopAll(); return dryad::Value(); });
    defineNative(interp, "caffeine_audio_set_master_volume", [=](const Args& a) { if (auto* s = audio()) s->setMasterVolume(static_cast<f32>(num(a, 0))); return dryad::Value(); });
    defineNative(interp, "caffeine_audio_master_volume", [=](const Args&) { auto* s = audio(); return dryad::Value(s ? static_cast<double>(s->masterVolume()) : 0.0); });
    defineNative(interp, "caffeine_audio_set_listener", [=](const Args& a) { if (auto* s = audio()) s->setListener(static_cast<f32>(num(a, 0)), static_cast<f32>(num(a, 1))); return dryad::Value(); });

    defineNative(interp, "caffeine_scene_load", [=](const Args& a) { if (scenes && !str(a, 0).empty()) scenes->requestLoad(str(a, 0)); return dryad::Value(); });
    defineNative(interp, "caffeine_scene_reload", [=](const Args&) { if (scenes) scenes->requestReload(); return dryad::Value(); });
    defineNative(interp, "caffeine_scene_current", [=](const Args&) { return dryad::Value(scenes ? scenes->current() : std::string()); });
    defineNative(interp, "caffeine_scene_is_loading", [=](const Args&) { return dryad::Value(scenes && scenes->hasPending()); });

    defineNative(interp, "caffeine_session_set", [](const Args& a) {
        auto& store = SessionStore::instance();
        const std::string key = str(a, 0);
        if (a.size() < 2) { store.remove(key); return dryad::Value(); }
        if (a[1].is_boolean()) store.setBoolean(key, a[1].as_boolean());
        else if (a[1].is_integer() || a[1].is_float()) store.setNumber(key, num(a, 1));
        else if (a[1].is_string()) store.setString(key, a[1].as_string());
        else store.remove(key);
        return dryad::Value(); });
    defineNative(interp, "caffeine_session_get", [](const Args& a) {
        const auto* v = SessionStore::instance().get(str(a, 0));
        if (!v) return dryad::Value();
        switch (v->kind) {
            case SessionStore::Value::Kind::Boolean: return dryad::Value(v->boolean);
            case SessionStore::Value::Kind::Number:
                // Whole numbers come back as integers so `get(k) == 3` works in Dryad.
                if (v->number == static_cast<double>(static_cast<int64_t>(v->number))) {
                    return dryad::Value(static_cast<int64_t>(v->number));
                }
                return dryad::Value(v->number);
            default: return dryad::Value(v->string);
        } });
    defineNative(interp, "caffeine_session_has", [](const Args& a) { return dryad::Value(SessionStore::instance().get(str(a, 0)) != nullptr); });
    defineNative(interp, "caffeine_session_remove", [](const Args& a) { return dryad::Value(SessionStore::instance().remove(str(a, 0))); });
    defineNative(interp, "caffeine_session_clear", [](const Args&) { SessionStore::instance().clear(); return dryad::Value(); });
}

}  // namespace Caffeine::Script
