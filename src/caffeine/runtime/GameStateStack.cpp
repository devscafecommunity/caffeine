#include "runtime/GameStateStack.hpp"

namespace Caffeine::Runtime {

GameStateStack& GameStateStack::instance() {
    static GameStateStack s;
    return s;
}

void GameStateStack::clear() {
    m_stack.clear();
}

void GameStateStack::push(const std::string& id) {
    const std::string from = m_stack.empty() ? std::string() : m_stack.back().id;
    m_stack.push_back({id, false});
    if (m_onTransition) m_onTransition(from, id);
}

bool GameStateStack::pop() {
    if (m_stack.empty()) return false;
    const std::string from = m_stack.back().id;
    m_stack.pop_back();
    const std::string to = m_stack.empty() ? std::string() : m_stack.back().id;
    if (m_onTransition) m_onTransition(from, to);
    return true;
}

bool GameStateStack::pauseTop() {
    if (m_stack.empty()) return false;
    m_stack.back().paused = true;
    return true;
}

bool GameStateStack::resumeTop() {
    if (m_stack.empty()) return false;
    m_stack.back().paused = false;
    return true;
}

const std::string& GameStateStack::top() const {
    static const std::string kEmpty;
    return m_stack.empty() ? kEmpty : m_stack.back().id;
}

void GameStateStack::setOnTransition(TransitionFn fn) {
    m_onTransition = std::move(fn);
}

}  // namespace Caffeine::Runtime
