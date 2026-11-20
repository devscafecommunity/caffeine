//! Scripting API reference used by the code editor's hover tooltip.
//!
//! One table per language (Lua `caffeine.*`, Dryad `caffeine_*`). A test reads the
//! C++ binding sources and fails when a Dryad native has no entry here, so the
//! reference cannot drift from the engine.

/// The scripting language a document is written in.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Language {
    Lua,
    Dryad,
}

/// One documented function or constant.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Entry {
    pub language: Language,
    /// Full name: `caffeine.math.cross` or `caffeine_time_delta`.
    pub name: &'static str,
    /// Call shape with argument names and result.
    pub signature: &'static str,
    pub doc: &'static str,
}

const fn lua(name: &'static str, signature: &'static str, doc: &'static str) -> Entry {
    Entry {
        language: Language::Lua,
        name,
        signature,
        doc,
    }
}

const fn dyd(name: &'static str, signature: &'static str, doc: &'static str) -> Entry {
    Entry {
        language: Language::Dryad,
        name,
        signature,
        doc,
    }
}

pub const ENTRIES: &[Entry] = &[
    // ── Lua: time ───────────────────────────────────────────────────────
    lua(
        "caffeine.time.delta",
        "caffeine.time.delta() -> number",
        "Scaled seconds since the previous frame.",
    ),
    lua(
        "caffeine.time.unscaledDelta",
        "caffeine.time.unscaledDelta() -> number",
        "Seconds since the previous frame, ignoring the time scale.",
    ),
    lua(
        "caffeine.time.time",
        "caffeine.time.time() -> number",
        "Scaled seconds since play started.",
    ),
    lua(
        "caffeine.time.fixedDelta",
        "caffeine.time.fixedDelta() -> number",
        "Length of one fixed simulation step.",
    ),
    lua(
        "caffeine.time.timeScale",
        "caffeine.time.timeScale() -> number",
        "Current time scale (1 = normal).",
    ),
    lua(
        "caffeine.time.setTimeScale",
        "caffeine.time.setTimeScale(scale)",
        "Slow down or speed up the game clock.",
    ),
    lua(
        "caffeine.time.paused",
        "caffeine.time.paused() -> boolean",
        "Whether the play clock is paused.",
    ),
    lua(
        "caffeine.time.setPaused",
        "caffeine.time.setPaused(paused)",
        "Pause or resume the play clock.",
    ),
    lua(
        "caffeine.time.frame",
        "caffeine.time.frame() -> number",
        "Frame counter since play started.",
    ),
    lua(
        "caffeine.time.seed",
        "caffeine.time.seed() -> number",
        "The deterministic random seed.",
    ),
    lua(
        "caffeine.time.setSeed",
        "caffeine.time.setSeed(seed)",
        "Set the deterministic random seed.",
    ),
    // ── Lua: input ──────────────────────────────────────────────────────
    lua(
        "caffeine.input.isKeyDown",
        "caffeine.input.isKeyDown(key) -> boolean",
        "True while the key is held. Names: `Space`, `W`, `LShift`, `Escape`, arrows…",
    ),
    lua(
        "caffeine.input.getAxis",
        "caffeine.input.getAxis(axis) -> number",
        "Axis value in -1..1: `Horizontal`, `Vertical`, `LookX`, `LookY`.",
    ),
    lua(
        "caffeine.input.isActionDown",
        "caffeine.input.isActionDown(action) -> boolean",
        "True while the action is held: `Jump`, `Attack`, `Interact`, `MoveUp`…",
    ),
    lua(
        "caffeine.input.isActionPressed",
        "caffeine.input.isActionPressed(action) -> boolean",
        "True only on the frame the action was pressed.",
    ),
    lua(
        "caffeine.input.mouseDelta",
        "caffeine.input.mouseDelta() -> dx, dy",
        "Mouse movement since the previous frame.",
    ),
    lua(
        "caffeine.input.mousePosition",
        "caffeine.input.mousePosition() -> x, y",
        "Mouse position in screen pixels.",
    ),
    lua(
        "caffeine.input.isMouseButtonDown",
        "caffeine.input.isMouseButtonDown(button) -> boolean",
        "1 = left, 2 = middle, 3 = right.",
    ),
    // ── Lua: world ──────────────────────────────────────────────────────
    lua(
        "caffeine.world.create",
        "caffeine.world.create() -> entityId",
        "Create an empty entity.",
    ),
    lua(
        "caffeine.world.destroy",
        "caffeine.world.destroy(entityId)",
        "Destroy an entity.",
    ),
    lua(
        "caffeine.world.hasComponent",
        "caffeine.world.hasComponent(entityId, type) -> boolean",
        "Check for `Transform`, `Sprite`, `RigidBody2D` or `Collider2D`.",
    ),
    lua(
        "caffeine.world.getTransform",
        "caffeine.world.getTransform(entityId) -> {x, y, z, rotation, scaleX, scaleY}",
        "Read the entity transform.",
    ),
    lua(
        "caffeine.world.setTransform",
        "caffeine.world.setTransform(entityId, {x, y, z, rotation, scaleX, scaleY})",
        "Write the given transform fields.",
    ),
    lua(
        "caffeine.world.addTransform",
        "caffeine.world.addTransform(entityId, {x, y, z})",
        "Move the entity by an offset.",
    ),
    lua(
        "caffeine.world.distanceTo",
        "caffeine.world.distanceTo(a, b) -> number",
        "Distance between two entities; 1e9 when either has no position.",
    ),
    lua(
        "caffeine.world.findNearest",
        "caffeine.world.findNearest(entityId, maxDistance) -> entityId",
        "Closest other entity within the distance, or 0.",
    ),
    lua(
        "caffeine.world.setYawPitch",
        "caffeine.world.setYawPitch(entityId, yaw, pitch)",
        "Orient an entity from yaw and pitch (radians).",
    ),
    lua(
        "caffeine.world.getVelocity",
        "caffeine.world.getVelocity(entityId) -> {x, y}",
        "2D rigid-body velocity.",
    ),
    lua(
        "caffeine.world.setVelocity",
        "caffeine.world.setVelocity(entityId, {x, y})",
        "Set the 2D rigid-body velocity.",
    ),
    // ── Lua: math ───────────────────────────────────────────────────────
    lua("caffeine.math.pi", "caffeine.math.pi", "The constant pi."),
    lua(
        "caffeine.math.epsilon",
        "caffeine.math.epsilon",
        "A small tolerance for comparisons (1e-5).",
    ),
    lua(
        "caffeine.math.vec2",
        "caffeine.math.vec2(x, y) -> {x, y}",
        "Build a 2D vector.",
    ),
    lua(
        "caffeine.math.vec3",
        "caffeine.math.vec3(x, y, z) -> {x, y, z}",
        "Build a 3D vector.",
    ),
    lua(
        "caffeine.math.add",
        "caffeine.math.add(a, b) -> vec",
        "Component-wise sum; the result has `z` only when an operand does.",
    ),
    lua(
        "caffeine.math.sub",
        "caffeine.math.sub(a, b) -> vec",
        "Component-wise difference.",
    ),
    lua(
        "caffeine.math.mul",
        "caffeine.math.mul(a, b) -> vec",
        "Component-wise product.",
    ),
    lua(
        "caffeine.math.scale",
        "caffeine.math.scale(v, s) -> vec",
        "Multiply every component by a number.",
    ),
    lua(
        "caffeine.math.dot",
        "caffeine.math.dot(a, b) -> number",
        "Dot product.",
    ),
    lua(
        "caffeine.math.cross",
        "caffeine.math.cross(a, b) -> vec3",
        "Cross product of two 3D vectors.",
    ),
    lua(
        "caffeine.math.length",
        "caffeine.math.length(v) -> number",
        "Vector length.",
    ),
    lua(
        "caffeine.math.lengthSquared",
        "caffeine.math.lengthSquared(v) -> number",
        "Squared length (no square root).",
    ),
    lua(
        "caffeine.math.distance",
        "caffeine.math.distance(a, b) -> number",
        "Distance between two points.",
    ),
    lua(
        "caffeine.math.normalize",
        "caffeine.math.normalize(v) -> vec",
        "Unit vector; zero stays zero.",
    ),
    lua(
        "caffeine.math.lerpVec",
        "caffeine.math.lerpVec(a, b, t) -> vec",
        "Linear interpolation between two vectors.",
    ),
    lua(
        "caffeine.math.clamp",
        "caffeine.math.clamp(v, lo, hi) -> number",
        "Limit a value to a range.",
    ),
    lua(
        "caffeine.math.clamp01",
        "caffeine.math.clamp01(v) -> number",
        "Limit a value to 0..1.",
    ),
    lua(
        "caffeine.math.lerp",
        "caffeine.math.lerp(a, b, t) -> number",
        "Linear interpolation.",
    ),
    lua(
        "caffeine.math.inverseLerp",
        "caffeine.math.inverseLerp(a, b, v) -> number",
        "Where `v` sits between `a` and `b` (0..1 inside).",
    ),
    lua(
        "caffeine.math.remap",
        "caffeine.math.remap(v, inLo, inHi, outLo, outHi) -> number",
        "Map a value from one range to another.",
    ),
    lua(
        "caffeine.math.smoothstep",
        "caffeine.math.smoothstep(edge0, edge1, v) -> number",
        "Smooth 0..1 ramp between two edges.",
    ),
    lua(
        "caffeine.math.approach",
        "caffeine.math.approach(current, target, step) -> number",
        "Move toward a target by at most `step`.",
    ),
    lua(
        "caffeine.math.sign",
        "caffeine.math.sign(v) -> number",
        "-1, 0 or 1.",
    ),
    lua(
        "caffeine.math.radians",
        "caffeine.math.radians(deg) -> number",
        "Degrees to radians.",
    ),
    lua(
        "caffeine.math.degrees",
        "caffeine.math.degrees(rad) -> number",
        "Radians to degrees.",
    ),
    // ── Lua: debug, events, components ──────────────────────────────────
    lua(
        "caffeine.debug.log",
        "caffeine.debug.log(message)",
        "Write an info line to the Console.",
    ),
    lua(
        "caffeine.debug.warn",
        "caffeine.debug.warn(message)",
        "Write a warning to the Console.",
    ),
    lua(
        "caffeine.debug.error",
        "caffeine.debug.error(message)",
        "Write an error to the Console.",
    ),
    lua(
        "caffeine.events.on",
        "caffeine.events.on(name, handler) -> handle",
        "Subscribe to an event; keep the handle to unsubscribe.",
    ),
    lua(
        "caffeine.events.emit",
        "caffeine.events.emit(name, payload)",
        "Raise an event for every subscriber.",
    ),
    lua(
        "caffeine.events.off",
        "caffeine.events.off(handle)",
        "Remove a subscription.",
    ),
    lua(
        "caffeine.component.list",
        "caffeine.component.list() -> {names}",
        "Every component the reflection registry knows.",
    ),
    lua(
        "caffeine.component.names",
        "caffeine.component.names(entityId) -> {names}",
        "Components this entity has.",
    ),
    lua(
        "caffeine.component.has",
        "caffeine.component.has(entityId, name) -> boolean",
        "Whether the entity has the component.",
    ),
    lua(
        "caffeine.component.add",
        "caffeine.component.add(entityId, name)",
        "Add a component with default values.",
    ),
    lua(
        "caffeine.component.remove",
        "caffeine.component.remove(entityId, name)",
        "Remove a component.",
    ),
    lua(
        "caffeine.component.get",
        "caffeine.component.get(entityId, name) -> table",
        "All fields of a component as a table.",
    ),
    lua(
        "caffeine.component.getField",
        "caffeine.component.getField(entityId, name, field) -> value",
        "Read one field.",
    ),
    lua(
        "caffeine.component.setField",
        "caffeine.component.setField(entityId, name, field, value)",
        "Write one field.",
    ),
    lua(
        "caffeine.component.set",
        "caffeine.component.set(entityId, name, table)",
        "Add the component and write the given fields.",
    ),
    // ── Dryad: transform and log ────────────────────────────────────────
    dyd(
        "caffeine_set_position",
        "caffeine_set_position(entity, x, y, z)",
        "Move the entity to an absolute position.",
    ),
    dyd(
        "caffeine_translate",
        "caffeine_translate(entity, dx, dy, dz)",
        "Move the entity by an offset.",
    ),
    dyd(
        "caffeine_get_position_x",
        "caffeine_get_position_x(entity) -> number",
        "Entity X position.",
    ),
    dyd(
        "caffeine_get_position_y",
        "caffeine_get_position_y(entity) -> number",
        "Entity Y position.",
    ),
    dyd(
        "caffeine_get_position_z",
        "caffeine_get_position_z(entity) -> number",
        "Entity Z position.",
    ),
    dyd(
        "caffeine_set_scale",
        "caffeine_set_scale(entity, x, y, z)",
        "Set the entity scale.",
    ),
    dyd(
        "caffeine_get_scale_x",
        "caffeine_get_scale_x(entity) -> number",
        "Entity X scale.",
    ),
    dyd(
        "caffeine_get_scale_y",
        "caffeine_get_scale_y(entity) -> number",
        "Entity Y scale.",
    ),
    dyd(
        "caffeine_get_scale_z",
        "caffeine_get_scale_z(entity) -> number",
        "Entity Z scale.",
    ),
    dyd(
        "caffeine_log",
        "caffeine_log(message)",
        "Write a message to the Console.",
    ),
    // ── Dryad: time ─────────────────────────────────────────────────────
    dyd(
        "caffeine_time_delta",
        "caffeine_time_delta() -> number",
        "Scaled seconds since the previous frame.",
    ),
    dyd(
        "caffeine_time_unscaled_delta",
        "caffeine_time_unscaled_delta() -> number",
        "Seconds since the previous frame, ignoring the time scale.",
    ),
    dyd(
        "caffeine_time_total",
        "caffeine_time_total() -> number",
        "Scaled seconds since play started.",
    ),
    dyd(
        "caffeine_time_fixed_delta",
        "caffeine_time_fixed_delta() -> number",
        "Length of one fixed simulation step.",
    ),
    dyd(
        "caffeine_time_scale",
        "caffeine_time_scale() -> number",
        "Current time scale (1 = normal).",
    ),
    dyd(
        "caffeine_time_frame",
        "caffeine_time_frame() -> integer",
        "Frame counter since play started.",
    ),
    dyd(
        "caffeine_time_set_scale",
        "caffeine_time_set_scale(scale)",
        "Slow down or speed up the game clock.",
    ),
    dyd(
        "caffeine_time_paused",
        "caffeine_time_paused() -> bool",
        "Whether the play clock is paused.",
    ),
    dyd(
        "caffeine_time_set_paused",
        "caffeine_time_set_paused(paused)",
        "Pause or resume the play clock.",
    ),
    // ── Dryad: input ────────────────────────────────────────────────────
    dyd(
        "caffeine_input_key_down",
        "caffeine_input_key_down(key) -> bool",
        "True while the key is held. Names: \"Space\", \"W\", \"LShift\"…",
    ),
    dyd(
        "caffeine_input_axis",
        "caffeine_input_axis(axis) -> number",
        "Axis in -1..1: \"Horizontal\", \"Vertical\", \"LookX\", \"LookY\".",
    ),
    dyd(
        "caffeine_input_action_down",
        "caffeine_input_action_down(action) -> bool",
        "True while the action is held: \"Jump\", \"Attack\"…",
    ),
    dyd(
        "caffeine_input_action_pressed",
        "caffeine_input_action_pressed(action) -> bool",
        "True only on the frame the action was pressed.",
    ),
    dyd(
        "caffeine_input_mouse_x",
        "caffeine_input_mouse_x() -> number",
        "Mouse X in screen pixels.",
    ),
    dyd(
        "caffeine_input_mouse_y",
        "caffeine_input_mouse_y() -> number",
        "Mouse Y in screen pixels.",
    ),
    dyd(
        "caffeine_input_mouse_dx",
        "caffeine_input_mouse_dx() -> number",
        "Mouse X movement since the previous frame.",
    ),
    dyd(
        "caffeine_input_mouse_dy",
        "caffeine_input_mouse_dy() -> number",
        "Mouse Y movement since the previous frame.",
    ),
    dyd(
        "caffeine_input_mouse_down",
        "caffeine_input_mouse_down(button) -> bool",
        "1 = left, 2 = middle, 3 = right.",
    ),
    // ── Dryad: world ────────────────────────────────────────────────────
    dyd(
        "caffeine_world_create",
        "caffeine_world_create([name]) -> entity",
        "Create an entity; returns its id (0 without a world).",
    ),
    dyd(
        "caffeine_world_destroy",
        "caffeine_world_destroy(entity)",
        "Destroy an entity.",
    ),
    dyd(
        "caffeine_world_is_alive",
        "caffeine_world_is_alive(entity) -> bool",
        "Whether the entity still exists.",
    ),
    dyd(
        "caffeine_world_distance",
        "caffeine_world_distance(a, b) -> number",
        "Distance in metres; 1e9 when either has no position.",
    ),
    // ── Dryad: math ─────────────────────────────────────────────────────
    dyd(
        "caffeine_math_clamp",
        "caffeine_math_clamp(v, lo, hi) -> number",
        "Limits a value to a range; swapped bounds are accepted.",
    ),
    dyd(
        "caffeine_math_clamp01",
        "caffeine_math_clamp01(v) -> number",
        "Limits a value to 0..1.",
    ),
    dyd(
        "caffeine_math_lerp",
        "caffeine_math_lerp(a, b, t) -> number",
        "Linear interpolation between two numbers.",
    ),
    dyd(
        "caffeine_math_inverse_lerp",
        "caffeine_math_inverse_lerp(a, b, v) -> number",
        "The inverse of lerp; the result is not clamped.",
    ),
    dyd(
        "caffeine_math_remap",
        "caffeine_math_remap(v, inLo, inHi, outLo, outHi) -> number",
        "Maps a value from one range to another.",
    ),
    dyd(
        "caffeine_math_smoothstep",
        "caffeine_math_smoothstep(e0, e1, v) -> number",
        "Smooth Hermite interpolation between two edges.",
    ),
    dyd(
        "caffeine_math_approach",
        "caffeine_math_approach(cur, target, step) -> number",
        "Moves a value toward a target by at most step.",
    ),
    dyd(
        "caffeine_math_sign",
        "caffeine_math_sign(v) -> number",
        "Sign of a number.",
    ),
    dyd(
        "caffeine_math_radians",
        "caffeine_math_radians(deg) -> number",
        "Converts degrees to radians.",
    ),
    dyd(
        "caffeine_math_degrees",
        "caffeine_math_degrees(rad) -> number",
        "Converts radians to degrees.",
    ),
    dyd(
        "caffeine_math_min",
        "caffeine_math_min(a, b) -> number",
        "Smaller of two numbers.",
    ),
    dyd(
        "caffeine_math_max",
        "caffeine_math_max(a, b) -> number",
        "Larger of two numbers.",
    ),
    dyd(
        "caffeine_math_abs",
        "caffeine_math_abs(v) -> number",
        "Absolute value.",
    ),
    dyd(
        "caffeine_math_floor",
        "caffeine_math_floor(v) -> number",
        "Rounds down to a whole number.",
    ),
    dyd(
        "caffeine_math_ceil",
        "caffeine_math_ceil(v) -> number",
        "Rounds up to a whole number.",
    ),
    dyd(
        "caffeine_math_round",
        "caffeine_math_round(v) -> number",
        "Rounds to the nearest whole number.",
    ),
    dyd(
        "caffeine_math_sqrt",
        "caffeine_math_sqrt(v) -> number",
        "Square root; negative input gives 0.",
    ),
    dyd(
        "caffeine_math_sin",
        "caffeine_math_sin(rad) -> number",
        "Sine of an angle in radians.",
    ),
    dyd(
        "caffeine_math_cos",
        "caffeine_math_cos(rad) -> number",
        "Cosine of an angle in radians.",
    ),
    dyd(
        "caffeine_math_pow",
        "caffeine_math_pow(base, exponent) -> number",
        "Raises a number to a power.",
    ),
    // ── Dryad: components ───────────────────────────────────────────────
    dyd(
        "caffeine_component_list",
        "caffeine_component_list() -> [names]",
        "Every component the reflection registry knows.",
    ),
    dyd(
        "caffeine_component_names",
        "caffeine_component_names(entity) -> [names]",
        "Components this entity has.",
    ),
    dyd(
        "caffeine_component_has",
        "caffeine_component_has(entity, name) -> bool",
        "Whether the entity has the component.",
    ),
    dyd(
        "caffeine_component_add",
        "caffeine_component_add(entity, name)",
        "Add a component with default values.",
    ),
    dyd(
        "caffeine_component_remove",
        "caffeine_component_remove(entity, name)",
        "Remove a component.",
    ),
    dyd(
        "caffeine_component_get",
        "caffeine_component_get(entity, name) -> object",
        "Every field of a component (null when missing).",
    ),
    dyd(
        "caffeine_component_get_field",
        "caffeine_component_get_field(entity, name, field) -> value",
        "Read one field.",
    ),
    dyd(
        "caffeine_component_set_field",
        "caffeine_component_set_field(entity, name, field, value)",
        "Write one field.",
    ),
    dyd(
        "caffeine_component_set",
        "caffeine_component_set(entity, name, object)",
        "Add the component and write the given fields.",
    ),
    dyd(
        "caffeine_component_get_number",
        "caffeine_component_get_number(entity, name, field[, index]) -> number",
        "Numbers, bools, enum indices and vector parts.",
    ),
    dyd(
        "caffeine_component_set_number",
        "caffeine_component_set_number(entity, name, field, value[, index])",
        "Write a number, bool, enum index or vector part.",
    ),
    dyd(
        "caffeine_component_set_vec",
        "caffeine_component_set_vec(entity, name, field, x, y[, z[, w]])",
        "Write a whole vector or colour.",
    ),
    dyd(
        "caffeine_component_get_text",
        "caffeine_component_get_text(entity, name, field) -> text",
        "Read a string or enum label.",
    ),
    dyd(
        "caffeine_component_set_text",
        "caffeine_component_set_text(entity, name, field, text)",
        "Write a string or enum label.",
    ),
    // ── language words and hooks ─────────────────────────────────────────
    lua(
        "function",
        "function name(args) ... end",
        "Declares a function. Global functions named `onCreate`, `onUpdate`... are engine hooks.",
    ),
    lua(
        "end",
        "end",
        "Closes a `function`, `if`, `for`, `while` or `do` block.",
    ),
    lua(
        "local",
        "local name = value",
        "Declares a variable visible only in the enclosing block. Prefer it over globals.",
    ),
    lua(
        "if",
        "if condition then ... end",
        "Runs the block when the condition is true. Only `false` and `nil` are false in Lua.",
    ),
    lua(
        "then",
        "if condition then",
        "Starts the body of an `if` or `elseif`.",
    ),
    lua(
        "elseif",
        "elseif condition then",
        "Another condition tried when the previous `if` failed.",
    ),
    lua(
        "else",
        "else",
        "Runs when no earlier condition of the `if` matched.",
    ),
    lua(
        "for",
        "for i = first, last, step do ... end",
        "Counting loop, or `for key, value in pairs(table) do` to walk a table.",
    ),
    lua(
        "while",
        "while condition do ... end",
        "Repeats the block while the condition is true.",
    ),
    lua(
        "do",
        "do ... end",
        "Opens a block for `for`, `while` or a plain scope.",
    ),
    lua(
        "repeat",
        "repeat ... until condition",
        "Runs the block, then repeats until the condition is true.",
    ),
    lua(
        "until",
        "until condition",
        "Ends a `repeat` block.",
    ),
    lua(
        "return",
        "return value",
        "Leaves the function, optionally with a value.",
    ),
    lua(
        "break",
        "break",
        "Leaves the innermost loop.",
    ),
    lua(
        "nil",
        "nil",
        "The absence of a value. Reading a missing key gives `nil`.",
    ),
    lua(
        "true",
        "true",
        "Boolean true.",
    ),
    lua(
        "false",
        "false",
        "Boolean false.",
    ),
    lua(
        "and",
        "a and b",
        "Short-circuit logical and.",
    ),
    lua(
        "or",
        "a or b",
        "Short-circuit logical or; `x or default` supplies a default.",
    ),
    lua(
        "not",
        "not value",
        "Logical negation.",
    ),
    lua(
        "onCreate",
        "function onCreate(entityId)",
        "Called once, on the first play-mode frame after the script is attached.",
    ),
    lua(
        "onUpdate",
        "function onUpdate(entityId, dt)",
        "Called every frame. `dt` is the scaled time since the last frame in seconds.",
    ),
    lua(
        "onFixedUpdate",
        "function onFixedUpdate(entityId, dt)",
        "Called at the fixed physics step. Use it for forces and movement that must be frame-rate independent.",
    ),
    lua(
        "onDestroy",
        "function onDestroy(entityId)",
        "Called when the entity is destroyed or play stops.",
    ),
    lua(
        "onCollision",
        "function onCollision(entityId, otherId)",
        "Called when this entity starts touching `otherId`.",
    ),
    lua(
        "onPostProcess",
        "function onPostProcess(entityId, dt, api)",
        "Drives a post-process component. Falls back to `onUpdate` when absent.",
    ),
    lua(
        "entityId",
        "entityId: integer",
        "The entity this script instance runs on. Pass it to `caffeine.component.*` and `caffeine.world.*`.",
    ),
    lua(
        "dt",
        "dt: number",
        "Seconds since the previous frame, scaled by the time scale.",
    ),
    dyd(
        "function",
        "function onUpdate(entity, dt) { ... }",
        "Declares the hook. A Dryad script is one `onUpdate` function.",
    ),
    dyd(
        "let",
        "let name = value;",
        "Declares a variable.",
    ),
    dyd(
        "if",
        "if (condition) { ... } else { ... }",
        "Runs the block when the condition is true.",
    ),
    dyd(
        "else",
        "else { ... }",
        "Runs when the `if` condition was false.",
    ),
    dyd(
        "true",
        "true",
        "Boolean true.",
    ),
    dyd(
        "false",
        "false",
        "Boolean false.",
    ),
    dyd(
        "onUpdate",
        "function onUpdate(entity, dt)",
        "Called every frame while playing. Node graphs start from the On Update node.",
    ),
    dyd(
        "entity",
        "entity: integer",
        "The entity this script runs on. Pass it to the `caffeine_*` natives.",
    ),
    dyd(
        "dt",
        "dt: number",
        "Seconds since the previous frame.",
    ),
];

