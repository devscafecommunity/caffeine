#pragma once

// Small host-side services scripts talk to. They hold no engine objects: a script only records
// a request (load a scene) or reads/writes plain data (the session store); the host acts on it
// at a safe point of its frame.

#include "core/Types.hpp"

#include <string>
#include <unordered_map>

namespace Caffeine::Script {

/// A scene change requested by a script. It is deferred: the world is replaced after the
/// frame's scripts have finished, never while one is running.
class SceneRequests {
public:
    void requestLoad(const std::string& path) { m_pending = path; m_hasPending = true; }
    void requestReload() { requestLoad(m_current); }
    bool hasPending() const { return m_hasPending; }
    /// Returns the requested path and clears the request.
    std::string take() { m_hasPending = false; return std::move(m_pending); }

    /// The host reports the scene that is loaded now (a project-relative or absolute path).
    void setCurrent(const std::string& path) { m_current = path; }
    const std::string& current() const { return m_current; }

private:
    std::string m_pending;
    std::string m_current;
    bool m_hasPending = false;
};

/// Values that survive scene changes for the lifetime of the process: score, settings, the
/// player's choices. Booleans, numbers and text only.
class SessionStore {
public:
    struct Value {
        enum class Kind { Number, Boolean, String } kind = Kind::Number;
        double number = 0.0;
        bool boolean = false;
        std::string string;
    };
    static SessionStore& instance() { static SessionStore s; return s; }

    void setNumber(const std::string& k, double v) { Value x; x.kind = Value::Kind::Number; x.number = v; m_values[k] = x; }
    void setBoolean(const std::string& k, bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; m_values[k] = x; }
    void setString(const std::string& k, const std::string& v) { Value x; x.kind = Value::Kind::String; x.string = v; m_values[k] = x; }
    const Value* get(const std::string& k) const {
        auto it = m_values.find(k);
        return it == m_values.end() ? nullptr : &it->second;
    }
    bool remove(const std::string& k) { return m_values.erase(k) > 0; }
    void clear() { m_values.clear(); }

private:
    std::unordered_map<std::string, Value> m_values;
};

}  // namespace Caffeine::Script
