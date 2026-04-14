#include "catch.hpp"
#include "../src/input/InputManager.hpp"

using namespace Caffeine;
using namespace Caffeine::Input;

// ============================================================================
// ActionState defaults
// ============================================================================

TEST_CASE("ActionState - Default is all false", "[input][action]") {
    InputManager mgr;
    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == false);
    REQUIRE(state.justPressed == false);
    REQUIRE(state.justReleased == false);
}

// ============================================================================
// Key press -> action state transitions
// ============================================================================

TEST_CASE("ActionState - justPressed on first frame of key down", "[input][action]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == true);
    REQUIRE(state.justPressed == true);
    REQUIRE(state.justReleased == false);
}

TEST_CASE("ActionState - pressed but not justPressed on second frame", "[input][action]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    // Frame 1: press
    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    // Frame 2: still held
    mgr.beginFrame();
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == true);
    REQUIRE(state.justPressed == false);
    REQUIRE(state.justReleased == false);
}

TEST_CASE("ActionState - justReleased on frame after key up", "[input][action]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    // Frame 1: press
    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    // Frame 2: release
    mgr.beginFrame();
    mgr.injectKeyUp(Key::Space);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == false);
    REQUIRE(state.justPressed == false);
    REQUIRE(state.justReleased == true);
}

TEST_CASE("ActionState - all false after release frame passes", "[input][action]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    // Frame 1: press
    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    // Frame 2: release
    mgr.beginFrame();
    mgr.injectKeyUp(Key::Space);
    mgr.endFrame();

    // Frame 3: idle
    mgr.beginFrame();
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == false);
    REQUIRE(state.justPressed == false);
    REQUIRE(state.justReleased == false);
}

// ============================================================================
// Multiple bindings per action
// ============================================================================

TEST_CASE("Action - Multiple bindings, either activates", "[input][binding]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));
    mgr.bind(Action::Jump, Binding::fromKey(Key::W));

    mgr.beginFrame();
    mgr.injectKeyDown(Key::W);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == true);
    REQUIRE(state.justPressed == true);
}

TEST_CASE("Action - Multiple bindings, both held, release one stays pressed", "[input][binding]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));
    mgr.bind(Action::Jump, Binding::fromKey(Key::W));

    // Frame 1: both down
    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.injectKeyDown(Key::W);
    mgr.endFrame();

    // Frame 2: release one
    mgr.beginFrame();
    mgr.injectKeyUp(Key::W);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == true);
    REQUIRE(state.justReleased == false);
}

// ============================================================================
// Mouse button bindings
// ============================================================================

TEST_CASE("Action - Mouse button binding", "[input][mouse]") {
    InputManager mgr;
    mgr.bind(Action::Attack, Binding::fromMouseButton(MouseButton::Left));

    mgr.beginFrame();
    mgr.injectMouseButtonDown(MouseButton::Left);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Attack);
    REQUIRE(state.pressed == true);
    REQUIRE(state.justPressed == true);
}

TEST_CASE("Mouse button release triggers justReleased", "[input][mouse]") {
    InputManager mgr;
    mgr.bind(Action::Attack, Binding::fromMouseButton(MouseButton::Left));

    mgr.beginFrame();
    mgr.injectMouseButtonDown(MouseButton::Left);
    mgr.endFrame();

    mgr.beginFrame();
    mgr.injectMouseButtonUp(MouseButton::Left);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Attack);
    REQUIRE(state.pressed == false);
    REQUIRE(state.justReleased == true);
}

// ============================================================================
// Gamepad button bindings
// ============================================================================

TEST_CASE("Action - Gamepad button binding", "[input][gamepad]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromGamepadButton(GamepadButton::A));

    mgr.beginFrame();
    mgr.injectGamepadButtonDown(GamepadButton::A);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == true);
    REQUIRE(state.justPressed == true);
}

// ============================================================================
// Mouse position and delta
// ============================================================================

TEST_CASE("Mouse position tracks injectMouseMove", "[input][mouse]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectMouseMove(100.0f, 200.0f);
    mgr.endFrame();

    Vec2 pos = mgr.mousePosition();
    REQUIRE(pos.x == Approx(100.0f));
    REQUIRE(pos.y == Approx(200.0f));
}

TEST_CASE("Mouse delta is difference between frames", "[input][mouse]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectMouseMove(100.0f, 200.0f);
    mgr.endFrame();

    mgr.beginFrame();
    mgr.injectMouseMove(150.0f, 220.0f);
    mgr.endFrame();

    Vec2 delta = mgr.mouseDelta();
    REQUIRE(delta.x == Approx(50.0f));
    REQUIRE(delta.y == Approx(20.0f));
}