/// Exact lookup by full name.
pub fn entry(language: Language, name: &str) -> Option<&'static Entry> {
    ENTRIES
        .iter()
        .find(|entry| entry.language == language && entry.name == name)
}

fn is_word(character: char) -> bool {
    character.is_alphanumeric() || character == '_'
}

/// The identifier (with Lua dots) around `column` in `line`, as the longest
/// dotted path that ends at the word under the cursor.
pub fn token_at(line: &str, column: usize) -> Option<String> {
    let chars: Vec<char> = line.chars().collect();
    if chars.is_empty() {
        return None;
    }
    let mut index = column.min(chars.len() - 1);
    if !is_word(chars[index]) {
        if index > 0 && is_word(chars[index - 1]) {
            index -= 1;
        } else {
            return None;
        }
    }
    let mut end = index;
    while end + 1 < chars.len() && is_word(chars[end + 1]) {
        end += 1;
    }
    let mut start = index;
    while start > 0 && (is_word(chars[start - 1]) || chars[start - 1] == '.') {
        start -= 1;
    }
    let token: String = chars[start..=end].iter().collect();
    Some(token.trim_start_matches('.').to_owned())
}

/// The whole dotted chain around `column`, including segments to the right of it.
pub fn full_token_at(line: &str, column: usize) -> Option<String> {
    let chars: Vec<char> = line.chars().collect();
    let token = token_at(line, column)?;
    let mut index = column.min(chars.len().saturating_sub(1));
    if !is_word(chars[index]) && chars[index] != '.' && index > 0 {
        index -= 1;
    }
    let mut end = index;
    while end + 1 < chars.len() && (is_word(chars[end + 1]) || chars[end + 1] == '.') {
        end += 1;
    }
    let mut start = index;
    while start > 0 && (is_word(chars[start - 1]) || chars[start - 1] == '.') {
        start -= 1;
    }
    let full: String = chars[start..=end].iter().collect();
    let full = full.trim_matches('.').to_owned();
    (full != token && !full.is_empty()).then_some(full)
}

