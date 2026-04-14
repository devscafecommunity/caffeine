#include "InputManager.hpp"

namespace Caffeine::Input {

InputManager::InputManager() {
    m_keyState.fill(false);
    m_mouseState.fill(false);
    m_gamepadButtonState.fill(false);
    m_gamepadAxisState.fill(0.0f);
    m_prevKeyState.fill(false);
    m_prevMouseState.fill(false);
    m_prevGamepadButtonState.fill(false);
    setupDefaultBindings();
}

void InputManager::beginFrame() {
    m_prevKeyState = m_keyState;
    m_prevMouseState = m_mouseState;
    m_prevGamepadButtonState = m_gamepadButtonState;
    m_prevMousePos = m_mousePos;
    m_prevActionStates = m_actionStates;
}

void InputManager::endFrame() {
    for (usize i = 0; i < static_cast<usize>(Action::Count); ++i) {
        auto action = static_cast<Action>(i);
        const auto& bindings = m_actionBindings[i];

        bool active = false;
        for (u8 b = 0; b < bindings.count; ++b) {
            if (isBindingActive(bindings.bindings[b])) {
                active = true;
                break;
            }
        }

        bool wasActive = m_prevActionStates[i].pressed;

        ActionState& state = m_actionStates[i];
        state.pressed      = active;
        state.justPressed  = active && !wasActive;
        state.justReleased = !active && wasActive;

        if (state.justPressed)  fireActionPressed(action);
        if (state.justReleased) fireActionReleased(action);
    }
}

void InputManager::injectKeyDown(Key key) {
    auto idx = static_cast<usize>(key);
    if (idx < m_keyState.size()) m_keyState[idx] = true;
}

void InputManager::injectKeyUp(Key key) {
    auto idx = static_cast<usize>(key);
    if (idx < m_keyState.size()) m_keyState[idx] = false;
}

void InputManager::injectMouseButtonDown(MouseButton button) {
    auto idx = static_cast<usize>(button);
    if (idx < m_mouseState.size()) m_mouseState[idx] = true;
}

void InputManager::injectMouseButtonUp(MouseButton button) {
    auto idx = static_cast<usize>(button);
    if (idx < m_mouseState.size()) m_mouseState[idx] = false;
}

void InputManager::injectMouseMove(f32 x, f32 y) {
    m_mousePos = Vec2(x, y);
}

void InputManager::injectGamepadButtonDown(GamepadButton button) {
    auto idx = static_cast<usize>(button);
    if (idx < m_gamepadButtonState.size()) m_gamepadButtonState[idx] = true;
}

void InputManager::injectGamepadButtonUp(GamepadButton button) {
    auto idx = static_cast<usize>(button);
    if (idx < m_gamepadButtonState.size()) m_gamepadButtonState[idx] = false;
}

void InputManager::injectGamepadAxis(GamepadAxis axis, f32 value) {
    auto idx = static_cast<usize>(axis);
    if (idx < m_gamepadAxisState.size()) m_gamepadAxisState[idx] = value;
}

ActionState InputManager::actionState(Action action) const {
    auto idx = static_cast<usize>(action);
    if (idx < m_actionStates.size()) return m_actionStates[idx];
    return {};
}

AxisState InputManager::axisState(Axis axis) const {
    auto idx = static_cast<usize>(axis);
    if (idx >= static_cast<usize>(Axis::Count)) return {};

    const auto& pair = m_axisBindings[idx];
    if (!pair.bound) return {};

    f32 value = 0.0f;

    if (pair.negative.type == BindingType::GamepadAxis &&
        pair.positive.type == BindingType::GamepadAxis &&
        pair.negative == pair.positive) {
        auto axisIdx = static_cast<usize>(pair.negative.gamepadAxis);
        if (axisIdx < m_gamepadAxisState.size()) {
            value = m_gamepadAxisState[axisIdx];
        }
    } else {
        f32 neg = isBindingActive(pair.negative) ? -1.0f : 0.0f;
        f32 pos = isBindingActive(pair.positive) ?  1.0f : 0.0f;
        value = neg + pos;
    }

    f32 prevValue = 0.0f;
    if (pair.negative.type == BindingType::GamepadAxis &&
        pair.positive.type == BindingType::GamepadAxis &&
        pair.negative == pair.positive) {
        prevValue = value;
    } else {
        f32 prevNeg = wasBindingActive(pair.negative) ? -1.0f : 0.0f;
        f32 prevPos = wasBindingActive(pair.positive) ?  1.0f : 0.0f;
        prevValue = prevNeg + prevPos;
    }

    AxisState state;
    state.value = value;
    state.delta = value - prevValue;
    return state;
}

Vec2 InputManager::mousePosition() const {
    return m_mousePos;
}

Vec2 InputManager::mouseDelta() const {
    return m_mousePos - m_prevMousePos;
}

void InputManager::bind(Action action, Binding binding) {
    auto idx = static_cast<usize>(action);
    if (idx >= static_cast<usize>(Action::Count)) return;

    auto& ab = m_actionBindings[idx];
    if (ab.count >= MAX_BINDINGS_PER_ACTION) return;
    ab.bindings[ab.count] = binding;
    ++ab.count;
}

void InputManager::clearBindings(Action action) {
    auto idx = static_cast<usize>(action);
    if (idx >= static_cast<usize>(Action::Count)) return;
    m_actionBindings[idx].count = 0;
}

void InputManager::clearAllBindings() {
    for (usize i = 0; i < static_cast<usize>(Action::Count); ++i) {
        m_actionBindings[i].count = 0;
    }
    for (usize i = 0; i < static_cast<usize>(Axis::Count); ++i) {
        m_axisBindings[i].bound = false;
    }
}

void InputManager::resetToDefaults() {
    clearAllBindings();
    setupDefaultBindings();
}

void InputManager::bindAxis(Axis axis, Binding negative, Binding positive) {
    auto idx = static_cast<usize>(axis);
    if (idx >= static_cast<usize>(Axis::Count)) return;
    m_axisBindings[idx].negative = negative;
    m_axisBindings[idx].positive = positive;
    m_axisBindings[idx].bound = true;
}

void InputManager::setCallbackHandler(IInputCallbacks* handler) {
    m_callbackHandler = handler;
}

void InputManager::setupDefaultBindings() {
    bind(Action::MoveUp,    Binding::fromKey(Key::W));
    bind(Action::MoveDown,  Binding::fromKey(Key::S));
    bind(Action::MoveLeft,  Binding::fromKey(Key::A));
    bind(Action::MoveRight, Binding::fromKey(Key::D));
    bind(Action::Jump,      Binding::fromKey(Key::Space));
    bind(Action::Pause,     Binding::fromKey(Key::Escape));

    bind(Action::MoveUp,    Binding::fromKey(Key::Up));
    bind(Action::MoveDown,  Binding::fromKey(Key::Down));
    bind(Action::MoveLeft,  Binding::fromKey(Key::Left));
    bind(Action::MoveRight, Binding::fromKey(Key::Right));
}

bool InputManager::isBindingActive(const Binding& binding) const {
    switch (binding.type) {
        case BindingType::Key: {
            auto idx = static_cast<usize>(binding.key);
            return idx < m_keyState.size() && m_keyState[idx];
        }
        case BindingType::MouseButton: {
            auto idx = static_cast<usize>(binding.mouseButton);
            return idx < m_mouseState.size() && m_mouseState[idx];
        }
        case BindingType::GamepadButton: {
            auto idx = static_cast<usize>(binding.gamepadButton);
            return idx < m_gamepadButtonState.size() && m_gamepadButtonState[idx];
        }
        case BindingType::GamepadAxis: {
            auto idx = static_cast<usize>(binding.gamepadAxis);
            return idx < m_gamepadAxisState.size() && m_gamepadAxisState[idx] > 0.5f;
        }
    }
    return false;
}

bool InputManager::wasBindingActive(const Binding& binding) const {
    switch (binding.type) {
        case BindingType::Key: {
            auto idx = static_cast<usize>(binding.key);
            return idx < m_prevKeyState.size() && m_prevKeyState[idx];
        }
        case BindingType::MouseButton: {
            auto idx = static_cast<usize>(binding.mouseButton);
            return idx < m_prevMouseState.size() && m_prevMouseState[idx];
        }
        case BindingType::GamepadButton: {
            auto idx = static_cast<usize>(binding.gamepadButton);
            return idx < m_prevGamepadButtonState.size() && m_prevGamepadButtonState[idx];
        }
        case BindingType::GamepadAxis:
            return false;
    }
    return false;
}

void InputManager::fireActionPressed(Action action) {
    if (onActionPressed) onActionPressed(action);
    if (m_callbackHandler) m_callbackHandler->onActionPressed(action);
}

void InputManager::fireActionReleased(Action action) {
    if (onActionReleased) onActionReleased(action);
    if (m_callbackHandler) m_callbackHandler->onActionReleased(action);
}

} // namespace Caffeine::Input