// ============================================================================
// Axis state (digital keys -> axis value)
// ============================================================================

TEST_CASE("AxisState - Default is zero", "[input][axis]") {
    InputManager mgr;
    auto state = mgr.axisState(Axis::MoveX);
    REQUIRE(state.value == Approx(0.0f));
    REQUIRE(state.delta == Approx(0.0f));
}

TEST_CASE("AxisState - Positive binding gives +1.0", "[input][axis]") {
    InputManager mgr;
    mgr.bindAxis(Axis::MoveX,
                 Binding::fromKey(Key::A),   // negative
                 Binding::fromKey(Key::D));   // positive

    mgr.beginFrame();
    mgr.injectKeyDown(Key::D);
    mgr.endFrame();

    auto state = mgr.axisState(Axis::MoveX);
    REQUIRE(state.value == Approx(1.0f));
}

TEST_CASE("AxisState - Negative binding gives -1.0", "[input][axis]") {
    InputManager mgr;
    mgr.bindAxis(Axis::MoveX,
                 Binding::fromKey(Key::A),
                 Binding::fromKey(Key::D));

    mgr.beginFrame();
    mgr.injectKeyDown(Key::A);
    mgr.endFrame();

    auto state = mgr.axisState(Axis::MoveX);
    REQUIRE(state.value == Approx(-1.0f));
}

TEST_CASE("AxisState - Both keys cancel to 0.0", "[input][axis]") {
    InputManager mgr;
    mgr.bindAxis(Axis::MoveX,
                 Binding::fromKey(Key::A),
                 Binding::fromKey(Key::D));

    mgr.beginFrame();
    mgr.injectKeyDown(Key::A);
    mgr.injectKeyDown(Key::D);
    mgr.endFrame();

    auto state = mgr.axisState(Axis::MoveX);
    REQUIRE(state.value == Approx(0.0f));
}

TEST_CASE("AxisState - Gamepad axis value pass-through", "[input][axis]") {
    InputManager mgr;
    mgr.bindAxis(Axis::LookX,
                 Binding::fromGamepadAxis(GamepadAxis::RightX),
                 Binding::fromGamepadAxis(GamepadAxis::RightX));

    mgr.beginFrame();
    mgr.injectGamepadAxis(GamepadAxis::RightX, 0.75f);
    mgr.endFrame();

    auto state = mgr.axisState(Axis::LookX);
    REQUIRE(state.value == Approx(0.75f));
}

TEST_CASE("AxisState - Delta tracks change between frames", "[input][axis]") {
    InputManager mgr;
    mgr.bindAxis(Axis::MoveX,
                 Binding::fromKey(Key::A),
                 Binding::fromKey(Key::D));

    // Frame 1: idle
    mgr.beginFrame();
    mgr.endFrame();

    // Frame 2: press D -> value goes from 0 to 1
    mgr.beginFrame();
    mgr.injectKeyDown(Key::D);
    mgr.endFrame();

    auto state = mgr.axisState(Axis::MoveX);
    REQUIRE(state.delta == Approx(1.0f));
}

// ============================================================================
// Binding management
// ============================================================================

TEST_CASE("clearBindings removes action bindings", "[input][binding]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));
    mgr.clearBindings(Action::Jump);

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    auto state = mgr.actionState(Action::Jump);
    REQUIRE(state.pressed == false);
}

TEST_CASE("clearAllBindings removes all action bindings", "[input][binding]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));
    mgr.bind(Action::Attack, Binding::fromMouseButton(MouseButton::Left));
    mgr.clearAllBindings();

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.injectMouseButtonDown(MouseButton::Left);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::Jump).pressed == false);
    REQUIRE(mgr.actionState(Action::Attack).pressed == false);
}

TEST_CASE("resetToDefaults restores WASD + arrow defaults", "[input][binding]") {
    InputManager mgr;
    mgr.clearAllBindings();
    mgr.resetToDefaults();

    mgr.beginFrame();
    mgr.injectKeyDown(Key::W);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::MoveUp).pressed == true);
}

TEST_CASE("bind respects MAX_BINDINGS_PER_ACTION limit", "[input][binding]") {
    InputManager mgr;
    mgr.clearBindings(Action::Jump);

    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));
    mgr.bind(Action::Jump, Binding::fromKey(Key::W));
    mgr.bind(Action::Jump, Binding::fromKey(Key::Up));
    mgr.bind(Action::Jump, Binding::fromKey(Key::Return));
    // 5th binding should be silently ignored (MAX=4)
    mgr.bind(Action::Jump, Binding::fromKey(Key::A));

    mgr.beginFrame();
    mgr.injectKeyDown(Key::A);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::Jump).pressed == false);
}