/// Resolve the token under the cursor to a reference entry. Lua accepts the full
/// path (`caffeine.math.cross`), a trailing part such as `math.cross`, or a bare
/// name when only one entry ends with it.
pub fn lookup(language: Language, line: &str, column: usize) -> Option<&'static Entry> {
    let token = token_at(line, column)?;
    // Hovering `time` in `caffeine.time.delta()` names the whole call, not the prefix.
    if let Some(full) = full_token_at(line, column) {
        if let Some(found) = entry(language, &full) {
            return Some(found);
        }
    }
    if let Some(found) = entry(language, &token) {
        return Some(found);
    }
    if language == Language::Lua {
        let suffix = format!(".{token}");
        let mut matches = ENTRIES
            .iter()
            .filter(|entry| entry.language == Language::Lua && entry.name.ends_with(&suffix));
        let first = matches.next()?;
        if matches.next().is_none() {
            return Some(first);
        }
    }
    None
}

/// Module an entry belongs to: `time` for `caffeine.time.delta` and `caffeine_time_delta`.
pub fn module_of(entry: &Entry) -> &str {
    if !entry.name.starts_with("caffeine") {
        let hook = entry.name.starts_with("on")
            && entry.name[2..]
                .chars()
                .next()
                .is_some_and(char::is_uppercase);
        return if hook { "lifecycle" } else { "language" };
    }
    let rest = match entry.language {
        Language::Lua => entry.name.strip_prefix("caffeine.").unwrap_or(entry.name),
        Language::Dryad => entry.name.strip_prefix("caffeine_").unwrap_or(entry.name),
    };
    let separator = match entry.language {
        Language::Lua => '.',
        Language::Dryad => '_',
    };
    rest.split(separator).next().unwrap_or(rest)
}

