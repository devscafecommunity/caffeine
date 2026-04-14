// ============================================================================
// @file    InputManager.hpp
// @brief   Action-based input system with remappable bindings (RF2.9)
// @note    Part of input/ module - no SDL3 dependency at compile time
// ============================================================================
#pragma once

#include "../core/Types.hpp"
#include "../math/Vec2.hpp"
#include "../containers/HashMap.hpp"
#include "../containers/Vector.hpp"

#include <array>
#include <functional>

namespace Caffeine::Input {

// ============================================================================
// Enums
// ============================================================================

enum class Action : u8 {
    MoveUp = 0,
    MoveDown,
    MoveLeft,
    MoveRight,
    Jump,
    Attack,
    Interact,
    Pause,
    Count
};

enum class Axis : u8 {
    MoveX = 0,
    MoveY,
    LookX,
    LookY,
    Count
};

// ============================================================================
// Key / Button codes (SDL3-compatible values, no SDL3 header dependency)
// ============================================================================

enum class Key : u16 {
    Unknown = 0,
    A = 4, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,
    Return = 40, Escape, Backspace, Tab, Space,
    Up = 82, Down, Left = 80, Right,
    LShift = 225, RShift, LCtrl = 224, RCtrl,
    LAlt = 226, RAlt,
    KeyCount = 512
};

enum class MouseButton : u8 {
    Left = 1,
    Middle,
    Right,
    X1,
    X2,
    Count
};

enum class GamepadButton : u8 {
    A = 0, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start, Guide,
    LeftStick, RightStick,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    Count
};

enum class GamepadAxis : u8 {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    TriggerLeft,
    TriggerRight,
    Count
};

// ============================================================================
// State structs
// ============================================================================

struct ActionState {
    bool pressed      = false;
    bool justPressed  = false;
    bool justReleased = false;
};

struct AxisState {
    f32 value = 0.0f;
    f32 delta = 0.0f;
};

// ============================================================================
// Binding - what physical input maps to a logical action/axis
// ============================================================================

enum class BindingType : u8 {
    Key,
    MouseButton,
    GamepadButton,
    GamepadAxis
};

struct Binding {
    BindingType type;
    union {
        Key             key;
        MouseButton     mouseButton;
        GamepadButton   gamepadButton;
        GamepadAxis     gamepadAxis;
    };

    static Binding fromKey(Key k) {
        Binding b{};
        b.type = BindingType::Key;
        b.key = k;
        return b;
    }

    static Binding fromMouseButton(MouseButton mb) {
        Binding b{};
        b.type = BindingType::MouseButton;
        b.mouseButton = mb;
        return b;
    }

    static Binding fromGamepadButton(GamepadButton gb) {
        Binding b{};
        b.type = BindingType::GamepadButton;
        b.gamepadButton = gb;
        return b;
    }

    static Binding fromGamepadAxis(GamepadAxis ga) {
        Binding b{};
        b.type = BindingType::GamepadAxis;
        b.gamepadAxis = ga;
        return b;
    }

    bool operator==(const Binding& other) const {
        if (type != other.type) return false;
        switch (type) {
            case BindingType::Key:           return key == other.key;
            case BindingType::MouseButton:   return mouseButton == other.mouseButton;
            case BindingType::GamepadButton: return gamepadButton == other.gamepadButton;
            case BindingType::GamepadAxis:   return gamepadAxis == other.gamepadAxis;
        }
        return false;
    }
};

// ============================================================================
// InputManager
// ============================================================================

static constexpr usize MAX_BINDINGS_PER_ACTION = 4;

class InputManager {
public:
    InputManager();

    // --- Frame lifecycle (called by GameLoop) ---
    void beginFrame();
    void endFrame();

    // --- Event injection (testable without SDL3) ---
    void injectKeyDown(Key key);
    void injectKeyUp(Key key);
    void injectMouseButtonDown(MouseButton button);
    void injectMouseButtonUp(MouseButton button);
    void injectMouseMove(f32 x, f32 y);
    void injectGamepadButtonDown(GamepadButton button);
    void injectGamepadButtonUp(GamepadButton button);
    void injectGamepadAxis(GamepadAxis axis, f32 value);

    // --- Query state ---
    ActionState actionState(Action action) const;
    AxisState   axisState(Axis axis) const;
    Vec2        mousePosition() const;
    Vec2        mouseDelta() const;

    // --- Binding management ---
    void bind(Action action, Binding binding);
    void clearBindings(Action action);
    void clearAllBindings();
    void resetToDefaults();

    // --- Axis binding ---
    void bindAxis(Axis axis, Binding negativeBinding, Binding positiveBinding);

    // --- Callbacks (std::function interface) ---
    std::function<void(Action)> onActionPressed;
    std::function<void(Action)> onActionReleased;

    // --- Virtual callback interface ---
    class IInputCallbacks {
    public:
        virtual ~IInputCallbacks() = default;
        virtual void onActionPressed(Action action) = 0;
        virtual void onActionReleased(Action action) = 0;
    };
    void setCallbackHandler(IInputCallbacks* handler);

private:
    void setupDefaultBindings();
    bool isBindingActive(const Binding& binding) const;
    bool wasBindingActive(const Binding& binding) const;
    void fireActionPressed(Action action);
    void fireActionReleased(Action action);

    // --- Raw input state ---
    std::array<bool, static_cast<usize>(Key::KeyCount)>           m_keyState{};
    std::array<bool, static_cast<usize>(MouseButton::Count)>      m_mouseState{};
    std::array<bool, static_cast<usize>(GamepadButton::Count)>    m_gamepadButtonState{};
    std::array<f32,  static_cast<usize>(GamepadAxis::Count)>      m_gamepadAxisState{};

    // --- Previous frame raw state (for justPressed/justReleased) ---
    std::array<bool, static_cast<usize>(Key::KeyCount)>           m_prevKeyState{};
    std::array<bool, static_cast<usize>(MouseButton::Count)>      m_prevMouseState{};
    std::array<bool, static_cast<usize>(GamepadButton::Count)>    m_prevGamepadButtonState{};

    // --- Action bindings: Action -> up to MAX_BINDINGS_PER_ACTION bindings ---
    struct ActionBindings {
        std::array<Binding, MAX_BINDINGS_PER_ACTION> bindings{};
        u8 count = 0;
    };
    std::array<ActionBindings, static_cast<usize>(Action::Count)> m_actionBindings{};

    // --- Axis bindings: Axis -> negative/positive binding pair ---
    struct AxisBindingPair {
        Binding negative;
        Binding positive;
        bool bound = false;
    };
    std::array<AxisBindingPair, static_cast<usize>(Axis::Count)> m_axisBindings{};

    // --- Cached action states ---
    std::array<ActionState, static_cast<usize>(Action::Count)> m_actionStates{};
    std::array<ActionState, static_cast<usize>(Action::Count)> m_prevActionStates{};

    // --- Mouse ---
    Vec2 m_mousePos{0.0f, 0.0f};
    Vec2 m_prevMousePos{0.0f, 0.0f};

    // --- Callback handler ---
    IInputCallbacks* m_callbackHandler = nullptr;
};

} // namespace Caffeine::Input