// ============================================================================
// Callbacks (std::function)
// ============================================================================

TEST_CASE("onActionPressed callback fires on justPressed", "[input][callback]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    Action firedAction = Action::Count;
    mgr.onActionPressed = [&](Action a) { firedAction = a; };

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    REQUIRE(firedAction == Action::Jump);
}

TEST_CASE("onActionReleased callback fires on justReleased", "[input][callback]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    Action firedAction = Action::Count;
    mgr.onActionReleased = [&](Action a) { firedAction = a; };

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    mgr.beginFrame();
    mgr.injectKeyUp(Key::Space);
    mgr.endFrame();

    REQUIRE(firedAction == Action::Jump);
}

// ============================================================================
// Callbacks (virtual interface)
// ============================================================================

class TestInputHandler : public InputManager::IInputCallbacks {
public:
    Action lastPressed = Action::Count;
    Action lastReleased = Action::Count;
    int pressCount = 0;
    int releaseCount = 0;

    void onActionPressed(Action action) override {
        lastPressed = action;
        ++pressCount;
    }
    void onActionReleased(Action action) override {
        lastReleased = action;
        ++releaseCount;
    }
};

TEST_CASE("IInputCallbacks - onActionPressed fires via interface", "[input][callback]") {
    InputManager mgr;
    mgr.bind(Action::Attack, Binding::fromKey(Key::E));

    TestInputHandler handler;
    mgr.setCallbackHandler(&handler);

    mgr.beginFrame();
    mgr.injectKeyDown(Key::E);
    mgr.endFrame();

    REQUIRE(handler.lastPressed == Action::Attack);
    REQUIRE(handler.pressCount == 1);
}

TEST_CASE("IInputCallbacks - onActionReleased fires via interface", "[input][callback]") {
    InputManager mgr;
    mgr.bind(Action::Attack, Binding::fromKey(Key::E));

    TestInputHandler handler;
    mgr.setCallbackHandler(&handler);

    mgr.beginFrame();
    mgr.injectKeyDown(Key::E);
    mgr.endFrame();

    mgr.beginFrame();
    mgr.injectKeyUp(Key::E);
    mgr.endFrame();

    REQUIRE(handler.lastReleased == Action::Attack);
    REQUIRE(handler.releaseCount == 1);
}

TEST_CASE("Callbacks do not fire when no state transition", "[input][callback]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));

    TestInputHandler handler;
    mgr.setCallbackHandler(&handler);

    // Frame 1: press
    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();
    REQUIRE(handler.pressCount == 1);

    // Frame 2: still held - no new press callback
    mgr.beginFrame();
    mgr.endFrame();
    REQUIRE(handler.pressCount == 1);
}

// ============================================================================
// Independent actions don't interfere
// ============================================================================

TEST_CASE("Different actions are independent", "[input][action]") {
    InputManager mgr;
    mgr.bind(Action::Jump, Binding::fromKey(Key::Space));
    mgr.bind(Action::Attack, Binding::fromKey(Key::E));

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::Jump).pressed == true);
    REQUIRE(mgr.actionState(Action::Attack).pressed == false);
}

// ============================================================================
// Default bindings exist after construction
// ============================================================================

TEST_CASE("Default bindings - MoveUp bound to W", "[input][defaults]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectKeyDown(Key::W);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::MoveUp).pressed == true);
}

TEST_CASE("Default bindings - MoveDown bound to S", "[input][defaults]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectKeyDown(Key::S);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::MoveDown).pressed == true);
}

TEST_CASE("Default bindings - MoveLeft bound to A", "[input][defaults]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectKeyDown(Key::A);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::MoveLeft).pressed == true);
}

TEST_CASE("Default bindings - MoveRight bound to D", "[input][defaults]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectKeyDown(Key::D);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::MoveRight).pressed == true);
}

TEST_CASE("Default bindings - Jump bound to Space", "[input][defaults]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Space);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::Jump).pressed == true);
}

TEST_CASE("Default bindings - Pause bound to Escape", "[input][defaults]") {
    InputManager mgr;

    mgr.beginFrame();
    mgr.injectKeyDown(Key::Escape);
    mgr.endFrame();

    REQUIRE(mgr.actionState(Action::Pause).pressed == true);
}