/// Parameters parsed from the signature: `(name, type)`, type empty when not written.
pub fn parameters(entry: &Entry) -> Vec<(&str, &str)> {
    let signature = entry.signature;
    let (Some(open), Some(close)) = (signature.find('('), signature.rfind(')')) else {
        return Vec::new();
    };
    if close <= open + 1 {
        return Vec::new();
    }
    signature[open + 1..close]
        .split(',')
        .map(str::trim)
        .filter(|part| !part.is_empty())
        .map(|part| match part.split_once(':') {
            Some((name, kind)) => (name.trim(), kind.trim()),
            None => (part, ""),
        })
        .collect()
}

/// Result type after `->`, if the signature declares one.
pub fn returns(entry: &Entry) -> Option<&str> {
    entry
        .signature
        .rsplit_once("->")
        .map(|(_, result)| result.trim())
        .filter(|result| !result.is_empty())
}

/// Other entries of the same module and language, for the "See also" line.
pub fn related(entry: &Entry) -> Vec<&'static Entry> {
    let module = module_of(entry);
    ENTRIES
        .iter()
        .filter(|other| {
            other.language == entry.language
                && other.name != entry.name
                && module_of(other) == module
        })
        .collect()
}

/// Website page documenting an entry, relative to `caffeine-website/content/en/docs/`.
pub fn reference_page(entry: &Entry) -> String {
    const API_PAGES: &[&str] = &[
        "time",
        "input",
        "world",
        "math",
        "debug",
        "events",
        "component",
        "entity",
        "random",
        "physics3d",
        "scene",
        "session",
        "audio",
        "hierarchy",
    ];
    let module = module_of(entry);
    if matches!(module, "language" | "lifecycle") {
        return "scripting/scripting.md".to_owned();
    }
    if API_PAGES.contains(&module) {
        format!("scripting/api/{module}.md")
    } else {
        "scripting/runtime-apis.md".to_owned()
    }
}

