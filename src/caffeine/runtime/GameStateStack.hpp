#pragma once

#include "core/Types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Caffeine::Runtime {

/// Optional push/pop scene or mode stack (domain-agnostic).
class GameStateStack {
public:
    static GameStateStack& instance();

    void clear();
    void push(const std::string& id);
    bool pop();
    bool pauseTop();
    bool resumeTop();

    const std::string& top() const;
    bool empty() const { return m_stack.empty(); }
    usize depth() const { return m_stack.size(); }

    using TransitionFn = std::function<void(const std::string& from, const std::string& to)>;
    void setOnTransition(TransitionFn fn);

private:
    struct Entry {
        std::string id;
        bool paused = false;
    };
    std::vector<Entry> m_stack;
    TransitionFn m_onTransition;
};

}  // namespace Caffeine::Runtime