/// Public URL of the same page, for machines without an engine checkout.
pub fn reference_url(entry: &Entry) -> String {
    let page = reference_page(entry);
    let page = page.trim_end_matches(".md");
    format!("https://devscafecommunity.github.io/caffeine-website/docs/{page}")
}

/// A call example: the signature without its result type.
fn example(entry: &Entry) -> String {
    let call = entry
        .signature
        .split("->")
        .next()
        .unwrap_or(entry.signature)
        .trim();
    match entry.language {
        Language::Lua => call.to_owned(),
        Language::Dryad => format!("{call};"),
    }
}

/// Markdown shown in the hover card: what it is, how to call it, what it takes and
/// returns, where else to look, and how to jump to the reference.
pub fn hover_markdown(entry: &Entry) -> String {
    let language = match entry.language {
        Language::Lua => "Lua",
        Language::Dryad => "Dryad",
    };
    let mut card = format!(
        "**{}** · {language} · module `{}`\n\n`{}`\n\n{}\n",
        entry.name,
        module_of(entry),
        entry.signature,
        entry.doc
    );
    let parameters = parameters(entry);
    if !parameters.is_empty() {
        card.push_str("\n**Parameters**\n");
        for (name, kind) in parameters {
            if kind.is_empty() {
                card.push_str(&format!("- `{name}`\n"));
            } else {
                card.push_str(&format!("- `{name}`: {kind}\n"));
            }
        }
    }
    if let Some(result) = returns(entry) {
        card.push_str(&format!("\n**Returns** `{result}`\n"));
    }
    card.push_str(&format!("\n**Example**\n\n`{}`\n", example(entry)));
    let related = related(entry);
    if !related.is_empty() {
        let names: Vec<String> = related
            .iter()
            .take(6)
            .map(|other| format!("`{}`", other.name))
            .collect();
        card.push_str(&format!("\n**See also** {}\n", names.join(", ")));
    }
    card.push_str(&format!(
        "\n*Ctrl+click opens the reference: {}*",
        reference_page(entry)
    ));
    card
}

/// Resolve the reference page on disk, looking inside an engine checkout.
pub fn reference_file(entry: &Entry, root: &std::path::Path) -> Option<std::path::PathBuf> {
    let file = root
        .join("caffeine-website/content/en/docs")
        .join(reference_page(entry));
    file.is_file().then_some(file)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hover_card_is_detailed_and_points_to_the_reference() {
        let entry = entry(Language::Lua, "caffeine.math.lerp").expect("lerp is documented");
        let card = hover_markdown(entry);
        assert!(card.contains("module `math`"));
        assert!(card.contains("**Parameters**"));
        assert!(card.contains("**Returns**"));
        assert!(card.contains("**Example**"));
        assert!(card.contains("**See also**"));
        assert!(card.contains("scripting/api/math.md"));
        assert_eq!(module_of(entry), "math");
        let native = entry_dryad("caffeine_time_delta");
        assert_eq!(module_of(native), "time");
        assert_eq!(reference_page(native), "scripting/api/time.md");
    }

    fn entry_dryad(name: &str) -> &'static Entry {
        entry(Language::Dryad, name).expect("documented")
    }

    #[test]
    fn every_entry_has_a_reference_page_on_disk() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
        if !root.join("caffeine-website/content/en/docs").is_dir() {
            return;
        }
        for entry in ENTRIES {
            assert!(
                reference_file(entry, &root).is_some(),
                "{} has no page {}",
                entry.name,
                reference_page(entry)
            );
        }
    }

    #[test]
    fn token_follows_the_dotted_path() {
        let line = "local c = caffeine.math.cross(a, b)";
        let column = line.find("cross").unwrap() + 2;
        assert_eq!(
            token_at(line, column).as_deref(),
            Some("caffeine.math.cross")
        );
        assert_eq!(token_at("x = 1", 3), None);
    }

    #[test]
    fn lookup_resolves_lua_and_dryad() {
        let lua_line = "caffeine.input.isKeyDown(\"Space\")";
        let found = lookup(Language::Lua, lua_line, 20).unwrap();
        assert_eq!(found.name, "caffeine.input.isKeyDown");

        let dyd_line = "  caffeine_world_destroy(entity);";
        let found = lookup(Language::Dryad, dyd_line, 6).unwrap();
        assert_eq!(found.name, "caffeine_world_destroy");
        assert!(lookup(Language::Dryad, "let value = 3;", 5).is_none());
    }

    #[test]
    fn ambiguous_bare_lua_names_do_not_resolve() {
        // `scale` exists under math only, `log` under debug only: both resolve; a name
        // shared by two tables must not guess.
        assert!(lookup(Language::Lua, "log(1)", 1).is_some());
        let shared = ["destroy", "create"];
        for name in shared {
            let count = ENTRIES
                .iter()
                .filter(|e| e.language == Language::Lua && e.name.ends_with(&format!(".{name}")))
                .count();
            assert!(count >= 1);
        }
    }

    #[test]
    fn every_dryad_native_is_documented() {
        let sources = [
            include_str!("../../../../../src/caffeine/script/DryadHost.cpp"),
            include_str!("../../../../../src/caffeine/script/DryadComponentBindings.cpp"),
            include_str!("../../../../../src/caffeine/script/DryadRuntimeBindings.cpp"),
        ];
        let mut missing = Vec::new();
        for source in sources {
            for part in source.split("\"caffeine_").skip(1) {
                let name = format!("caffeine_{}", part.split('"').next().unwrap_or_default());
                if !name
                    .chars()
                    .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_')
                {
                    continue;
                }
                if entry(Language::Dryad, &name).is_none() {
                    missing.push(name);
                }
            }
        }
        assert!(
            missing.is_empty(),
            "undocumented Dryad natives: {missing:?}"
        );
    }
}
