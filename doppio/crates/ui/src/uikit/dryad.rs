//! Dryad node graph model, catalog, emitter and round-trip.
//!
//! The graph is editor state only: the game runs the emitted `.dyd` through
//! the C++ `DryadHost`. This module mirrors `script/nodes/{NodeGraph,
//! NodeCatalog,NodeEmitter,NodeGraphIO,NodeRoundTrip}` so both sides agree on
//! the file format:
//!
//! - `name.dyd` — the program, with `// @node <id>` markers.
//! - `name.node.json` — canvas positions, node types, wires and parameters.
//!
//! The emitter is deterministic: the same graph always produces the same
//! text. JSON fields, ordering and number formatting match the C++ side
//! (`%.17g` for values, default `%g` for f32 positions) so existing files
//! round-trip unchanged.

use iced::Vector;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

// ── Catalog ──────────────────────────────────────────────────────────────

/// A node type understood by the emitter and the Dryad host.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum NodeKind {
    OnUpdate,
    GetPosition,
    DeltaTime,
    SetPosition,
    Translate,
    SetScale,
    GetScale,
    Log,
    Constant,
    Branch,
    KeyDown,
    ActionDown,
    Axis,
    TotalTime,
    DestroySelf,
    Add,
    Subtract,
    Multiply,
    Divide,
    Negate,
    Less,
    Greater,
    Equal,
    And,
    Or,
    Not,
    SelfEntity,
    IsAlive,
    Distance,
    RandomValue,
    RandomRange,
    RandomInt,
    RandomChance,
    MouseX,
    MouseY,
    MouseDeltaX,
    MouseDeltaY,
    MouseDown,
    FixedDelta,
    FrameCount,
    Min,
    Max,
    Abs,
    Floor,
    Ceil,
    Round,
    Sqrt,
    Pow,
    Sin,
    Cos,
    Radians,
    Degrees,
    Sign,
    Clamp,
    Clamp01,
    Lerp,
    InverseLerp,
    Smoothstep,
}

impl NodeKind {
    /// Every kind, in catalog order.
    pub const ALL: [NodeKind; 58] = [
        NodeKind::Constant,
        NodeKind::Branch,
        NodeKind::OnUpdate,
        NodeKind::SetPosition,
        NodeKind::Translate,
        NodeKind::GetPosition,
        NodeKind::SetScale,
        NodeKind::GetScale,
        NodeKind::DeltaTime,
        NodeKind::Log,
        NodeKind::KeyDown,
        NodeKind::ActionDown,
        NodeKind::Axis,
        NodeKind::TotalTime,
        NodeKind::DestroySelf,
        NodeKind::Add,
        NodeKind::Subtract,
        NodeKind::Multiply,
        NodeKind::Divide,
        NodeKind::Negate,
        NodeKind::Less,
        NodeKind::Greater,
        NodeKind::Equal,
        NodeKind::And,
        NodeKind::Or,
        NodeKind::Not,
        NodeKind::SelfEntity,
        NodeKind::IsAlive,
        NodeKind::Distance,
        NodeKind::RandomValue,
        NodeKind::RandomRange,
        NodeKind::RandomInt,
        NodeKind::RandomChance,
        NodeKind::MouseX,
        NodeKind::MouseY,
        NodeKind::MouseDeltaX,
        NodeKind::MouseDeltaY,
        NodeKind::MouseDown,
        NodeKind::FixedDelta,
        NodeKind::FrameCount,
        NodeKind::Min,
        NodeKind::Max,
        NodeKind::Abs,
        NodeKind::Floor,
        NodeKind::Ceil,
        NodeKind::Round,
        NodeKind::Sqrt,
        NodeKind::Pow,
        NodeKind::Sin,
        NodeKind::Cos,
        NodeKind::Radians,
        NodeKind::Degrees,
        NodeKind::Sign,
        NodeKind::Clamp,
        NodeKind::Clamp01,
        NodeKind::Lerp,
        NodeKind::InverseLerp,
        NodeKind::Smoothstep,
    ];

    /// The `type` field written to `.node.json` (`"OnUpdate"`, `"Branch"`…).
    pub fn type_name(self) -> &'static str {
        match self {
            NodeKind::OnUpdate => "OnUpdate",
            NodeKind::GetPosition => "GetPosition",
            NodeKind::DeltaTime => "DeltaTime",
            NodeKind::SetPosition => "SetPosition",
            NodeKind::Translate => "Translate",
            NodeKind::SetScale => "SetScale",
            NodeKind::GetScale => "GetScale",
            NodeKind::Log => "Log",
            NodeKind::Constant => "Constant",
            NodeKind::Branch => "Branch",
            NodeKind::KeyDown => "KeyDown",
            NodeKind::ActionDown => "ActionDown",
            NodeKind::Axis => "Axis",
            NodeKind::TotalTime => "TotalTime",
            NodeKind::DestroySelf => "DestroySelf",
            NodeKind::Add => "Add",
            NodeKind::Subtract => "Subtract",
            NodeKind::Multiply => "Multiply",
            NodeKind::Divide => "Divide",
            NodeKind::Negate => "Negate",
            NodeKind::Less => "Less",
            NodeKind::Greater => "Greater",
            NodeKind::Equal => "Equal",
            NodeKind::And => "And",
            NodeKind::Or => "Or",
            NodeKind::Not => "Not",
            NodeKind::SelfEntity => "SelfEntity",
            NodeKind::IsAlive => "IsAlive",
            NodeKind::Distance => "Distance",
            other => native_row(other).map_or("", |row| row.type_name),
        }
    }

    /// The label shown on the canvas and in the palette.
    pub fn display_name(self) -> &'static str {
        match self {
            NodeKind::OnUpdate => "On Update",
            NodeKind::GetPosition => "Get Position",
            NodeKind::DeltaTime => "Delta Time",
            NodeKind::SetPosition => "Set Position",
            NodeKind::Translate => "Translate",
            NodeKind::SetScale => "Set Scale",
            NodeKind::GetScale => "Get Scale",
            NodeKind::Log => "Log",
            NodeKind::Constant => "Constant",
            NodeKind::Branch => "If / Else",
            NodeKind::KeyDown => "Key Down",
            NodeKind::ActionDown => "Action Down",
            NodeKind::Axis => "Input Axis",
            NodeKind::TotalTime => "Total Time",
            NodeKind::DestroySelf => "Destroy Self",
            other => extra(other).1,
        }
    }

    /// Display path used to group the palette, e.g. `"Engine / Transform"`.
    pub fn category(self) -> &'static str {
        match self {
            NodeKind::Constant => "Language / Values",
            NodeKind::Branch => "Language / Flow",
            NodeKind::OnUpdate => "Engine / Events",
            NodeKind::SetPosition
            | NodeKind::Translate
            | NodeKind::GetPosition
            | NodeKind::SetScale
            | NodeKind::GetScale => "Engine / Transform",
            NodeKind::DeltaTime => "Engine / Time",
            NodeKind::Log => "Engine / Debug",
            NodeKind::KeyDown | NodeKind::ActionDown | NodeKind::Axis => "Engine / Input",
            NodeKind::TotalTime => "Engine / Time",
            NodeKind::DestroySelf => "Engine / World",
            other => extra(other).2,
        }
    }

    pub fn description(self) -> &'static str {
        match self {
            NodeKind::Constant => "A fixed number or boolean value.",
            NodeKind::Branch => {
                "Run one exec branch when the condition is true, the other when false."
            }
            NodeKind::OnUpdate => "Runs once per frame while the entity is in play mode.",
            NodeKind::SetPosition => "Move the entity to an absolute position.",
            NodeKind::Translate => "Move the entity by a relative offset.",
            NodeKind::GetPosition => "Read the entity position.",
            NodeKind::SetScale => "Set the entity scale on all three axes.",
            NodeKind::GetScale => "Read the entity scale.",
            NodeKind::DeltaTime => "Seconds elapsed since the previous frame.",
            NodeKind::Log => "Write a message to the Doppio console.",
            NodeKind::KeyDown => "True while the named key is held (Space, W, LShift...).",
            NodeKind::ActionDown => "True while the named action is held (Jump, Attack...).",
            NodeKind::Axis => "Input axis in -1..1 (Horizontal, Vertical, LookX, LookY).",
            NodeKind::TotalTime => "Scaled seconds since play started.",
            NodeKind::DestroySelf => "Destroy the entity that runs this script.",
            other => extra(other).3,
        }
    }

    /// Short line under the node title on the canvas.
    pub fn subtitle(self) -> &'static str {
        match self {
            NodeKind::OnUpdate => "runs every frame",
            NodeKind::SetPosition => "absolute position",
            NodeKind::Translate => "relative motion",
            NodeKind::Branch => "if / else",
            NodeKind::SetScale => "absolute scale",
            NodeKind::GetPosition => "reads position",
            NodeKind::GetScale => "reads scale",
            NodeKind::Log => "console message",
            NodeKind::DeltaTime => "frame delta",
            NodeKind::Constant => "fixed value",
            NodeKind::KeyDown => "keyboard",
            NodeKind::ActionDown => "input action",
            NodeKind::Axis => "input axis",
            NodeKind::TotalTime => "time since play",
            NodeKind::DestroySelf => "removes the entity",
            other => extra(other).4,
        }
    }

    /// Parse the `.node.json` `type` field.
    pub fn from_type_name(name: &str) -> Option<NodeKind> {
        NodeKind::ALL
            .iter()
            .copied()
            .find(|kind| kind.type_name() == name)
    }

    /// `true` for nodes that only produce data and have no exec pins.
    pub fn is_data_only(self) -> bool {
        self.call_native().is_some()
            || matches!(
                self,
                NodeKind::GetPosition
                    | NodeKind::GetScale
                    | NodeKind::DeltaTime
                    | NodeKind::Constant
                    | NodeKind::KeyDown
                    | NodeKind::ActionDown
                    | NodeKind::Axis
                    | NodeKind::TotalTime
                    | NodeKind::Add
                    | NodeKind::Subtract
                    | NodeKind::Multiply
                    | NodeKind::Divide
                    | NodeKind::Negate
                    | NodeKind::Less
                    | NodeKind::Greater
                    | NodeKind::Equal
                    | NodeKind::And
                    | NodeKind::Or
                    | NodeKind::Not
                    | NodeKind::SelfEntity
                    | NodeKind::IsAlive
                    | NodeKind::Distance
            )
    }

    /// Infix or prefix operator emitted for a math, compare or logic node.
    pub fn operator(self) -> Option<Operator> {
        let (symbol, unary, input, output) = match self {
            NodeKind::Add => ("+", false, PinKind::Number, PinKind::Number),
            NodeKind::Subtract => ("-", false, PinKind::Number, PinKind::Number),
            NodeKind::Multiply => ("*", false, PinKind::Number, PinKind::Number),
            NodeKind::Divide => ("/", false, PinKind::Number, PinKind::Number),
            NodeKind::Negate => ("-", true, PinKind::Number, PinKind::Number),
            NodeKind::Less => ("<", false, PinKind::Number, PinKind::Bool),
            NodeKind::Greater => (">", false, PinKind::Number, PinKind::Bool),
            NodeKind::Equal => ("==", false, PinKind::Number, PinKind::Bool),
            NodeKind::And => ("&&", false, PinKind::Bool, PinKind::Bool),
            NodeKind::Or => ("||", false, PinKind::Bool, PinKind::Bool),
            NodeKind::Not => ("!", true, PinKind::Bool, PinKind::Bool),
            _ => return None,
        };
        Some(Operator {
            symbol,
            unary,
            input,
            output,
        })
    }

    /// Native called by a node that wraps one engine function, with its argument count.
    pub fn call_native(self) -> Option<(&'static str, usize)> {
        match self {
            NodeKind::IsAlive => Some(("caffeine_world_is_alive", 1)),
            NodeKind::Distance => Some(("caffeine_world_distance", 2)),
            other => native_row(other).map(|row| (row.native, row.arity)),
        }
    }
}

/// A data node that wraps one engine native taking up to three number inputs `a`, `b`, `c`.
#[derive(Debug, Clone, Copy)]
struct NativeNode {
    kind: NodeKind,
    type_name: &'static str,
    display_name: &'static str,
    category: &'static str,
    description: &'static str,
    subtitle: &'static str,
    native: &'static str,
    arity: usize,
    boolean: bool,
}

fn native_row(kind: NodeKind) -> Option<&'static NativeNode> {
    NATIVE_NODES.iter().find(|row| row.kind == kind)
}

const NATIVE_NODES: [NativeNode; 29] = [
    NativeNode {
        kind: NodeKind::RandomValue,
        type_name: "RandomValue",
        display_name: "Random Value",
        category: "Engine / Random",
        description: "A random number from 0 up to 1.",
        subtitle: "random 0..1",
        native: "caffeine_random_value",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::RandomRange,
        type_name: "RandomRange",
        display_name: "Random Range",
        category: "Engine / Random",
        description: "A random number between a and b.",
        subtitle: "between a and b",
        native: "caffeine_random_range",
        arity: 2,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::RandomInt,
        type_name: "RandomInt",
        display_name: "Random Int",
        category: "Engine / Random",
        description: "A random whole number between a and b, both included.",
        subtitle: "whole number",
        native: "caffeine_random_int",
        arity: 2,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::RandomChance,
        type_name: "RandomChance",
        display_name: "Random Chance",
        category: "Engine / Random",
        description: "True with the probability a (0 never, 1 always).",
        subtitle: "coin flip",
        native: "caffeine_random_chance",
        arity: 1,
        boolean: true,
    },
    NativeNode {
        kind: NodeKind::MouseX,
        type_name: "MouseX",
        display_name: "Mouse X",
        category: "Engine / Input",
        description: "Mouse horizontal position in pixels.",
        subtitle: "pointer x",
        native: "caffeine_input_mouse_x",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::MouseY,
        type_name: "MouseY",
        display_name: "Mouse Y",
        category: "Engine / Input",
        description: "Mouse vertical position in pixels.",
        subtitle: "pointer y",
        native: "caffeine_input_mouse_y",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::MouseDeltaX,
        type_name: "MouseDeltaX",
        display_name: "Mouse Delta X",
        category: "Engine / Input",
        description: "Horizontal mouse movement since the previous frame.",
        subtitle: "pointer dx",
        native: "caffeine_input_mouse_dx",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::MouseDeltaY,
        type_name: "MouseDeltaY",
        display_name: "Mouse Delta Y",
        category: "Engine / Input",
        description: "Vertical mouse movement since the previous frame.",
        subtitle: "pointer dy",
        native: "caffeine_input_mouse_dy",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::MouseDown,
        type_name: "MouseDown",
        display_name: "Mouse Button",
        category: "Engine / Input",
        description: "True while the mouse button a is held (1 left, 2 middle, 3 right).",
        subtitle: "mouse button",
        native: "caffeine_input_mouse_down",
        arity: 1,
        boolean: true,
    },
    NativeNode {
        kind: NodeKind::FixedDelta,
        type_name: "FixedDelta",
        display_name: "Fixed Delta",
        category: "Engine / Time",
        description: "The fixed simulation step in seconds.",
        subtitle: "fixed step",
        native: "caffeine_time_fixed_delta",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::FrameCount,
        type_name: "FrameCount",
        display_name: "Frame Count",
        category: "Engine / Time",
        description: "Frames elapsed since play started.",
        subtitle: "frame number",
        native: "caffeine_time_frame",
        arity: 0,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Min,
        type_name: "Min",
        display_name: "Min",
        category: "Language / Math",
        description: "The smaller of a and b.",
        subtitle: "smaller",
        native: "caffeine_math_min",
        arity: 2,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Max,
        type_name: "Max",
        display_name: "Max",
        category: "Language / Math",
        description: "The larger of a and b.",
        subtitle: "larger",
        native: "caffeine_math_max",
        arity: 2,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Abs,
        type_name: "Abs",
        display_name: "Abs",
        category: "Language / Math",
        description: "Absolute value of a.",
        subtitle: "absolute",
        native: "caffeine_math_abs",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Floor,
        type_name: "Floor",
        display_name: "Floor",
        category: "Language / Math",
        description: "Rounds a down to a whole number.",
        subtitle: "round down",
        native: "caffeine_math_floor",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Ceil,
        type_name: "Ceil",
        display_name: "Ceil",
        category: "Language / Math",
        description: "Rounds a up to a whole number.",
        subtitle: "round up",
        native: "caffeine_math_ceil",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Round,
        type_name: "Round",
        display_name: "Round",
        category: "Language / Math",
        description: "Rounds a to the nearest whole number.",
        subtitle: "nearest",
        native: "caffeine_math_round",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Sqrt,
        type_name: "Sqrt",
        display_name: "Square Root",
        category: "Language / Math",
        description: "Square root of a; negative input gives 0.",
        subtitle: "square root",
        native: "caffeine_math_sqrt",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Pow,
        type_name: "Pow",
        display_name: "Power",
        category: "Language / Math",
        description: "a raised to the power b.",
        subtitle: "a to the b",
        native: "caffeine_math_pow",
        arity: 2,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Sin,
        type_name: "Sin",
        display_name: "Sine",
        category: "Language / Trigonometry",
        description: "Sine of an angle a in radians.",
        subtitle: "radians",
        native: "caffeine_math_sin",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Cos,
        type_name: "Cos",
        display_name: "Cosine",
        category: "Language / Trigonometry",
        description: "Cosine of an angle a in radians.",
        subtitle: "radians",
        native: "caffeine_math_cos",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Radians,
        type_name: "Radians",
        display_name: "Degrees to Radians",
        category: "Language / Trigonometry",
        description: "Converts degrees a to radians.",
        subtitle: "deg to rad",
        native: "caffeine_math_radians",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Degrees,
        type_name: "Degrees",
        display_name: "Radians to Degrees",
        category: "Language / Trigonometry",
        description: "Converts radians a to degrees.",
        subtitle: "rad to deg",
        native: "caffeine_math_degrees",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Sign,
        type_name: "Sign",
        display_name: "Sign",
        category: "Language / Math",
        description: "-1, 0 or 1 by the sign of a.",
        subtitle: "sign",
        native: "caffeine_math_sign",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Clamp,
        type_name: "Clamp",
        display_name: "Clamp",
        category: "Language / Math",
        description: "Limits a to the range b..c.",
        subtitle: "limit range",
        native: "caffeine_math_clamp",
        arity: 3,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Clamp01,
        type_name: "Clamp01",
        display_name: "Clamp 0..1",
        category: "Language / Math",
        description: "Limits a to 0..1.",
        subtitle: "limit 0..1",
        native: "caffeine_math_clamp01",
        arity: 1,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Lerp,
        type_name: "Lerp",
        display_name: "Lerp",
        category: "Language / Math",
        description: "Blend from a to b by the factor c.",
        subtitle: "blend",
        native: "caffeine_math_lerp",
        arity: 3,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::InverseLerp,
        type_name: "InverseLerp",
        display_name: "Inverse Lerp",
        category: "Language / Math",
        description: "Where c sits between a and b (0 at a, 1 at b).",
        subtitle: "position in range",
        native: "caffeine_math_inverse_lerp",
        arity: 3,
        boolean: false,
    },
    NativeNode {
        kind: NodeKind::Smoothstep,
        type_name: "Smoothstep",
        display_name: "Smoothstep",
        category: "Language / Math",
        description: "Smooth 0..1 ramp of c between the edges a and b.",
        subtitle: "smooth ramp",
        native: "caffeine_math_smoothstep",
        arity: 3,
        boolean: false,
    },
];

/// How an operator node is written in Dryad source.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Operator {
    pub symbol: &'static str,
    pub unary: bool,
    pub input: PinKind,
    pub output: PinKind,
}

/// `(type, display, category, description, subtitle)` of the nodes added after the first ten.
fn extra(
    kind: NodeKind,
) -> (
    &'static str,
    &'static str,
    &'static str,
    &'static str,
    &'static str,
) {
    if let Some(row) = native_row(kind) {
        return (
            row.type_name,
            row.display_name,
            row.category,
            row.description,
            row.subtitle,
        );
    }
    EXTRA
        .iter()
        .copied()
        .find(|row| row.0 == kind.type_name())
        .unwrap_or(("", "", "", "", ""))
}

const EXTRA: [(&str, &str, &str, &str, &str); 14] = [
    ("Add", "Add", "Language / Math", "a + b", "add"),
    (
        "Subtract",
        "Subtract",
        "Language / Math",
        "a - b",
        "subtract",
    ),
    (
        "Multiply",
        "Multiply",
        "Language / Math",
        "a * b",
        "multiply",
    ),
    (
        "Divide",
        "Divide",
        "Language / Math",
        "a / b. Dividing by zero gives an error value, guard it with a Compare.",
        "divide",
    ),
    (
        "Negate",
        "Negate",
        "Language / Math",
        "The value with its sign flipped.",
        "-a",
    ),
    (
        "Less",
        "Less Than",
        "Language / Compare",
        "True when a is smaller than b.",
        "a < b",
    ),
    (
        "Greater",
        "Greater Than",
        "Language / Compare",
        "True when a is larger than b.",
        "a > b",
    ),
    (
        "Equal",
        "Equal",
        "Language / Compare",
        "True when a and b are the same value.",
        "a == b",
    ),
    (
        "And",
        "And",
        "Language / Logic",
        "True when both inputs are true.",
        "a and b",
    ),
    (
        "Or",
        "Or",
        "Language / Logic",
        "True when at least one input is true.",
        "a or b",
    ),
    (
        "Not",
        "Not",
        "Language / Logic",
        "The opposite of the input.",
        "not a",
    ),
    (
        "SelfEntity",
        "This Entity",
        "Engine / World",
        "The id of the entity that runs this script.",
        "entity id",
    ),
    (
        "IsAlive",
        "Is Alive",
        "Engine / World",
        "True while the entity with this id still exists.",
        "entity exists",
    ),
    (
        "Distance",
        "Distance",
        "Engine / World",
        "World distance between two entities.",
        "between entities",
    ),
];

/// One catalog row shared by the palette, the canvas and the emitter.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CatalogEntry {
    pub kind: NodeKind,
    pub type_name: &'static str,
    pub display_name: &'static str,
    pub category: &'static str,
    pub description: &'static str,
}

/// The catalog. It is static so the palette, tests and agents share one list.
pub fn catalog() -> &'static [CatalogEntry] {
    static ALL_ENTRIES: std::sync::OnceLock<Vec<CatalogEntry>> = std::sync::OnceLock::new();
    ALL_ENTRIES.get_or_init(|| {
        let mut entries = CATALOG.to_vec();
        entries.extend(NATIVE_NODES.iter().map(|row| CatalogEntry {
            kind: row.kind,
            type_name: row.type_name,
            display_name: row.display_name,
            category: row.category,
            description: row.description,
        }));
        entries
    })
}

const CATALOG: [CatalogEntry; 29] = [
    CatalogEntry {
        kind: NodeKind::Constant,
        type_name: "Constant",
        display_name: "Constant",
        category: "Language / Values",
        description: "A fixed number or boolean value.",
    },
    CatalogEntry {
        kind: NodeKind::Branch,
        type_name: "Branch",
        display_name: "If / Else",
        category: "Language / Flow",
        description: "Run one exec branch when the condition is true, the other when false.",
    },
    CatalogEntry {
        kind: NodeKind::OnUpdate,
        type_name: "OnUpdate",
        display_name: "On Update",
        category: "Engine / Events",
        description: "Runs once per frame while the entity is in play mode.",
    },
    CatalogEntry {
        kind: NodeKind::SetPosition,
        type_name: "SetPosition",
        display_name: "Set Position",
        category: "Engine / Transform",
        description: "Move the entity to an absolute position.",
    },
    CatalogEntry {
        kind: NodeKind::Translate,
        type_name: "Translate",
        display_name: "Translate",
        category: "Engine / Transform",
        description: "Move the entity by a relative offset.",
    },
    CatalogEntry {
        kind: NodeKind::GetPosition,
        type_name: "GetPosition",
        display_name: "Get Position",
        category: "Engine / Transform",
        description: "Read the entity position.",
    },
    CatalogEntry {
        kind: NodeKind::SetScale,
        type_name: "SetScale",
        display_name: "Set Scale",
        category: "Engine / Transform",
        description: "Set the entity scale on all three axes.",
    },
    CatalogEntry {
        kind: NodeKind::GetScale,
        type_name: "GetScale",
        display_name: "Get Scale",
        category: "Engine / Transform",
        description: "Read the entity scale.",
    },
    CatalogEntry {
        kind: NodeKind::DeltaTime,
        type_name: "DeltaTime",
        display_name: "Delta Time",
        category: "Engine / Time",
        description: "Seconds elapsed since the previous frame.",
    },
    CatalogEntry {
        kind: NodeKind::Log,
        type_name: "Log",
        display_name: "Log",
        category: "Engine / Debug",
        description: "Write a message to the Doppio console.",
    },
    CatalogEntry {
        kind: NodeKind::KeyDown,
        type_name: "KeyDown",
        display_name: "Key Down",
        category: "Engine / Input",
        description: "True while the named key is held (Space, W, LShift...).",
    },
    CatalogEntry {
        kind: NodeKind::ActionDown,
        type_name: "ActionDown",
        display_name: "Action Down",
        category: "Engine / Input",
        description: "True while the named action is held (Jump, Attack...).",
    },
    CatalogEntry {
        kind: NodeKind::Axis,
        type_name: "Axis",
        display_name: "Input Axis",
        category: "Engine / Input",
        description: "Input axis in -1..1 (Horizontal, Vertical, LookX, LookY).",
    },
    CatalogEntry {
        kind: NodeKind::TotalTime,
        type_name: "TotalTime",
        display_name: "Total Time",
        category: "Engine / Time",
        description: "Scaled seconds since play started.",
    },
    CatalogEntry {
        kind: NodeKind::DestroySelf,
        type_name: "DestroySelf",
        display_name: "Destroy Self",
        category: "Engine / World",
        description: "Destroy the entity that runs this script.",
    },
    CatalogEntry {
        kind: NodeKind::Add,
        type_name: "Add",
        display_name: "Add",
        category: "Language / Math",
        description: "a + b",
    },
    CatalogEntry {
        kind: NodeKind::Subtract,
        type_name: "Subtract",
        display_name: "Subtract",
        category: "Language / Math",
        description: "a - b",
    },
    CatalogEntry {
        kind: NodeKind::Multiply,
        type_name: "Multiply",
        display_name: "Multiply",
        category: "Language / Math",
        description: "a * b",
    },
    CatalogEntry {
        kind: NodeKind::Divide,
        type_name: "Divide",
        display_name: "Divide",
        category: "Language / Math",
        description: "a / b. Dividing by zero gives an error value, guard it with a Compare.",
    },
    CatalogEntry {
        kind: NodeKind::Negate,
        type_name: "Negate",
        display_name: "Negate",
        category: "Language / Math",
        description: "The value with its sign flipped.",
    },
    CatalogEntry {
        kind: NodeKind::Less,
        type_name: "Less",
        display_name: "Less Than",
        category: "Language / Compare",
        description: "True when a is smaller than b.",
    },
    CatalogEntry {
        kind: NodeKind::Greater,
        type_name: "Greater",
        display_name: "Greater Than",
        category: "Language / Compare",
        description: "True when a is larger than b.",
    },
    CatalogEntry {
        kind: NodeKind::Equal,
        type_name: "Equal",
        display_name: "Equal",
        category: "Language / Compare",
        description: "True when a and b are the same value.",
    },
    CatalogEntry {
        kind: NodeKind::And,
        type_name: "And",
        display_name: "And",
        category: "Language / Logic",
        description: "True when both inputs are true.",
    },
    CatalogEntry {
        kind: NodeKind::Or,
        type_name: "Or",
        display_name: "Or",
        category: "Language / Logic",
        description: "True when at least one input is true.",
    },
    CatalogEntry {
        kind: NodeKind::Not,
        type_name: "Not",
        display_name: "Not",
        category: "Language / Logic",
        description: "The opposite of the input.",
    },
    CatalogEntry {
        kind: NodeKind::SelfEntity,
        type_name: "SelfEntity",
        display_name: "This Entity",
        category: "Engine / World",
        description: "The id of the entity that runs this script.",
    },
    CatalogEntry {
        kind: NodeKind::IsAlive,
        type_name: "IsAlive",
        display_name: "Is Alive",
        category: "Engine / World",
        description: "True while the entity with this id still exists.",
    },
    CatalogEntry {
        kind: NodeKind::Distance,
        type_name: "Distance",
        display_name: "Distance",
        category: "Engine / World",
        description: "World distance between two entities.",
    },
];

/// Look up a catalog entry by `type` name.
pub fn catalog_entry(type_name: &str) -> Option<&'static CatalogEntry> {
    catalog().iter().find(|entry| entry.type_name == type_name)
}

// ── Pins ─────────────────────────────────────────────────────────────────

/// Wire kind. Exec chains run statements; data pins carry values.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PinKind {
    Exec,
    Number,
    Bool,
}

/// One port on a node. `name` is the pin name used in edge endpoints
/// (`"n2.x"`), `label` is what the canvas draws.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PinDef {
    pub name: &'static str,
    pub label: &'static str,
    pub kind: PinKind,
}

const EXEC_IN: PinDef = PinDef {
    name: "in",
    label: "in",
    kind: PinKind::Exec,
};
const EXEC_OUT: PinDef = PinDef {
    name: "out",
    label: "out",
    kind: PinKind::Exec,
};
const VECTOR_INPUTS: [PinDef; 4] = [
    EXEC_IN,
    PinDef {
        name: "x",
        label: "x",
        kind: PinKind::Number,
    },
    PinDef {
        name: "y",
        label: "y",
        kind: PinKind::Number,
    },
    PinDef {
        name: "z",
        label: "z",
        kind: PinKind::Number,
    },
];
const VECTOR_OUTPUTS: [PinDef; 1] = [EXEC_OUT];
const VECTOR_DATA_OUTPUTS: [PinDef; 3] = [
    PinDef {
        name: "x",
        label: "x",
        kind: PinKind::Number,
    },
    PinDef {
        name: "y",
        label: "y",
        kind: PinKind::Number,
    },
    PinDef {
        name: "z",
        label: "z",
        kind: PinKind::Number,
    },
];
const VALUE_OUTPUT: [PinDef; 1] = [PinDef {
    name: "value",
    label: "value",
    kind: PinKind::Number,
}];
const BOOL_VALUE_OUTPUT: [PinDef; 1] = [PinDef {
    name: "value",
    label: "value",
    kind: PinKind::Bool,
}];
const NUMBER_AB: [PinDef; 2] = [
    PinDef {
        name: "a",
        label: "a",
        kind: PinKind::Number,
    },
    PinDef {
        name: "b",
        label: "b",
        kind: PinKind::Number,
    },
];
const NUMBER_ABC: [PinDef; 3] = [
    PinDef {
        name: "a",
        label: "a",
        kind: PinKind::Number,
    },
    PinDef {
        name: "b",
        label: "b",
        kind: PinKind::Number,
    },
    PinDef {
        name: "c",
        label: "c",
        kind: PinKind::Number,
    },
];
const NUMBER_A: [PinDef; 1] = [PinDef {
    name: "a",
    label: "a",
    kind: PinKind::Number,
}];
const BOOL_AB: [PinDef; 2] = [
    PinDef {
        name: "a",
        label: "a",
        kind: PinKind::Bool,
    },
    PinDef {
        name: "b",
        label: "b",
        kind: PinKind::Bool,
    },
];
const BOOL_A: [PinDef; 1] = [PinDef {
    name: "a",
    label: "a",
    kind: PinKind::Bool,
}];
const LOG_INPUTS: [PinDef; 1] = [EXEC_IN];
const BRANCH_INPUTS: [PinDef; 2] = [
    EXEC_IN,
    PinDef {
        name: "condition",
        label: "if",
        kind: PinKind::Bool,
    },
];
const BRANCH_OUTPUTS: [PinDef; 3] = [
    PinDef {
        name: "then",
        label: "then",
        kind: PinKind::Exec,
    },
    PinDef {
        name: "else",
        label: "else",
        kind: PinKind::Exec,
    },
    EXEC_OUT,
];

/// Input pins for a kind, in canvas order.
pub fn inputs(kind: NodeKind) -> &'static [PinDef] {
    if let Some(row) = native_row(kind) {
        return match row.arity {
            0 => &[],
            1 => &NUMBER_A,
            2 => &NUMBER_AB,
            _ => &NUMBER_ABC,
        };
    }
    match kind {
        NodeKind::SetPosition | NodeKind::Translate | NodeKind::SetScale => &VECTOR_INPUTS,
        NodeKind::Log | NodeKind::DestroySelf => &LOG_INPUTS,
        NodeKind::Branch => &BRANCH_INPUTS,
        NodeKind::Add
        | NodeKind::Subtract
        | NodeKind::Multiply
        | NodeKind::Divide
        | NodeKind::Less
        | NodeKind::Greater
        | NodeKind::Equal
        | NodeKind::Distance => &NUMBER_AB,
        NodeKind::Negate | NodeKind::IsAlive => &NUMBER_A,
        NodeKind::And | NodeKind::Or => &BOOL_AB,
        NodeKind::Not => &BOOL_A,
        NodeKind::SelfEntity
        | NodeKind::OnUpdate
        | NodeKind::GetPosition
        | NodeKind::DeltaTime
        | NodeKind::GetScale
        | NodeKind::Constant
        | NodeKind::KeyDown
        | NodeKind::ActionDown
        | NodeKind::Axis
        | NodeKind::TotalTime => &[],
        // Native-call nodes returned above.
        _ => &[],
    }
}

/// Output pins for a kind. `Constant` always reports a number here; the
/// canvas and connection checks use [`outputs_for`] to honour `bool`.
pub fn outputs(kind: NodeKind) -> &'static [PinDef] {
    if let Some(row) = native_row(kind) {
        return if row.boolean {
            &BOOL_VALUE_OUTPUT
        } else {
            &VALUE_OUTPUT
        };
    }
    match kind {
        NodeKind::OnUpdate => std::slice::from_ref(&EXEC_OUT),
        NodeKind::SetPosition | NodeKind::Translate | NodeKind::SetScale => &VECTOR_OUTPUTS,
        NodeKind::GetPosition | NodeKind::GetScale => &VECTOR_DATA_OUTPUTS,
        NodeKind::DeltaTime
        | NodeKind::Constant
        | NodeKind::Axis
        | NodeKind::TotalTime
        | NodeKind::Add
        | NodeKind::Subtract
        | NodeKind::Multiply
        | NodeKind::Divide
        | NodeKind::Negate
        | NodeKind::SelfEntity
        | NodeKind::Distance => &VALUE_OUTPUT,
        NodeKind::KeyDown
        | NodeKind::ActionDown
        | NodeKind::Less
        | NodeKind::Greater
        | NodeKind::Equal
        | NodeKind::And
        | NodeKind::Or
        | NodeKind::Not
        | NodeKind::IsAlive => &BOOL_VALUE_OUTPUT,
        NodeKind::Log | NodeKind::DestroySelf => std::slice::from_ref(&EXEC_OUT),
        NodeKind::Branch => &BRANCH_OUTPUTS,
        // Native-call nodes returned above.
        _ => &[],
    }
}

/// Output pins for one node, honouring a `Constant`'s number/bool mode.
pub fn outputs_for(node: &GraphNode) -> Vec<PinDef> {
    let mut pins = outputs(node.kind).to_vec();
    if node.kind == NodeKind::Constant && node.constant_is_bool {
        for pin in &mut pins {
            if pin.name == "value" {
                pin.kind = PinKind::Bool;
            }
        }
    }
    pins
}

// ── Graph model ──────────────────────────────────────────────────────────

/// One node: identity, canvas position and inline values.
#[derive(Debug, Clone, PartialEq)]
pub struct GraphNode {
    pub id: String,
    pub kind: NodeKind,
    pub x: f32,
    pub y: f32,
    pub number_value: f64,
    pub bool_value: bool,
    pub constant_is_bool: bool,
    pub input_x: f64,
    pub input_y: f64,
    pub input_z: f64,
    pub input_condition: bool,
    pub text_value: String,
}

impl GraphNode {
    pub fn new(id: impl Into<String>, kind: NodeKind, x: f32, y: f32) -> Self {
        Self {
            id: id.into(),
            kind,
            x,
            y,
            number_value: 0.0,
            bool_value: false,
            constant_is_bool: false,
            input_x: 0.0,
            input_y: 0.0,
            input_z: 0.0,
            input_condition: false,
            text_value: match kind {
                NodeKind::KeyDown => "Space",
                NodeKind::ActionDown => "Jump",
                NodeKind::Axis => "Horizontal",
                _ => "Hello from Dryad",
            }
            .to_owned(),
        }
    }

    /// Editable text value for a Log node, or an empty string.
    pub fn text(&self) -> &str {
        &self.text_value
    }
}

/// One link between two pins. Endpoints use `nodeId.pin` (`"n1.out"`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GraphEdge {
    pub from: String,
    pub to: String,
}

/// The editor graph, mirroring the C++ `NodeGraph`.
#[derive(Debug, Clone, PartialEq)]
pub struct Graph {
    pub version: i32,
    pub script_file_name: String,
    pub nodes: Vec<GraphNode>,
    pub edges: Vec<GraphEdge>,
}

/// The emitted files for a graph.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EmitResult {
    pub dyd: String,
    pub node_json: String,
}

impl Graph {
    /// An empty graph for `script_file_name`. Add an `OnUpdate` node before
    /// emitting; the emitter requires exactly one.
    pub fn new(script_file_name: &str) -> Self {
        Self {
            version: 1,
            script_file_name: script_file_name.to_owned(),
            nodes: Vec::new(),
            edges: Vec::new(),
        }
    }

    /// The graph the editor starts with when a script has no markers and no
    /// sibling JSON: a single `OnUpdate` node.
    pub fn default_for_script(script_file_name: &str) -> Self {
        let mut graph = Graph::new(script_file_name);
        graph
            .nodes
            .push(GraphNode::new("n1", NodeKind::OnUpdate, 40.0, 40.0));
        graph
    }

    pub fn find_node(&self, id: &str) -> Option<&GraphNode> {
        self.nodes.iter().find(|node| node.id == id)
    }

    pub fn find_node_mut(&mut self, id: &str) -> Option<&mut GraphNode> {
        self.nodes.iter_mut().find(|node| node.id == id)
    }

    pub fn has_node(&self, id: &str) -> bool {
        self.find_node(id).is_some()
    }

    /// The single `OnUpdate` node, if the graph is valid.
    pub fn on_update(&self) -> Option<&GraphNode> {
        self.nodes
            .iter()
            .find(|node| node.kind == NodeKind::OnUpdate)
    }

    fn next_serial(&self) -> u64 {
        let mut max = 0u64;
        for node in &self.nodes {
            let Some(rest) = node.id.strip_prefix('n') else {
                continue;
            };
            if rest.is_empty() || !rest.bytes().all(|byte| byte.is_ascii_digit()) {
                continue;
            }
            if let Ok(value) = rest.parse::<u64>() {
                max = max.max(value);
            }
        }
        max + 1
    }

    /// Add a node at `x`/`y` and return its generated id.
    pub fn add_node(&mut self, kind: NodeKind, x: f32, y: f32) -> String {
        let id = format!("n{}", self.next_serial());
        let mut node = GraphNode::new(id.clone(), kind, x, y);
        if kind == NodeKind::Constant {
            node.number_value = 1.0;
        }
        self.nodes.push(node);
        id
    }

    /// Remove a node and every edge that touches it.
    pub fn remove_node(&mut self, id: &str) {
        let prefix = format!("{id}.");
        self.edges
            .retain(|edge| !edge.from.starts_with(&prefix) && !edge.to.starts_with(&prefix));
        self.nodes.retain(|node| node.id != id);
    }

    /// Name of the pin at `index` of a node's inputs.
    pub fn input_pin(&self, node_id: &str, index: usize) -> Option<&'static str> {
        let node = self.find_node(node_id)?;
        inputs(node.kind).get(index).map(|pin| pin.name)
    }

    /// Name of the pin at `index` of a node's outputs.
    pub fn output_pin(&self, node_id: &str, index: usize) -> Option<&'static str> {
        let node = self.find_node(node_id)?;
        outputs_for(node).get(index).map(|pin| pin.name)
    }

    /// Remove the edge feeding an input endpoint (`"n2.x"`).
    pub fn disconnect_input(&mut self, to: &str) {
        self.edges.retain(|edge| edge.to != to);
    }

    /// Remove a specific edge.
    pub fn disconnect(&mut self, from: &str, to: &str) {
        self.edges
            .retain(|edge| !(edge.from == from && edge.to == to));
    }

    /// Resolve the edge feeding `to`, if any.
    pub fn edge_into(&self, to: &str) -> Option<&GraphEdge> {
        self.edges.iter().find(|edge| edge.to == to)
    }

    /// Connect two pins after validating names, kinds, self-links and cycles.
    ///
    /// Data and exec inputs accept one wire; connecting replaces any existing
    /// edge into `to`. Exec outputs may fan out to several inputs.
    pub fn connect(&mut self, from: &str, to: &str) -> Result<(), String> {
        let (from_node, from_pin) = parse_pin_ref(from)
            .ok_or_else(|| format!("invalid source endpoint '{from}' (expected nodeId.pin)"))?;
        let (to_node, to_pin) = parse_pin_ref(to)
            .ok_or_else(|| format!("invalid target endpoint '{to}' (expected nodeId.pin)"))?;

        if from_node == to_node {
            return Err("a node cannot connect to itself".to_owned());
        }
        let source = self
            .find_node(from_node)
            .ok_or_else(|| format!("unknown source node '{from_node}'"))?;
        let target = self
            .find_node(to_node)
            .ok_or_else(|| format!("unknown target node '{to_node}'"))?;

        let output = outputs_for(source)
            .into_iter()
            .find(|pin| pin.name == from_pin)
            .ok_or_else(|| {
                format!(
                    "{} has no output pin '{}'",
                    source.kind.type_name(),
                    from_pin
                )
            })?;
        let input = inputs(target.kind)
            .iter()
            .copied()
            .find(|pin| pin.name == to_pin)
            .ok_or_else(|| format!("{} has no input pin '{}'", target.kind.type_name(), to_pin))?;

        let compatible = matches!(
            (output.kind, input.kind),
            (PinKind::Exec, PinKind::Exec)
                | (PinKind::Number, PinKind::Number)
                | (PinKind::Bool, PinKind::Bool)
        );
        if !compatible {
            return Err(format!(
                "cannot connect {} output '{}' to {} input '{}'",
                kind_label(output.kind),
                from_pin,
                kind_label(input.kind),
                to_pin
            ));
        }

        if self.path_exists(to_node, from_node) {
            return Err("that connection would create a cycle".to_owned());
        }

        self.disconnect_input(to);
        self.edges.push(GraphEdge {
            from: from.to_owned(),
            to: to.to_owned(),
        });
        Ok(())
    }

    /// `true` when an edge path leads from `start` to `goal` (exec fan-out
    /// still counts as a directed path).
    fn path_exists(&self, start: &str, goal: &str) -> bool {
        if start == goal {
            return true;
        }
        let mut stack = vec![start];
        let mut visited = vec![start];
        while let Some(current) = stack.pop() {
            for edge in &self.edges {
                let Some((from_node, _)) = parse_pin_ref(&edge.from) else {
                    continue;
                };
                if from_node != current {
                    continue;
                }
                let Some((to_node, _)) = parse_pin_ref(&edge.to) else {
                    continue;
                };
                if to_node == goal {
                    return true;
                }
                if !visited.contains(&to_node) {
                    visited.push(to_node);
                    stack.push(to_node);
                }
            }
        }
        false
    }

    /// Emit the `.dyd` and `.node.json` texts.
    pub fn emit(&self) -> Result<EmitResult, String> {
        emit(self)
    }

    /// Serialize the graph as `.node.json` (deterministic field order).
    pub fn to_json(&self) -> String {
        serialize_json(self)
    }

    /// Parse `.node.json` text. Unknown fields are ignored; missing optional
    /// fields keep their defaults, matching the C++ loader.
    pub fn from_json(source: &str) -> Result<Self, String> {
        parse_json(source)
    }

    /// Write the emitted `.dyd` at `script_path` and its sibling
    /// `<stem>.node.json`.
    pub fn write_sibling_files(&self, script_path: &Path) -> Result<(), String> {
        let emitted = self.emit()?;
        std::fs::write(script_path, emitted.dyd)
            .map_err(|error| format!("{}: {error}", script_path.display()))?;
        let json_path = node_json_path_for_script(script_path);
        std::fs::write(&json_path, emitted.node_json)
            .map_err(|error| format!("{}: {error}", json_path.display()))?;
        Ok(())
    }

    /// Write only the `.node.json` text to `path`.
    pub fn save_node_json(&self, path: &Path) -> Result<(), String> {
        let emitted = self.emit()?;
        std::fs::write(path, emitted.node_json)
            .map_err(|error| format!("{}: {error}", path.display()))
    }

    /// Open a `.dyd` script: prefer the sibling JSON when it still emits the
    /// current script text, otherwise rebuild from `// @node` markers.
    pub fn load(script_path: &Path) -> Result<Self, String> {
        let script_name = file_name(script_path);
        let json_path = node_json_path_for_script(script_path);
        let json_graph = if json_path.exists() {
            std::fs::read_to_string(&json_path)
                .ok()
                .and_then(|text| Graph::from_json(&text).ok())
                .map(|mut graph| {
                    if graph.script_file_name.is_empty() {
                        graph.script_file_name = script_name.clone();
                    }
                    graph
                })
        } else {
            None
        };

        let dyd = std::fs::read_to_string(script_path).unwrap_or_default();
        if let Some(graph) = &json_graph {
            if !dyd.is_empty() {
                if let Ok(emitted) = emit(graph) {
                    if scripts_match(&emitted.dyd, &dyd) {
                        return Ok(graph.clone());
                    }
                }
            }
        }

        if has_node_markers(&dyd) {
            match import_from_dyd(&dyd, &script_name, json_graph.as_ref()) {
                Ok(graph) => Ok(graph),
                Err(error) => match json_graph {
                    Some(graph) => Ok(graph),
                    None => Err(error),
                },
            }
        } else if let Some(graph) = json_graph {
            Ok(graph)
        } else {
            Ok(Graph::default_for_script(&script_name))
        }
    }

    /// Rebuild this graph from script text, keeping positions for ids that
    /// survive. Errors when the source has no markers or cannot be parsed.
    pub fn sync_from_dyd(&mut self, source: &str) -> Result<(), String> {
        if !has_node_markers(source) {
            return Err("script has no // @node markers".to_owned());
        }
        let imported = import_from_dyd(source, &self.script_file_name, Some(self))?;
        *self = imported;
        Ok(())
    }

    /// Rebuild a graph from script text without a layout hint.
    pub fn import(source: &str, script_file_name: &str) -> Result<Self, String> {
        import_from_dyd(source, script_file_name, None)
    }
}

fn kind_label(kind: PinKind) -> &'static str {
    match kind {
        PinKind::Exec => "exec",
        PinKind::Number => "number",
        PinKind::Bool => "bool",
    }
}

/// Split an endpoint `"nodeId.pin"` into its parts.
pub fn parse_pin_ref(endpoint: &str) -> Option<(&str, &str)> {
    let dot = endpoint.rfind('.')?;
    if dot == 0 || dot + 1 >= endpoint.len() {
        return None;
    }
    Some((&endpoint[..dot], &endpoint[dot + 1..]))
}

/// The sibling JSON path for a script path (`player.dyd` → `player.node.json`).
pub fn node_json_path_for_script(script_path: &Path) -> PathBuf {
    let stem = script_path
        .file_stem()
        .map(|stem| stem.to_string_lossy().into_owned())
        .unwrap_or_default();
    script_path.with_file_name(format!("{stem}.node.json"))
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_default()
}

/// `true` when the text contains an emitter marker.
pub fn has_node_markers(source: &str) -> bool {
    source.contains("// @node ")
}

// ── Emitter ──────────────────────────────────────────────────────────────

/// Emit a graph exactly as the C++ `NodeEmitter` does.
pub fn emit(graph: &Graph) -> Result<EmitResult, String> {
    let mut on_update: Option<&GraphNode> = None;
    for node in &graph.nodes {
        if node.kind == NodeKind::OnUpdate {
            if on_update.is_some() {
                return Err("graph must contain exactly one OnUpdate node".to_owned());
            }
            on_update = Some(node);
        }
    }
    let on_update = on_update.ok_or_else(|| "graph is missing an OnUpdate node".to_owned())?;
    if graph.script_file_name.is_empty() {
        return Err("script file name is required".to_owned());
    }

    let mut emitter = Emitter {
        graph,
        cache: HashMap::new(),
    };
    let mut dyd = String::new();
    dyd.push_str("// @node ");
    dyd.push_str(&on_update.id);
    dyd.push('\n');
    dyd.push_str("function onUpdate(entity, dt) {\n");
    if let Some(entry) = emitter.exec_edge_from(&on_update.id, "out") {
        let (next, _) =
            parse_pin_ref(&entry.to).ok_or_else(|| "invalid exec edge from OnUpdate".to_owned())?;
        emitter.emit_exec_chain(next, &mut dyd, 1)?;
    }
    dyd.push_str("}\n");

    Ok(EmitResult {
        dyd,
        node_json: serialize_json(graph),
    })
}

struct Emitter<'a> {
    graph: &'a Graph,
    cache: HashMap<String, String>,
}

impl<'a> Emitter<'a> {
    fn exec_edge_from(&self, node_id: &str, pin: &str) -> Option<&'a GraphEdge> {
        let source = format!("{node_id}.{pin}");
        self.graph.edges.iter().find(|edge| edge.from == source)
    }

    fn edge_into(&self, node_id: &str, pin: &str) -> Option<&'a GraphEdge> {
        let target = format!("{node_id}.{pin}");
        self.graph.edges.iter().find(|edge| edge.to == target)
    }

    fn line(out: &mut String, depth: usize, text: &str) {
        for _ in 0..depth {
            out.push_str("  ");
        }
        out.push_str(text);
        out.push('\n');
    }

    fn emit_exec_chain(
        &mut self,
        node_id: &str,
        out: &mut String,
        depth: usize,
    ) -> Result<(), String> {
        let node = self
            .graph
            .find_node(node_id)
            .ok_or_else(|| format!("unknown node in exec chain: {node_id}"))?;
        match node.kind {
            NodeKind::SetPosition | NodeKind::Translate | NodeKind::SetScale => {
                Self::line(out, depth, &format!("// @node {}", node.id));
                let x = self.resolve_data_input(&node.id, "x")?;
                let y = self.resolve_data_input(&node.id, "y")?;
                let z = self.resolve_data_input(&node.id, "z")?;
                let native = match node.kind {
                    NodeKind::SetPosition => "caffeine_set_position",
                    NodeKind::Translate => "caffeine_translate",
                    _ => "caffeine_set_scale",
                };
                Self::line(out, depth, &format!("{native}(entity, {x}, {y}, {z});"));
                if let Some(next) = self.exec_edge_from(&node.id, "out") {
                    let (next_id, _) = parse_pin_ref(&next.to)
                        .ok_or_else(|| "invalid exec edge from vector action".to_owned())?;
                    self.emit_exec_chain(next_id, out, depth)?;
                }
                Ok(())
            }
            NodeKind::DestroySelf => {
                Self::line(out, depth, &format!("// @node {}", node.id));
                Self::line(out, depth, "caffeine_world_destroy(entity);");
                if let Some(next) = self.exec_edge_from(&node.id, "out") {
                    let (next_id, _) = parse_pin_ref(&next.to)
                        .ok_or_else(|| "invalid exec edge from DestroySelf".to_owned())?;
                    self.emit_exec_chain(next_id, out, depth)?;
                }
                Ok(())
            }
            NodeKind::Log => {
                Self::line(out, depth, &format!("// @node {}", node.id));
                Self::line(
                    out,
                    depth,
                    &format!("caffeine_log(\"{}\");", json_escape(&node.text_value)),
                );
                if let Some(next) = self.exec_edge_from(&node.id, "out") {
                    let (next_id, _) = parse_pin_ref(&next.to)
                        .ok_or_else(|| "invalid exec edge from Log".to_owned())?;
                    self.emit_exec_chain(next_id, out, depth)?;
                }
                Ok(())
            }
            NodeKind::Branch => {
                Self::line(out, depth, &format!("// @node {}", node.id));
                let condition = self.resolve_data_input(&node.id, "condition")?;
                Self::line(out, depth, &format!("if ({condition}) {{"));
                if let Some(then_edge) = self.exec_edge_from(&node.id, "then") {
                    let (next_id, _) = parse_pin_ref(&then_edge.to)
                        .ok_or_else(|| "invalid exec edge from Branch.then".to_owned())?;
                    self.emit_exec_chain(next_id, out, depth + 1)?;
                }
                Self::line(out, depth, "} else {");
                if let Some(else_edge) = self.exec_edge_from(&node.id, "else") {
                    let (next_id, _) = parse_pin_ref(&else_edge.to)
                        .ok_or_else(|| "invalid exec edge from Branch.else".to_owned())?;
                    self.emit_exec_chain(next_id, out, depth + 1)?;
                }
                Self::line(out, depth, "}");
                if let Some(next) = self.exec_edge_from(&node.id, "out") {
                    let (next_id, _) = parse_pin_ref(&next.to)
                        .ok_or_else(|| "invalid exec edge from Branch.out".to_owned())?;
                    self.emit_exec_chain(next_id, out, depth)?;
                }
                Ok(())
            }
            NodeKind::OnUpdate => Err("OnUpdate cannot appear inside exec chain".to_owned()),
            NodeKind::GetPosition
            | NodeKind::GetScale
            | NodeKind::DeltaTime
            | NodeKind::KeyDown
            | NodeKind::ActionDown
            | NodeKind::Axis
            | NodeKind::TotalTime
            | NodeKind::Add
            | NodeKind::Subtract
            | NodeKind::Multiply
            | NodeKind::Divide
            | NodeKind::Negate
            | NodeKind::Less
            | NodeKind::Greater
            | NodeKind::Equal
            | NodeKind::And
            | NodeKind::Or
            | NodeKind::Not
            | NodeKind::SelfEntity
            | NodeKind::IsAlive
            | NodeKind::Distance
            | NodeKind::Constant => Err(format!("data-only node in exec chain: {node_id}")),
            _ => Err(format!("data-only node in exec chain: {node_id}")),
        }
    }

    fn resolve_data_input(&mut self, node_id: &str, pin: &str) -> Result<String, String> {
        if let Some(edge) = self.edge_into(node_id, pin) {
            let (source_node, source_pin) = parse_pin_ref(&edge.from)
                .ok_or_else(|| format!("invalid data edge to {node_id}.{pin}"))?;
            return self.resolve_data_output(source_node, source_pin);
        }
        if let Some(node) = self.graph.find_node(node_id) {
            if node
                .kind
                .operator()
                .is_some_and(|op| op.input == PinKind::Bool)
            {
                return Ok("false".to_owned());
            }
            match pin {
                "x" => return Ok(format_number(node.input_x)),
                "y" => return Ok(format_number(node.input_y)),
                "z" => return Ok(format_number(node.input_z)),
                "condition" => {
                    return Ok(if node.input_condition {
                        "true"
                    } else {
                        "false"
                    }
                    .to_owned())
                }
                _ => {}
            }
        }
        Ok("0".to_owned())
    }

    fn resolve_data_output(&mut self, node_id: &str, pin: &str) -> Result<String, String> {
        let key = format!("{node_id}.{pin}");
        if let Some(cached) = self.cache.get(&key) {
            return Ok(cached.clone());
        }
        let node = self
            .graph
            .find_node(node_id)
            .ok_or_else(|| format!("unknown data node: {node_id}"))?;
        let expression = match node.kind {
            NodeKind::Constant => {
                if pin != "value" {
                    return Err("Constant only exposes pin 'value'".to_owned());
                }
                if node.constant_is_bool {
                    if node.bool_value {
                        "true".to_owned()
                    } else {
                        "false".to_owned()
                    }
                } else {
                    format_number(node.number_value)
                }
            }
            NodeKind::DeltaTime => {
                if pin != "value" {
                    return Err("DeltaTime only exposes pin 'value'".to_owned());
                }
                "dt".to_owned()
            }
            NodeKind::KeyDown | NodeKind::ActionDown | NodeKind::Axis => {
                if pin != "value" {
                    return Err(format!(
                        "{} only exposes pin 'value'",
                        node.kind.type_name()
                    ));
                }
                let native = match node.kind {
                    NodeKind::KeyDown => "caffeine_input_key_down",
                    NodeKind::ActionDown => "caffeine_input_action_down",
                    _ => "caffeine_input_axis",
                };
                format!("{native}(\"{}\")", json_escape(&node.text_value))
            }
            NodeKind::TotalTime => {
                if pin != "value" {
                    return Err("TotalTime only exposes pin 'value'".to_owned());
                }
                "caffeine_time_total()".to_owned()
            }
            NodeKind::GetPosition => match pin {
                "x" => "caffeine_get_position_x(entity)".to_owned(),
                "y" => "caffeine_get_position_y(entity)".to_owned(),
                "z" => "caffeine_get_position_z(entity)".to_owned(),
                _ => return Err("GetPosition exposes x, y, z pins".to_owned()),
            },
            NodeKind::GetScale => match pin {
                "x" => "caffeine_get_scale_x(entity)".to_owned(),
                "y" => "caffeine_get_scale_y(entity)".to_owned(),
                "z" => "caffeine_get_scale_z(entity)".to_owned(),
                _ => return Err("GetScale exposes x, y, z pins".to_owned()),
            },
            NodeKind::SelfEntity => "entity".to_owned(),
            kind if kind.operator().is_some() => {
                let Some(op) = kind.operator() else {
                    return Err(format!("node {node_id} is not an operator"));
                };
                let a = self.resolve_data_input(node_id, "a")?;
                if op.unary {
                    format!("({}{a})", op.symbol)
                } else {
                    let b = self.resolve_data_input(node_id, "b")?;
                    format!("({a} {} {b})", op.symbol)
                }
            }
            kind if kind.call_native().is_some() => {
                let Some((native, arity)) = kind.call_native() else {
                    return Err(format!("node {node_id} is not a call"));
                };
                let mut arguments = Vec::new();
                for pin in ["a", "b", "c"].into_iter().take(arity) {
                    arguments.push(self.resolve_data_input(node_id, pin)?);
                }
                format!("{native}({})", arguments.join(", "))
            }
            _ => return Err(format!("node {node_id} is not a data source")),
        };
        self.cache.insert(key, expression.clone());
        Ok(expression)
    }
}

/// Split `(a + b)`, `(!a)` or `caffeine_world_distance(a, b)` into a node kind and its operand
/// expressions. The emitter parenthesises every operator, so each has exactly one at depth 0.
fn parse_operator_expression(expression: &str) -> Option<(NodeKind, Vec<String>)> {
    let expression = expression.trim();
    for kind in NodeKind::ALL {
        let Some((native, arity)) = kind.call_native() else {
            continue;
        };
        let Some(rest) = expression
            .strip_prefix(native)
            .and_then(|rest| rest.strip_prefix('('))
        else {
            continue;
        };
        let inner = rest.strip_suffix(')')?;
        let parts = if inner.trim().is_empty() {
            Vec::new()
        } else {
            split_top_level(inner, ',')
        };
        return (parts.len() == arity).then_some((kind, parts));
    }
    let inner = expression.strip_prefix('(')?.strip_suffix(')')?;
    // The leading `(` must close at the very end, not earlier (`(a) + (b)`).
    let mut depth = 0i32;
    for (index, character) in expression.char_indices() {
        match character {
            '(' => depth += 1,
            ')' => {
                depth -= 1;
                if depth == 0 && index + 1 != expression.len() {
                    return None;
                }
            }
            _ => {}
        }
    }
    let inner = inner.trim();
    if let Some(operand) = inner.strip_prefix('!') {
        return Some((NodeKind::Not, vec![operand.trim().to_owned()]));
    }
    if let Some(operand) = inner.strip_prefix('-') {
        // `-5` is a constant; only `-` before an expression is a Negate node.
        if operand.trim_start().starts_with(['(', '_'])
            || operand.trim_start().starts_with(char::is_alphabetic)
        {
            return Some((NodeKind::Negate, vec![operand.trim().to_owned()]));
        }
    }
    for (symbol, kind) in [
        (" && ", NodeKind::And),
        (" || ", NodeKind::Or),
        (" == ", NodeKind::Equal),
        (" < ", NodeKind::Less),
        (" > ", NodeKind::Greater),
        (" + ", NodeKind::Add),
        (" - ", NodeKind::Subtract),
        (" * ", NodeKind::Multiply),
        (" / ", NodeKind::Divide),
    ] {
        let parts = split_top_level(inner, symbol);
        if parts.len() == 2 {
            return Some((kind, parts));
        }
    }
    None
}

/// Split `text` at the separator where it appears outside parentheses and quotes.
fn split_top_level<P: SplitPattern>(text: &str, separator: P) -> Vec<String> {
    let mut parts = Vec::new();
    let mut depth = 0i32;
    let mut quoted = false;
    let mut start = 0usize;
    let mut index = 0usize;
    while index < text.len() {
        let rest = &text[index..];
        let character = rest.chars().next().unwrap_or(' ');
        match character {
            '"' => quoted = !quoted,
            '(' if !quoted => depth += 1,
            ')' if !quoted => depth -= 1,
            _ => {}
        }
        if depth == 0 && !quoted {
            if let Some(width) = separator.matches_at(rest) {
                parts.push(text[start..index].trim().to_owned());
                index += width;
                start = index;
                continue;
            }
        }
        index += character.len_utf8();
    }
    parts.push(text[start..].trim().to_owned());
    parts
}

trait SplitPattern {
    fn matches_at(&self, rest: &str) -> Option<usize>;
}

impl SplitPattern for char {
    fn matches_at(&self, rest: &str) -> Option<usize> {
        rest.starts_with(*self).then(|| self.len_utf8())
    }
}

impl SplitPattern for &str {
    fn matches_at(&self, rest: &str) -> Option<usize> {
        rest.starts_with(*self).then_some(self.len())
    }
}

/// Recognise `caffeine_input_key_down("Space")` style calls and return the node kind
/// plus the quoted name.
fn input_call(expression: &str) -> Option<(NodeKind, String)> {
    for (native, kind) in [
        ("caffeine_input_key_down", NodeKind::KeyDown),
        ("caffeine_input_action_down", NodeKind::ActionDown),
        ("caffeine_input_axis", NodeKind::Axis),
    ] {
        let Some(rest) = expression.strip_prefix(native) else {
            continue;
        };
        let inner = rest.strip_prefix("(\"")?.strip_suffix("\")")?;
        if inner.contains('"') {
            return None;
        }
        return Some((kind, inner.replace("\\\\", "\\")));
    }
    None
}

fn json_escape(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 8);
    for character in text.chars() {
        match character {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(character),
        }
    }
    out
}

/// Deterministic `.node.json`, byte-compatible with the C++ serializer.
fn serialize_json(graph: &Graph) -> String {
    let mut nodes: Vec<&GraphNode> = graph.nodes.iter().collect();
    nodes.sort_by(|a, b| a.id.cmp(&b.id));
    let mut edges: Vec<&GraphEdge> = graph.edges.iter().collect();
    edges.sort_by(|a, b| a.from.cmp(&b.from).then_with(|| a.to.cmp(&b.to)));

    let mut json = String::new();
    json.push_str("{\n");
    json.push_str(&format!("  \"version\": {},\n", graph.version));
    json.push_str(&format!(
        "  \"script\": \"{}\",\n",
        json_escape(&graph.script_file_name)
    ));
    json.push_str("  \"nodes\": [\n");
    for (index, node) in nodes.iter().enumerate() {
        json.push_str(&format!(
            "    {{ \"id\": \"{}\", \"type\": \"{}\", \"x\": {}, \"y\": {}",
            json_escape(&node.id),
            node.kind.type_name(),
            format_f32(node.x),
            format_f32(node.y)
        ));
        if node.kind == NodeKind::Constant {
            if node.constant_is_bool {
                json.push_str(&format!(
                    ", \"valueKind\": \"bool\", \"value\": {}",
                    if node.bool_value { "true" } else { "false" }
                ));
            } else {
                json.push_str(&format!(
                    ", \"valueKind\": \"number\", \"value\": {}",
                    format_number(node.number_value)
                ));
            }
        }
        if matches!(
            node.kind,
            NodeKind::SetPosition | NodeKind::Translate | NodeKind::SetScale
        ) {
            json.push_str(&format!(
                ", \"inputX\": {}, \"inputY\": {}, \"inputZ\": {}",
                format_number(node.input_x),
                format_number(node.input_y),
                format_number(node.input_z)
            ));
        }
        if node.kind == NodeKind::Branch {
            json.push_str(&format!(
                ", \"condition\": {}",
                if node.input_condition {
                    "true"
                } else {
                    "false"
                }
            ));
        }
        if matches!(
            node.kind,
            NodeKind::Log | NodeKind::KeyDown | NodeKind::ActionDown | NodeKind::Axis
        ) {
            json.push_str(&format!(
                ", \"text\": \"{}\"",
                json_escape(&node.text_value)
            ));
        }
        json.push_str(" }");
        if index + 1 < nodes.len() {
            json.push(',');
        }
        json.push('\n');
    }
    json.push_str("  ],\n");
    json.push_str("  \"edges\": [\n");
    for (index, edge) in edges.iter().enumerate() {
        json.push_str(&format!(
            "    {{ \"from\": \"{}\", \"to\": \"{}\" }}",
            json_escape(&edge.from),
            json_escape(&edge.to)
        ));
        if index + 1 < edges.len() {
            json.push(',');
        }
        json.push('\n');
    }
    json.push_str("  ]\n");
    json.push_str("}\n");
    json
}

fn parse_json(source: &str) -> Result<Graph, String> {
    let value: serde_json::Value =
        serde_json::from_str(source).map_err(|error| format!("invalid node JSON: {error}"))?;
    let version = value
        .get("version")
        .and_then(|version| version.as_i64())
        .unwrap_or(1) as i32;
    let script_file_name = value
        .get("script")
        .and_then(|script| script.as_str())
        .unwrap_or_default()
        .to_owned();
    let mut graph = Graph {
        version,
        script_file_name,
        nodes: Vec::new(),
        edges: Vec::new(),
    };
    if let Some(nodes) = value.get("nodes").and_then(|nodes| nodes.as_array()) {
        for entry in nodes {
            let id = entry
                .get("id")
                .and_then(|id| id.as_str())
                .ok_or_else(|| "node entry without id".to_owned())?;
            let type_name = entry
                .get("type")
                .and_then(|kind| kind.as_str())
                .ok_or_else(|| format!("node '{id}' without type"))?;
            let kind = NodeKind::from_type_name(type_name)
                .ok_or_else(|| format!("unknown node type '{type_name}'"))?;
            let mut node = GraphNode::new(
                id.to_owned(),
                kind,
                entry.get("x").and_then(|x| x.as_f64()).unwrap_or(0.0) as f32,
                entry.get("y").and_then(|y| y.as_f64()).unwrap_or(0.0) as f32,
            );
            if let Some(number) = entry.get("inputX").and_then(|value| value.as_f64()) {
                node.input_x = number;
            }
            if let Some(number) = entry.get("inputY").and_then(|value| value.as_f64()) {
                node.input_y = number;
            }
            if let Some(number) = entry.get("inputZ").and_then(|value| value.as_f64()) {
                node.input_z = number;
            }
            if let Some(flag) = entry.get("condition").and_then(|value| value.as_bool()) {
                node.input_condition = flag;
            }
            if kind == NodeKind::Constant {
                let value_kind = entry
                    .get("valueKind")
                    .and_then(|value| value.as_str())
                    .unwrap_or_default();
                let value = entry.get("value");
                if value_kind == "bool" || value.is_some_and(|value| value.is_boolean()) {
                    node.constant_is_bool = true;
                    node.bool_value = value.and_then(|value| value.as_bool()).unwrap_or(false);
                } else if let Some(number) = value.and_then(|value| value.as_f64()) {
                    node.number_value = number;
                }
            }
            if matches!(
                kind,
                NodeKind::Log | NodeKind::KeyDown | NodeKind::ActionDown | NodeKind::Axis
            ) {
                if let Some(text) = entry.get("text").and_then(|text| text.as_str()) {
                    node.text_value = text.to_owned();
                }
            }
            graph.nodes.push(node);
        }
    }
    if let Some(edges) = value.get("edges").and_then(|edges| edges.as_array()) {
        for entry in edges {
            let from = entry
                .get("from")
                .and_then(|from| from.as_str())
                .ok_or_else(|| "edge entry without from".to_owned())?;
            let to = entry
                .get("to")
                .and_then(|to| to.as_str())
                .ok_or_else(|| "edge entry without to".to_owned())?;
            graph.edges.push(GraphEdge {
                from: from.to_owned(),
                to: to.to_owned(),
            });
        }
    }
    Ok(graph)
}

// ── Import from `.dyd` ───────────────────────────────────────────────────

/// Rebuild a graph from emitter-shaped `.dyd` text. `layout_hint` supplies
/// positions for ids that survive and places imported helper nodes below the
/// graph. Returns `NoMarkers`-style errors when the text is hand-written
/// without markers.
pub fn import_from_dyd(
    source: &str,
    script_file_name: &str,
    layout_hint: Option<&Graph>,
) -> Result<Graph, String> {
    if !has_node_markers(source) {
        return Err("no // @node markers in script".to_owned());
    }
    let mut importer = DydImporter::new(source, script_file_name);
    importer.run()?;
    let mut graph = importer.graph;
    if let Some(hint) = layout_hint {
        merge_graph_layout(&mut graph, hint);
    }
    Ok(graph)
}

/// Copy positions from `hint` for ids present in both graphs. Imported helper
/// nodes (`import_*`) are stacked under the graph.
pub fn merge_graph_layout(graph: &mut Graph, hint: &Graph) {
    let mut orphan_y = 200.0f32;
    for node in &mut graph.nodes {
        if let Some(hint_node) = hint.find_node(&node.id) {
            node.x = hint_node.x;
            node.y = hint_node.y;
            continue;
        }
        if node.id.starts_with("import_") {
            node.x = 40.0;
            node.y = orphan_y;
            orphan_y += 48.0;
        }
    }
}

/// Compare script text ignoring line-ending and trailing-whitespace noise,
/// matching the C++ panel.
pub fn scripts_match(a: &str, b: &str) -> bool {
    fn normalize(text: &str) -> String {
        let mut out: String = text
            .chars()
            .filter(|character| *character != '\r')
            .collect();
        while out.ends_with('\n') || out.ends_with(' ') || out.ends_with('\t') {
            out.pop();
        }
        out
    }
    normalize(a) == normalize(b)
}

struct DydImporter<'a> {
    source: &'a str,
    pos: usize,
    graph: Graph,
    serial: u32,
    get_position_id: Option<String>,
    get_scale_id: Option<String>,
    delta_time_id: Option<String>,
    expr_to_constant: HashMap<String, String>,
}

impl<'a> DydImporter<'a> {
    fn new(source: &'a str, script_file_name: &str) -> Self {
        Self {
            source,
            pos: 0,
            graph: Graph::new(script_file_name),
            serial: 0,
            get_position_id: None,
            get_scale_id: None,
            delta_time_id: None,
            expr_to_constant: HashMap::new(),
        }
    }

    fn run(&mut self) -> Result<(), String> {
        self.skip_ws();
        let on_update_id = self
            .read_node_marker()
            .ok_or_else(|| "missing OnUpdate @node marker".to_owned())?;
        self.ensure_node(&on_update_id, NodeKind::OnUpdate);
        self.consume("function")
            .then_some(())
            .ok_or_else(|| "expected function onUpdate".to_owned())?;
        self.consume("onUpdate")
            .then_some(())
            .ok_or_else(|| "expected onUpdate".to_owned())?;
        self.consume("(")
            .then_some(())
            .ok_or_else(|| "expected (".to_owned())?;
        self.consume("entity")
            .then_some(())
            .ok_or_else(|| "expected entity".to_owned())?;
        self.consume(",")
            .then_some(())
            .ok_or_else(|| "expected ,".to_owned())?;
        self.consume("dt")
            .then_some(())
            .ok_or_else(|| "expected dt".to_owned())?;
        self.consume(")")
            .then_some(())
            .ok_or_else(|| "expected )".to_owned())?;
        self.consume("{")
            .then_some(())
            .ok_or_else(|| "expected {".to_owned())?;

        let entry = format!("{on_update_id}.out");
        self.parse_statement_list(&entry)?;

        self.consume("}")
            .then_some(())
            .ok_or_else(|| "expected closing }".to_owned())?;
        self.skip_ws();
        if self.pos < self.source.len() {
            return Err("unexpected trailing input".to_owned());
        }
        Ok(())
    }

    fn peek(&self) -> char {
        self.source[self.pos..].chars().next().unwrap_or('\0')
    }

    fn rest(&self) -> &'a str {
        &self.source[self.pos..]
    }

    fn skip_ws(&mut self) {
        while self.pos < self.source.len() {
            let rest = self.rest();
            if let Some(character) = rest.chars().next() {
                if character.is_whitespace() {
                    self.pos += character.len_utf8();
                    continue;
                }
            }
            if rest.starts_with("//") {
                if rest.starts_with("// @node") {
                    break;
                }
                match rest.find('\n') {
                    Some(offset) => self.pos += offset,
                    None => self.pos = self.source.len(),
                }
                continue;
            }
            break;
        }
    }

    fn consume(&mut self, literal: &str) -> bool {
        self.skip_ws();
        if self.rest().starts_with(literal) {
            self.pos += literal.len();
            true
        } else {
            false
        }
    }

    fn read_node_marker(&mut self) -> Option<String> {
        self.skip_ws();
        if !self.consume("//") {
            return None;
        }
        if !self.consume("@node") {
            return None;
        }
        self.skip_ws();
        let rest = self.rest();
        let end = rest.find(char::is_whitespace).unwrap_or(rest.len());
        let id = rest[..end].to_owned();
        self.pos += end;
        if id.is_empty() {
            None
        } else {
            Some(id)
        }
    }

    fn ensure_node(&mut self, id: &str, kind: NodeKind) -> &mut GraphNode {
        if self.graph.find_node(id).is_none() {
            self.graph
                .nodes
                .push(GraphNode::new(id.to_owned(), kind, 0.0, 0.0));
        }
        self.graph
            .find_node_mut(id)
            .expect("node was just inserted")
    }

    fn add_edge(&mut self, from: String, to: String) {
        if self
            .graph
            .edges
            .iter()
            .any(|edge| edge.from == from && edge.to == to)
        {
            return;
        }
        self.graph.edges.push(GraphEdge { from, to });
    }

    fn parse_statement_list(&mut self, exec_in: &str) -> Result<String, String> {
        let mut exec_out = exec_in.to_owned();
        loop {
            self.skip_ws();
            if self.peek() == '}' {
                return Ok(exec_out);
            }
            let mark = self.pos;
            let Some(node_id) = self.read_node_marker() else {
                self.pos = mark;
                return Ok(exec_out);
            };
            if self.consume("caffeine_set_position") {
                exec_out = self.parse_vector_call(&node_id, NodeKind::SetPosition, &exec_out)?;
            } else if self.consume("caffeine_translate") {
                exec_out = self.parse_vector_call(&node_id, NodeKind::Translate, &exec_out)?;
            } else if self.consume("caffeine_set_scale") {
                exec_out = self.parse_vector_call(&node_id, NodeKind::SetScale, &exec_out)?;
            } else if self.consume("caffeine_world_destroy") {
                exec_out = self.parse_destroy(&node_id, &exec_out)?;
            } else if self.consume("caffeine_log") {
                exec_out = self.parse_log(&node_id, &exec_out)?;
            } else if self.consume("if") {
                exec_out = self.parse_branch(&node_id, &exec_out)?;
            } else {
                return Err("unsupported statement after @node marker".to_owned());
            }
        }
    }

    fn parse_vector_call(
        &mut self,
        node_id: &str,
        kind: NodeKind,
        exec_out: &str,
    ) -> Result<String, String> {
        self.ensure_node(node_id, kind);
        if exec_out.is_empty() {
            return Err("vector node without exec input".to_owned());
        }
        self.add_edge(exec_out.to_owned(), format!("{node_id}.in"));

        if !self.consume("(") {
            return Err("expected ( after vector call".to_owned());
        }
        if !self.consume("entity") {
            return Err("expected entity".to_owned());
        }
        if !self.consume(",") {
            return Err("expected ,".to_owned());
        }
        let x = self.read_argument()?;
        let y = self.read_argument()?;
        let z = self.read_argument()?;
        if !self.consume(")") {
            return Err("expected )".to_owned());
        }
        if !self.consume(";") {
            return Err("expected ;".to_owned());
        }

        self.wire_data_expr(&x, node_id, "x")?;
        self.wire_data_expr(&y, node_id, "y")?;
        self.wire_data_expr(&z, node_id, "z")?;

        Ok(format!("{node_id}.out"))
    }

    fn parse_destroy(&mut self, node_id: &str, exec_out: &str) -> Result<String, String> {
        self.ensure_node(node_id, NodeKind::DestroySelf);
        if exec_out.is_empty() {
            return Err("DestroySelf without exec input".to_owned());
        }
        self.add_edge(exec_out.to_owned(), format!("{node_id}.in"));
        for expected in ["(", "entity", ")", ";"] {
            if !self.consume(expected) {
                return Err(format!("expected {expected} after caffeine_world_destroy"));
            }
        }
        Ok(format!("{node_id}.out"))
    }

    fn parse_log(&mut self, node_id: &str, exec_out: &str) -> Result<String, String> {
        self.ensure_node(node_id, NodeKind::Log);
        if exec_out.is_empty() {
            return Err("Log without exec input".to_owned());
        }
        self.add_edge(exec_out.to_owned(), format!("{node_id}.in"));

        if !self.consume("(") {
            return Err("expected ( after caffeine_log".to_owned());
        }
        self.skip_ws();
        if self.peek() != '"' {
            return Err("expected a string argument to caffeine_log".to_owned());
        }
        self.pos += 1;
        let mut text = String::new();
        while self.peek() != '"' && self.pos < self.source.len() {
            let character = self.peek();
            self.pos += character.len_utf8();
            if character == '\\' && self.pos < self.source.len() {
                let escaped = self.peek();
                self.pos += escaped.len_utf8();
                match escaped {
                    'n' => text.push('\n'),
                    'r' => text.push('\r'),
                    't' => text.push('\t'),
                    _ => text.push(escaped),
                }
            } else {
                text.push(character);
            }
        }
        if self.pos >= self.source.len() {
            return Err("unterminated caffeine_log string".to_owned());
        }
        self.pos += 1;
        if !self.consume(")") {
            return Err("expected ) after caffeine_log".to_owned());
        }
        if !self.consume(";") {
            return Err("expected ; after caffeine_log".to_owned());
        }
        if let Some(node) = self.graph.find_node_mut(node_id) {
            node.text_value = text;
        }
        Ok(format!("{node_id}.out"))
    }

    fn parse_branch(&mut self, node_id: &str, exec_out: &str) -> Result<String, String> {
        self.ensure_node(node_id, NodeKind::Branch);
        if exec_out.is_empty() {
            return Err("Branch without exec input".to_owned());
        }
        self.add_edge(exec_out.to_owned(), format!("{node_id}.in"));

        if !self.consume("(") {
            return Err("expected ( after if".to_owned());
        }
        let condition_start = self.pos;
        let mut depth = 1usize;
        while self.pos < self.source.len() && depth > 0 {
            let character = self.peek();
            self.pos += character.len_utf8();
            match character {
                '(' => depth += 1,
                ')' => depth -= 1,
                _ => {}
            }
        }
        if depth != 0 {
            return Err("unbalanced if condition".to_owned());
        }
        let condition = self.source[condition_start..self.pos - 1].trim().to_owned();
        self.wire_data_expr(&condition, node_id, "condition")?;

        if !self.consume("{") {
            return Err("expected { after if condition".to_owned());
        }
        let _then_out = self.parse_statement_list(&format!("{node_id}.then"))?;
        if !self.consume("}") {
            return Err("expected } after then branch".to_owned());
        }
        if !self.consume("else") {
            return Err("expected else".to_owned());
        }
        if !self.consume("{") {
            return Err("expected { after else".to_owned());
        }
        let _else_out = self.parse_statement_list(&format!("{node_id}.else"))?;
        if !self.consume("}") {
            return Err("expected } after else branch".to_owned());
        }
        Ok(format!("{node_id}.out"))
    }

    fn read_argument(&mut self) -> Result<String, String> {
        self.skip_ws();
        let start = self.pos;
        let mut depth = 0usize;
        while self.pos < self.source.len() {
            let character = self.peek();
            match character {
                '(' => depth += 1,
                ')' => {
                    if depth == 0 {
                        break;
                    }
                    depth -= 1;
                }
                ',' if depth == 0 => {
                    let argument = self.source[start..self.pos].trim().to_owned();
                    self.pos += 1;
                    return Ok(argument);
                }
                _ => {}
            }
            self.pos += character.len_utf8();
        }
        if depth != 0 {
            return Err("unbalanced parentheses in argument".to_owned());
        }
        let argument = self.source[start..self.pos].trim().to_owned();
        if argument.is_empty() {
            Err("empty argument".to_owned())
        } else {
            Ok(argument)
        }
    }

    fn get_position_node(&mut self) -> String {
        if let Some(id) = &self.get_position_id {
            return id.clone();
        }
        let id = format!("import_g{}", self.serial);
        self.serial += 1;
        self.ensure_node(&id, NodeKind::GetPosition);
        self.get_position_id = Some(id.clone());
        id
    }

    fn get_scale_node(&mut self) -> String {
        if let Some(id) = &self.get_scale_id {
            return id.clone();
        }
        let id = format!("import_gs{}", self.serial);
        self.serial += 1;
        self.ensure_node(&id, NodeKind::GetScale);
        self.get_scale_id = Some(id.clone());
        id
    }

    fn wire_data_expr(&mut self, expression: &str, to_node: &str, pin: &str) -> Result<(), String> {
        let expression = expression.trim();
        let position = match expression {
            "caffeine_get_position_x(entity)" => Some("x"),
            "caffeine_get_position_y(entity)" => Some("y"),
            "caffeine_get_position_z(entity)" => Some("z"),
            _ => None,
        };
        if let Some(pin_name) = position {
            let source = self.get_position_node();
            self.add_edge(format!("{source}.{pin_name}"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        let scale = match expression {
            "caffeine_get_scale_x(entity)" => Some("x"),
            "caffeine_get_scale_y(entity)" => Some("y"),
            "caffeine_get_scale_z(entity)" => Some("z"),
            _ => None,
        };
        if let Some(pin_name) = scale {
            let source = self.get_scale_node();
            self.add_edge(format!("{source}.{pin_name}"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        if expression == "entity" {
            let id = format!("import_self{}", self.serial);
            self.serial += 1;
            self.ensure_node(&id, NodeKind::SelfEntity);
            self.add_edge(format!("{id}.value"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        if let Some((kind, parts)) = parse_operator_expression(expression) {
            let id = format!("import_op{}", self.serial);
            self.serial += 1;
            self.ensure_node(&id, kind);
            let inputs = ["a", "b", "c"];
            for (part, input) in parts.iter().zip(inputs) {
                self.wire_data_expr(part, &id, input)?;
            }
            self.add_edge(format!("{id}.value"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        if expression == "dt" {
            if self.delta_time_id.is_none() {
                let id = format!("import_dt{}", self.serial);
                self.serial += 1;
                self.ensure_node(&id, NodeKind::DeltaTime);
                self.delta_time_id = Some(id);
            }
            let source = self.delta_time_id.clone().unwrap_or_default();
            self.add_edge(format!("{source}.value"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        if let Some((kind, text)) = input_call(expression) {
            let source = match self.expr_to_constant.get(expression).cloned() {
                Some(existing) => existing,
                None => {
                    let id = format!("import_i{}", self.serial);
                    self.serial += 1;
                    let node = self.ensure_node(&id, kind);
                    node.text_value = text;
                    self.expr_to_constant
                        .insert(expression.to_owned(), id.clone());
                    id
                }
            };
            self.add_edge(format!("{source}.value"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        if expression == "caffeine_time_total()" {
            let source = match self.expr_to_constant.get(expression).cloned() {
                Some(existing) => existing,
                None => {
                    let id = format!("import_tt{}", self.serial);
                    self.serial += 1;
                    self.ensure_node(&id, NodeKind::TotalTime);
                    self.expr_to_constant
                        .insert(expression.to_owned(), id.clone());
                    id
                }
            };
            self.add_edge(format!("{source}.value"), format!("{to_node}.{pin}"));
            return Ok(());
        }
        if let Some(constant) = self.expr_to_constant.get(expression).cloned() {
            self.add_edge(format!("{constant}.value"), format!("{to_node}.{pin}"));
            return Ok(());
        }

        let constant_id = format!("import_c{}", self.serial);
        self.serial += 1;
        if expression == "true" || expression == "false" {
            let node = self.ensure_node(&constant_id, NodeKind::Constant);
            node.constant_is_bool = true;
            node.bool_value = expression == "true";
        } else if let Ok(number) = expression.parse::<f64>() {
            let node = self.ensure_node(&constant_id, NodeKind::Constant);
            node.number_value = number;
        } else {
            return Err("unsupported data expression".to_owned());
        }
        self.expr_to_constant
            .insert(expression.to_owned(), constant_id.clone());
        self.add_edge(format!("{constant_id}.value"), format!("{to_node}.{pin}"));
        Ok(())
    }
}

// ── Editor state ─────────────────────────────────────────────────────────

/// Editor state for one `.dyd` file. The window owns this; panels read it.
#[derive(Debug, Clone)]
pub struct State {
    pub graph: Option<Graph>,
    pub script_path: Option<PathBuf>,
    pub script_relative: String,
    pub selected: Option<String>,
    pub selected_link: Option<(String, String)>,
    pub search: String,
    pub status: Option<String>,
    pub pan: Vector,
    pub zoom: f32,
    pub dirty: bool,
    /// In-progress text edits keyed by `node:field`. Keeping them here lets a
    /// half-typed number (`"0."`) stay visible until it parses.
    pub drafts: HashMap<String, String>,
}

impl Default for State {
    fn default() -> Self {
        Self::new()
    }
}

impl State {
    pub fn new() -> Self {
        Self {
            graph: None,
            script_path: None,
            script_relative: String::new(),
            selected: None,
            selected_link: None,
            search: String::new(),
            status: None,
            pan: Vector::new(0.0, 0.0),
            zoom: 1.0,
            dirty: false,
            drafts: HashMap::new(),
        }
    }

    /// Key used by [`State::draft`] / [`State::set_draft`].
    pub fn draft_key(node: &str, field: &str) -> String {
        format!("{node}:{field}")
    }

    pub fn draft(&self, node: &str, field: &str) -> Option<&str> {
        self.drafts
            .get(&Self::draft_key(node, field))
            .map(String::as_str)
    }

    pub fn set_draft(&mut self, node: &str, field: &str, text: String) {
        self.drafts.insert(Self::draft_key(node, field), text);
    }

    pub fn clear_drafts(&mut self) {
        self.drafts.clear();
    }

    /// Open a `.dyd`, preferring the sibling JSON when it matches the script.
    pub fn open(
        &mut self,
        path: impl Into<PathBuf>,
        relative: Option<String>,
    ) -> Result<(), String> {
        let path = path.into();
        if !path.exists() {
            let message = format!("{} does not exist.", path.display());
            self.status = Some(message.clone());
            return Err(message);
        }
        let relative = relative.unwrap_or_else(|| file_name(&path));
        self.script_relative = relative.clone();
        self.script_path = Some(path.clone());
        self.selected = None;
        self.selected_link = None;
        self.drafts.clear();
        self.dirty = false;
        self.status = None;

        match Graph::load(&path) {
            Ok(graph) => {
                self.graph = Some(graph);
                self.status = Some(format!("Opened {relative}."));
                self.fit_view();
                Ok(())
            }
            Err(error) => {
                self.graph = None;
                self.status = Some(error.clone());
                Err(error)
            }
        }
    }

    /// Write both files next to the open script.
    pub fn save(&mut self) -> Result<(), String> {
        let Some(path) = self.script_path.clone() else {
            let message = "No script open — open a .dyd from Assets first.".to_owned();
            self.status = Some(message.clone());
            return Err(message);
        };
        let Some(graph) = self.graph.as_mut() else {
            let message = "No graph open.".to_owned();
            self.status = Some(message.clone());
            return Err(message);
        };
        graph.script_file_name = file_name(&path);
        match graph.write_sibling_files(&path) {
            Ok(()) => {
                self.dirty = false;
                self.status = Some(format!("Saved {}.", file_name(&path)));
                Ok(())
            }
            Err(error) => {
                self.status = Some(error.clone());
                Err(error)
            }
        }
    }

    /// Re-read the `.dyd` and rebuild the canvas from its markers.
    pub fn sync_from_dyd(&mut self) -> Result<(), String> {
        let Some(path) = self.script_path.clone() else {
            let message = "No script open.".to_owned();
            self.status = Some(message.clone());
            return Err(message);
        };
        let source = std::fs::read_to_string(&path)
            .map_err(|error| format!("{}: {error}", path.display()))?;
        if !has_node_markers(&source) {
            let message =
                "Script has no // @node markers (hand-edited Dryad is still valid).".to_owned();
            self.status = Some(message.clone());
            return Err(message);
        }
        let Some(graph) = self.graph.as_mut() else {
            let message = "No graph open.".to_owned();
            self.status = Some(message.clone());
            return Err(message);
        };
        match graph.sync_from_dyd(&source) {
            Ok(()) => {
                // The canvas now matches the .dyd, but merged positions and
                // helper-node layout still need a Save to reach the JSON.
                self.dirty = true;
                self.drafts.clear();
                if let (Some(selected), Some(graph)) = (&self.selected, &self.graph) {
                    if graph.find_node(selected).is_none() {
                        self.selected = None;
                    }
                }
                self.status = Some("Canvas rebuilt from // @node markers.".to_owned());
                Ok(())
            }
            Err(error) => {
                self.status = Some(error.clone());
                Err(error)
            }
        }
    }

    /// Add a node near the end of the graph and select it.
    ///
    /// A graph may only hold one `OnUpdate`; when one already exists the
    /// existing node stays selected and is returned.
    pub fn add_node(&mut self, kind: NodeKind) -> String {
        let Some(graph) = self.graph.as_mut() else {
            return String::new();
        };
        if kind == NodeKind::OnUpdate {
            if let Some(existing) = graph.on_update() {
                let id = existing.id.clone();
                self.selected = Some(id.clone());
                self.status = Some("Only one OnUpdate node is allowed.".to_owned());
                return id;
            }
        }
        let index = graph.nodes.len();
        let x = 60.0 + (index % 4) as f32 * 220.0;
        let y = 60.0 + (index / 4) as f32 * 170.0;
        let id = graph.add_node(kind, x, y);
        self.selected = Some(id.clone());
        self.selected_link = None;
        self.dirty = true;
        self.status = Some(format!("Added {}.", kind.display_name()));
        id
    }

    /// Delete the selected link, or the selected node (never the last
    /// `OnUpdate`).
    pub fn delete_selected(&mut self) {
        if let Some((from, to)) = self.selected_link.take() {
            if let Some(graph) = self.graph.as_mut() {
                graph.disconnect(&from, &to);
                self.dirty = true;
                self.status = Some("Deleted link.".to_owned());
            }
            return;
        }
        let Some(selected) = self.selected.clone() else {
            self.status = Some("Select a node or link first.".to_owned());
            return;
        };
        let Some(graph) = self.graph.as_mut() else {
            return;
        };
        let removes_on_update = graph
            .find_node(&selected)
            .is_some_and(|node| node.kind == NodeKind::OnUpdate);
        if removes_on_update
            && graph
                .nodes
                .iter()
                .filter(|node| node.kind == NodeKind::OnUpdate)
                .count()
                <= 1
        {
            self.status = Some("The graph needs one OnUpdate node.".to_owned());
            return;
        }
        graph.remove_node(&selected);
        self.selected = None;
        self.dirty = true;
        self.status = Some("Deleted node.".to_owned());
    }

    /// Connect two endpoints (`"n1.out"` → `"n2.in"`). Invalid links leave
    /// the graph untouched and report the reason in `status`.
    pub fn connect(&mut self, from: &str, to: &str) -> Result<(), String> {
        let Some(graph) = self.graph.as_mut() else {
            let message = "No graph open.".to_owned();
            self.status = Some(message.clone());
            return Err(message);
        };
        match graph.connect(from, to) {
            Ok(()) => {
                self.selected_link = Some((from.to_owned(), to.to_owned()));
                self.dirty = true;
                self.status = Some("Connected.".to_owned());
                Ok(())
            }
            Err(error) => {
                self.status = Some(error.clone());
                Err(error)
            }
        }
    }

    /// Offset a node's canvas position.
    pub fn move_node(&mut self, id: &str, dx: f32, dy: f32) {
        if let Some(node) = self
            .graph
            .as_mut()
            .and_then(|graph| graph.find_node_mut(id))
        {
            node.x += dx;
            node.y += dy;
            self.dirty = true;
        }
    }

    /// Edit a numeric field. Fields: `"value"` (Constant), `"x"`, `"y"`,
    /// `"z"` (vector inline fallbacks).
    pub fn set_number(&mut self, node: &str, field: &str, value: f64) {
        let Some(node) = self
            .graph
            .as_mut()
            .and_then(|graph| graph.find_node_mut(node))
        else {
            return;
        };
        match field {
            "value" => node.number_value = value,
            "x" => node.input_x = value,
            "y" => node.input_y = value,
            "z" => node.input_z = value,
            _ => return,
        }
        self.dirty = true;
    }

    /// Edit a boolean field. Fields: `"value"` (Constant value),
    /// `"condition"` (Branch fallback), `"constant_is_bool"` (Constant mode).
    pub fn set_bool(&mut self, node: &str, field: &str, value: bool) {
        let Some(node) = self
            .graph
            .as_mut()
            .and_then(|graph| graph.find_node_mut(node))
        else {
            return;
        };
        match field {
            "value" => node.bool_value = value,
            "condition" => node.input_condition = value,
            "constant_is_bool" => node.constant_is_bool = value,
            _ => return,
        }
        self.dirty = true;
    }

    /// Edit a Log node's message.
    pub fn set_text(&mut self, node: &str, value: String) {
        let Some(node) = self
            .graph
            .as_mut()
            .and_then(|graph| graph.find_node_mut(node))
        else {
            return;
        };
        node.text_value = value;
        self.dirty = true;
    }

    /// Build the `OnUpdate → Translate` example with `DeltaTime` on Y.
    pub fn add_motion_example(&mut self) {
        let Some(graph) = self.graph.as_mut() else {
            return;
        };
        let on_update_id = match graph.on_update() {
            Some(node) => node.id.clone(),
            None => graph.add_node(NodeKind::OnUpdate, 40.0, 40.0),
        };
        let (base_x, base_y) = graph
            .find_node(&on_update_id)
            .map(|node| (node.x, node.y))
            .unwrap_or((40.0, 40.0));

        let delta = graph.add_node(NodeKind::DeltaTime, base_x + 80.0, base_y + 110.0);
        let translate = graph.add_node(NodeKind::Translate, base_x + 220.0, base_y);
        let _ = graph.connect(&format!("{on_update_id}.out"), &format!("{translate}.in"));
        let _ = graph.connect(&format!("{delta}.value"), &format!("{translate}.y"));
        self.selected = Some(translate);
        self.dirty = true;
        self.status = Some("Added a motion example: OnUpdate → Translate by dt on Y.".to_owned());
    }

    /// Reset pan/zoom so the graph starts at the canvas origin. The panel
    /// owns the canvas size, so this does not try to centre precisely.
    pub fn fit_view(&mut self) {
        self.zoom = 1.0;
        let Some(graph) = self.graph.as_ref() else {
            self.pan = Vector::new(0.0, 0.0);
            return;
        };
        let (min_x, min_y) = graph
            .nodes
            .iter()
            .fold((f32::INFINITY, f32::INFINITY), |(min_x, min_y), node| {
                (min_x.min(node.x), min_y.min(node.y))
            });
        if min_x.is_finite() && min_y.is_finite() {
            self.pan = Vector::new(40.0 - min_x, 40.0 - min_y);
        } else {
            self.pan = Vector::new(0.0, 0.0);
        }
    }
}

// ── Number formatting ────────────────────────────────────────────────────

/// C `%.17g`, used for every emitted/JSON numeric value.
pub fn format_number(value: f64) -> String {
    format_general(value, 17)
}

/// C default `%g` (6 significant digits), used for f32 canvas positions.
fn format_f32(value: f32) -> String {
    format_general(value as f64, 6)
}

fn format_general(value: f64, precision: usize) -> String {
    if value.is_nan() {
        return "nan".to_owned();
    }
    if value.is_infinite() {
        return if value.is_sign_negative() {
            "-inf"
        } else {
            "inf"
        }
        .to_owned();
    }
    let negative = value.is_sign_negative();
    let magnitude = value.abs();
    if magnitude == 0.0 {
        return if negative {
            "-0".to_owned()
        } else {
            "0".to_owned()
        };
    }

    let scientific = format!("{:.*e}", precision.saturating_sub(1), magnitude);
    let (mantissa, exponent) = scientific
        .split_once('e')
        .expect("Rust exponential formatting always has an exponent");
    let exponent: i32 = exponent.parse().expect("exponent is an integer");
    let digits: String = mantissa
        .chars()
        .filter(|character| *character != '.')
        .collect();

    let mut out = String::new();
    if negative {
        out.push('-');
    }
    if exponent < -4 || exponent >= precision as i32 {
        let lead = &digits[..1];
        let rest = digits[1..].trim_end_matches('0').to_owned();
        out.push_str(lead);
        if !rest.is_empty() {
            out.push('.');
            out.push_str(&rest);
        }
        out.push('e');
        out.push(if exponent < 0 { '-' } else { '+' });
        out.push_str(&format!("{:02}", exponent.abs()));
        return out;
    }

    if exponent >= 0 {
        let integer_digits = exponent as usize + 1;
        if integer_digits >= digits.len() {
            out.push_str(&digits);
            out.push_str(&"0".repeat(integer_digits - digits.len()));
            return out;
        }
        out.push_str(&digits[..integer_digits]);
        let fraction = digits[integer_digits..].trim_end_matches('0');
        if !fraction.is_empty() {
            out.push('.');
            out.push_str(fraction);
        }
        out
    } else {
        let zeros = (-exponent - 1) as usize;
        out.push_str("0.");
        out.push_str(&"0".repeat(zeros));
        out.push_str(digits.trim_end_matches('0'));
        out
    }
}

// ── Tests ────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_graph() -> Graph {
        let mut graph = Graph::new("player.dyd");
        let on_update = graph.add_node(NodeKind::OnUpdate, 40.0, 40.0);
        let translate = graph.add_node(NodeKind::Translate, 260.0, 40.0);
        let delta = graph.add_node(NodeKind::DeltaTime, 120.0, 150.0);
        graph
            .connect(&format!("{on_update}.out"), &format!("{translate}.in"))
            .expect("exec link");
        graph
            .connect(&format!("{delta}.value"), &format!("{translate}.y"))
            .expect("data link");
        graph
    }

    #[test]
    fn catalog_covers_every_kind() {
        for kind in NodeKind::ALL {
            let entry = catalog_entry(kind.type_name())
                .unwrap_or_else(|| panic!("missing catalog entry for {kind:?}"));
            assert_eq!(entry.kind, kind);
            assert_eq!(entry.display_name, kind.display_name());
            assert_eq!(entry.category, kind.category());
            assert_eq!(entry.description, kind.description());
            assert!(NodeKind::from_type_name(kind.type_name()).is_some());
        }
        assert_eq!(catalog().len(), NodeKind::ALL.len());
    }

    #[test]
    fn emitter_matches_the_cxx_output() {
        let emitted = emit(&sample_graph()).expect("emit");
        assert_eq!(
            emitted.dyd,
            "// @node n1\nfunction onUpdate(entity, dt) {\n  // @node n2\n  caffeine_translate(entity, 0, dt, 0);\n}\n"
        );
        assert_eq!(
            emitted.node_json,
            "{\n  \"version\": 1,\n  \"script\": \"player.dyd\",\n  \"nodes\": [\n    { \"id\": \"n1\", \"type\": \"OnUpdate\", \"x\": 40, \"y\": 40 },\n    { \"id\": \"n2\", \"type\": \"Translate\", \"x\": 260, \"y\": 40, \"inputX\": 0, \"inputY\": 0, \"inputZ\": 0 },\n    { \"id\": \"n3\", \"type\": \"DeltaTime\", \"x\": 120, \"y\": 150 }\n  ],\n  \"edges\": [\n    { \"from\": \"n1.out\", \"to\": \"n2.in\" },\n    { \"from\": \"n3.value\", \"to\": \"n2.y\" }\n  ]\n}\n"
        );
    }

    #[test]
    fn emitter_is_deterministic_regardless_of_insertion_order() {
        let first = sample_graph();
        let mut second = sample_graph();
        second.nodes.reverse();
        second.edges.reverse();
        let a = emit(&first).expect("first");
        let b = emit(&first).expect("first again");
        let c = emit(&second).expect("second");
        assert_eq!(a, b);
        assert_eq!(a.dyd, c.dyd);
        assert_eq!(a.node_json, c.node_json);
    }

    #[test]
    fn emitter_requires_one_on_update() {
        let mut graph = Graph::new("empty.dyd");
        assert!(emit(&graph).is_err());
        graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        assert!(emit(&graph).is_err());
    }

    #[test]
    fn json_round_trip_is_stable() {
        let graph = sample_graph();
        let first = emit(&graph).expect("emit");
        let parsed = Graph::from_json(&first.node_json).expect("parse");
        assert_eq!(parsed, graph);
        let second = emit(&parsed).expect("re-emit");
        assert_eq!(first, second);
    }

    #[test]
    fn branch_graph_matches_the_cxx_reference_output() {
        // Reference text produced by the C++ NodeEmitter for the
        // `sampleMoveGraph` in tests/test_node_emitter.cpp.
        let mut graph = Graph::new("move.node.dyd");
        let on_update = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let branch = graph.add_node(NodeKind::Branch, 120.0, 0.0);
        let condition = graph.add_node(NodeKind::Constant, 0.0, 80.0);
        let set_position = graph.add_node(NodeKind::SetPosition, 240.0, 0.0);
        let x = graph.add_node(NodeKind::Constant, 120.0, 80.0);
        let y = graph.add_node(NodeKind::Constant, 180.0, 80.0);
        let z = graph.add_node(NodeKind::Constant, 240.0, 80.0);
        for (id, value) in [(&x, 7.0), (&y, 8.0), (&z, 9.0)] {
            graph.find_node_mut(id).expect("constant").number_value = value;
        }
        {
            let node = graph.find_node_mut(&condition).expect("condition");
            node.constant_is_bool = true;
            node.bool_value = true;
        }
        graph
            .connect(&format!("{on_update}.out"), &format!("{branch}.in"))
            .expect("exec");
        graph
            .connect(
                &format!("{condition}.value"),
                &format!("{branch}.condition"),
            )
            .expect("condition");
        graph
            .connect(&format!("{branch}.then"), &format!("{set_position}.in"))
            .expect("then");
        for (from, pin) in [(&x, "x"), (&y, "y"), (&z, "z")] {
            graph
                .connect(&format!("{from}.value"), &format!("{set_position}.{pin}"))
                .expect("axis");
        }
        let emitted = emit(&graph).expect("emit");
        assert_eq!(
            emitted.dyd,
            "// @node n1\nfunction onUpdate(entity, dt) {\n  // @node n2\n  if (true) {\n    // @node n4\n    caffeine_set_position(entity, 7, 8, 9);\n  } else {\n  }\n}\n"
        );
        assert_eq!(
            emitted.node_json,
            "{\n  \"version\": 1,\n  \"script\": \"move.node.dyd\",\n  \"nodes\": [\n    { \"id\": \"n1\", \"type\": \"OnUpdate\", \"x\": 0, \"y\": 0 },\n    { \"id\": \"n2\", \"type\": \"Branch\", \"x\": 120, \"y\": 0, \"condition\": false },\n    { \"id\": \"n3\", \"type\": \"Constant\", \"x\": 0, \"y\": 80, \"valueKind\": \"bool\", \"value\": true },\n    { \"id\": \"n4\", \"type\": \"SetPosition\", \"x\": 240, \"y\": 0, \"inputX\": 0, \"inputY\": 0, \"inputZ\": 0 },\n    { \"id\": \"n5\", \"type\": \"Constant\", \"x\": 120, \"y\": 80, \"valueKind\": \"number\", \"value\": 7 },\n    { \"id\": \"n6\", \"type\": \"Constant\", \"x\": 180, \"y\": 80, \"valueKind\": \"number\", \"value\": 8 },\n    { \"id\": \"n7\", \"type\": \"Constant\", \"x\": 240, \"y\": 80, \"valueKind\": \"number\", \"value\": 9 }\n  ],\n  \"edges\": [\n    { \"from\": \"n1.out\", \"to\": \"n2.in\" },\n    { \"from\": \"n2.then\", \"to\": \"n4.in\" },\n    { \"from\": \"n3.value\", \"to\": \"n2.condition\" },\n    { \"from\": \"n5.value\", \"to\": \"n4.x\" },\n    { \"from\": \"n6.value\", \"to\": \"n4.y\" },\n    { \"from\": \"n7.value\", \"to\": \"n4.z\" }\n  ]\n}\n"
        );
    }

    #[test]
    fn import_round_trips_markers() {
        let source = "// @node n1\nfunction onUpdate(entity, dt) {\n  // @node b1\n  if (true) {\n    // @node t1\n    caffeine_translate(entity, 0, dt, 0);\n  } else {\n  }\n}\n";
        let graph = Graph::import(source, "mover.dyd").expect("import");
        assert_eq!(
            graph.find_node("b1").map(|node| node.kind),
            Some(NodeKind::Branch)
        );
        assert_eq!(
            graph.find_node("t1").map(|node| node.kind),
            Some(NodeKind::Translate)
        );
        assert!(graph
            .nodes
            .iter()
            .any(|node| node.kind == NodeKind::DeltaTime));
        let emitted = emit(&graph).expect("re-emit");
        assert_eq!(emitted.dyd, source);
    }

    #[test]
    fn import_requires_markers() {
        let error = Graph::import("function onUpdate(entity, dt) {}\n", "hand.dyd")
            .expect_err("hand-written scripts have no markers");
        assert!(error.contains("markers"));
    }

    #[test]
    fn invalid_connections_are_rejected() {
        let mut graph = sample_graph();
        assert!(graph.connect("n2.out", "n2.in").is_err(), "self link");
        assert!(graph.connect("nope.out", "n2.in").is_err(), "unknown node");
        assert!(graph.connect("n1.nope", "n2.in").is_err(), "unknown pin");
        assert!(graph.connect("n1.out", "n2.x").is_err(), "exec into data");
        assert!(
            graph.connect("n3.value", "n2.in").is_err(),
            "data into exec"
        );
        assert!(graph.connect("n2.out", "n1.in").is_err(), "no input pin");
        // n1.out -> n2.in already exists; a cycle back into n1 is impossible
        // because OnUpdate has no input, so build a loop between two actions.
        let mut looped = Graph::new("loop.dyd");
        let root = looped.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let a = looped.add_node(NodeKind::Log, 100.0, 0.0);
        let b = looped.add_node(NodeKind::Log, 200.0, 0.0);
        looped
            .connect(&format!("{root}.out"), &format!("{a}.in"))
            .expect("root");
        looped
            .connect(&format!("{a}.out"), &format!("{b}.in"))
            .expect("a to b");
        assert!(looped
            .connect(&format!("{b}.out"), &format!("{a}.in"))
            .is_err());
    }

    #[test]
    fn connecting_replaces_the_existing_input_wire() {
        let mut graph = sample_graph();
        let other = graph.add_node(NodeKind::Constant, 0.0, 0.0);
        assert!(
            graph
                .connect(&format!("{other}.value"), "n3.value")
                .is_err(),
            "DeltaTime has no data input"
        );
        graph
            .connect(&format!("{other}.value"), "n2.y")
            .expect("replace data input");
        let into_y: Vec<&str> = graph
            .edges
            .iter()
            .filter(|edge| edge.to == "n2.y")
            .map(|edge| edge.from.as_str())
            .collect();
        let expected = format!("{other}.value");
        assert_eq!(into_y, vec![expected.as_str()]);
    }

    #[test]
    fn exec_outputs_fan_out_but_inputs_take_one() {
        let mut graph = Graph::new("fan.dyd");
        let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let a = graph.add_node(NodeKind::Log, 100.0, 0.0);
        let b = graph.add_node(NodeKind::Log, 100.0, 100.0);
        graph
            .connect(&format!("{root}.out"), &format!("{a}.in"))
            .expect("a");
        graph
            .connect(&format!("{root}.out"), &format!("{b}.in"))
            .expect("b");
        assert_eq!(
            graph
                .edges
                .iter()
                .filter(|edge| edge.from == format!("{root}.out"))
                .count(),
            2
        );
        graph.disconnect_input(&format!("{b}.in"));
        assert!(!graph.edges.iter().any(|edge| edge.to == format!("{b}.in")));
    }

    #[test]
    fn constant_bool_only_feeds_bool_inputs() {
        let mut graph = Graph::new("bool.dyd");
        let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let branch = graph.add_node(NodeKind::Branch, 100.0, 0.0);
        let flag = graph.add_node(NodeKind::Constant, 0.0, 100.0);
        graph
            .find_node_mut(&flag)
            .expect("constant")
            .constant_is_bool = true;
        graph
            .connect(&format!("{root}.out"), &format!("{branch}.in"))
            .expect("exec");
        graph
            .connect(&format!("{flag}.value"), &format!("{branch}.condition"))
            .expect("bool into condition");
        let number = graph.add_node(NodeKind::Constant, 0.0, 200.0);
        assert!(graph
            .connect(&format!("{number}.value"), &format!("{branch}.condition"))
            .is_err());
    }

    #[test]
    fn state_edits_mark_dirty_and_guard_on_update() {
        let mut state = State::new();
        state.graph = Some(Graph::new("state.dyd"));
        let root = state.add_node(NodeKind::OnUpdate);
        let other = state.add_node(NodeKind::OnUpdate);
        assert_eq!(root, other, "only one OnUpdate node is allowed");
        let translate = state.add_node(NodeKind::Translate);
        state.set_number(&translate, "y", 2.5);
        state.set_bool(&translate, "condition", true);
        assert!(state.dirty);
        assert_eq!(
            state
                .graph
                .as_ref()
                .and_then(|graph| graph.find_node(&translate))
                .map(|node| node.input_y),
            Some(2.5)
        );
        state.selected = Some(translate);
        state.delete_selected();
        assert!(state
            .graph
            .as_ref()
            .and_then(|graph| graph.find_node("n2"))
            .is_none());
    }

    #[test]
    fn state_round_trips_a_script_on_disk() {
        let root = std::env::temp_dir().join(format!("doppio-dryad-{}", std::process::id()));
        std::fs::create_dir_all(&root).expect("temp dir");
        let script = root.join("actors.dyd");
        let mut state = State::new();
        state.graph = Some(Graph::new("actors.dyd"));
        let root_node = state.add_node(NodeKind::OnUpdate);
        let translate = state.add_node(NodeKind::Translate);
        state
            .connect(&format!("{root_node}.out"), &format!("{translate}.in"))
            .expect("exec");
        state.set_number(&translate, "x", 1.0);
        state.script_path = Some(script.clone());
        state.save().expect("save");
        assert!(!state.dirty);
        let json = node_json_path_for_script(&script);
        assert!(json.exists());

        let mut reopened = State::new();
        reopened.open(&script, None).expect("open");
        let graph = reopened.graph.expect("graph");
        assert_eq!(graph.script_file_name, "actors.dyd");
        assert_eq!(graph.nodes.len(), 2);
        assert!(emit(&graph)
            .expect("emit")
            .dyd
            .contains("caffeine_translate(entity, 1, 0, 0);"));

        // Hand edits with markers win over stale JSON.
        std::fs::write(
            &script,
            "// @node n1\nfunction onUpdate(entity, dt) {\n  // @node l1\n  caffeine_log(\"hi\");\n}\n",
        )
        .expect("edit script");
        let mut synced = State::new();
        synced.open(&script, None).expect("reopen");
        let graph = synced.graph.as_ref().expect("graph");
        assert_eq!(graph.on_update().map(|node| node.id.as_str()), Some("n1"));
        assert!(synced.sync_from_dyd().is_ok());
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn format_number_matches_c_printf() {
        let cases = [
            (0.0, "0"),
            (-0.0, "-0"),
            (1.0, "1"),
            (3.0, "3"),
            (0.1, "0.10000000000000001"),
            (0.5, "0.5"),
            (-2.25, "-2.25"),
            (1e20, "1e+20"),
            (123456789.0, "123456789"),
            (1e-5, "1.0000000000000001e-05"),
            (0.0001, "0.0001"),
            (1.2345678901234567, "1.2345678901234567"),
            (1.0 / 3.0, "0.33333333333333331"),
            (42.0, "42"),
        ];
        for (value, expected) in cases {
            assert_eq!(format_number(value), expected, "value {value}");
        }
        assert_eq!(format_f32(40.0), "40");
        assert_eq!(format_f32(-12.5), "-12.5");
        assert_eq!(format_f32(1234567.0), "1.23457e+06");
        assert_eq!(format_f32(0.001), "0.001");
    }

    #[test]
    fn math_compare_and_logic_nodes_emit_and_round_trip() {
        let mut graph = Graph::new("logic.dyd");
        let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let branch = graph.add_node(NodeKind::Branch, 100.0, 0.0);
        let log = graph.add_node(NodeKind::Log, 200.0, 0.0);
        let and = graph.add_node(NodeKind::And, 0.0, 100.0);
        let key = graph.add_node(NodeKind::KeyDown, 0.0, 200.0);
        let less = graph.add_node(NodeKind::Less, 0.0, 300.0);
        let total = graph.add_node(NodeKind::TotalTime, 0.0, 400.0);
        let add = graph.add_node(NodeKind::Add, 0.0, 500.0);
        let dt = graph.add_node(NodeKind::DeltaTime, 0.0, 600.0);
        let not = graph.add_node(NodeKind::Not, 0.0, 700.0);
        let alive = graph.add_node(NodeKind::IsAlive, 0.0, 800.0);
        let me = graph.add_node(NodeKind::SelfEntity, 0.0, 900.0);
        let c = |g: &mut Graph, a: &str, b: &str| g.connect(a, b).unwrap();
        c(&mut graph, &format!("{root}.out"), &format!("{branch}.in"));
        c(&mut graph, &format!("{branch}.then"), &format!("{log}.in"));
        c(
            &mut graph,
            &format!("{and}.value"),
            &format!("{branch}.condition"),
        );
        c(&mut graph, &format!("{key}.value"), &format!("{and}.a"));
        c(&mut graph, &format!("{less}.value"), &format!("{not}.a"));
        c(&mut graph, &format!("{not}.value"), &format!("{and}.b"));
        c(&mut graph, &format!("{total}.value"), &format!("{less}.a"));
        c(&mut graph, &format!("{add}.value"), &format!("{less}.b"));
        c(&mut graph, &format!("{dt}.value"), &format!("{add}.a"));
        c(&mut graph, &format!("{me}.value"), &format!("{alive}.a"));
        // A number cannot feed a bool input, and a bool cannot feed math.
        assert!(graph
            .connect(&format!("{total}.value"), &format!("{and}.a"))
            .is_err());
        assert!(graph
            .connect(&format!("{key}.value"), &format!("{add}.b"))
            .is_err());

        let out = graph.emit().unwrap();
        assert!(
            out.dyd.contains(
                "if ((caffeine_input_key_down(\"Space\") && (!(caffeine_time_total() < (dt + 0))))) {"
            ),
            "{}",
            out.dyd
        );
        let imported = import_from_dyd(&out.dyd, "logic.dyd", None).unwrap();
        let count = |kind: NodeKind| imported.nodes.iter().filter(|n| n.kind == kind).count();
        for kind in [NodeKind::And, NodeKind::Not, NodeKind::Less, NodeKind::Add] {
            assert_eq!(count(kind), 1, "{kind:?}");
        }
        assert_eq!(imported.emit().unwrap().dyd, out.dyd);

        // A graph with a call node and `entity` also survives the trip.
        let mut calls = Graph::new("calls.dyd");
        let root = calls.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let branch = calls.add_node(NodeKind::Branch, 100.0, 0.0);
        let log = calls.add_node(NodeKind::Log, 200.0, 0.0);
        let alive = calls.add_node(NodeKind::IsAlive, 0.0, 100.0);
        let me = calls.add_node(NodeKind::SelfEntity, 0.0, 200.0);
        c(&mut calls, &format!("{root}.out"), &format!("{branch}.in"));
        c(&mut calls, &format!("{branch}.then"), &format!("{log}.in"));
        c(
            &mut calls,
            &format!("{alive}.value"),
            &format!("{branch}.condition"),
        );
        c(&mut calls, &format!("{me}.value"), &format!("{alive}.a"));
        let text = calls.emit().unwrap().dyd;
        assert!(
            text.contains("if (caffeine_world_is_alive(entity)) {"),
            "{text}"
        );
        let again = import_from_dyd(&text, "calls.dyd", None).unwrap();
        assert_eq!(again.emit().unwrap().dyd, text);
    }

    #[test]
    fn input_and_world_nodes_emit_and_round_trip() {
        let mut graph = Graph::new("input.dyd");
        let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let branch = graph.add_node(NodeKind::Branch, 100.0, 0.0);
        let key = graph.add_node(NodeKind::KeyDown, 0.0, 100.0);
        let destroy = graph.add_node(NodeKind::DestroySelf, 200.0, 0.0);
        let mv = graph.add_node(NodeKind::Translate, 200.0, 100.0);
        let axis = graph.add_node(NodeKind::Axis, 0.0, 200.0);
        let total = graph.add_node(NodeKind::TotalTime, 0.0, 300.0);
        graph.find_node_mut(&axis).unwrap().text_value = "Vertical".to_owned();
        graph
            .connect(&format!("{root}.out"), &format!("{branch}.in"))
            .unwrap();
        graph
            .connect(&format!("{key}.value"), &format!("{branch}.condition"))
            .unwrap();
        graph
            .connect(&format!("{branch}.then"), &format!("{destroy}.in"))
            .unwrap();
        graph
            .connect(&format!("{branch}.else"), &format!("{mv}.in"))
            .unwrap();
        graph
            .connect(&format!("{axis}.value"), &format!("{mv}.x"))
            .unwrap();
        graph
            .connect(&format!("{total}.value"), &format!("{mv}.y"))
            .unwrap();

        let out = graph.emit().unwrap();
        assert!(out
            .dyd
            .contains("if (caffeine_input_key_down(\"Space\")) {"));
        assert!(out.dyd.contains("caffeine_world_destroy(entity);"));
        assert!(out
            .dyd
            .contains("caffeine_translate(entity, caffeine_input_axis(\"Vertical\"), caffeine_time_total(), 0);"));

        // A bool pin cannot feed a number input.
        assert!(graph
            .connect(&format!("{key}.value"), &format!("{mv}.z"))
            .is_err());

        let imported = import_from_dyd(&out.dyd, "input.dyd", None).unwrap();
        let count = |kind: NodeKind| imported.nodes.iter().filter(|n| n.kind == kind).count();
        assert_eq!(count(NodeKind::KeyDown), 1);
        assert_eq!(count(NodeKind::DestroySelf), 1);
        assert_eq!(count(NodeKind::TotalTime), 1);
        assert!(imported
            .nodes
            .iter()
            .any(|n| n.kind == NodeKind::Axis && n.text_value == "Vertical"));
        assert_eq!(imported.emit().unwrap().dyd, out.dyd);

        let reloaded = Graph::from_json(&out.node_json).unwrap();
        assert!(reloaded
            .nodes
            .iter()
            .any(|n| n.kind == NodeKind::Axis && n.text_value == "Vertical"));
        assert_eq!(reloaded.emit().unwrap().dyd, out.dyd);
    }

    #[test]
    fn every_native_node_emits_calls_and_round_trips() {
        let natives: Vec<NodeKind> = NodeKind::ALL
            .iter()
            .copied()
            .filter(|kind| native_row(*kind).is_some())
            .collect();
        assert!(natives.len() >= 29, "the native table feeds the catalog");
        for kind in natives {
            let row = native_row(kind).unwrap();
            let mut graph = Graph::new("native.dyd");
            let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
            let node = graph.add_node(kind, 0.0, 100.0);
            let mv = graph.add_node(NodeKind::Translate, 200.0, 0.0);
            if row.boolean {
                let branch = graph.add_node(NodeKind::Branch, 100.0, 0.0);
                graph
                    .connect(&format!("{root}.out"), &format!("{branch}.in"))
                    .unwrap();
                graph
                    .connect(&format!("{node}.value"), &format!("{branch}.condition"))
                    .unwrap();
                graph
                    .connect(&format!("{branch}.then"), &format!("{mv}.in"))
                    .unwrap();
            } else {
                graph
                    .connect(&format!("{root}.out"), &format!("{mv}.in"))
                    .unwrap();
                graph
                    .connect(&format!("{node}.value"), &format!("{mv}.x"))
                    .unwrap();
            }
            let out = graph.emit().unwrap_or_else(|e| panic!("{kind:?}: {e}"));
            let zeros = vec!["0"; row.arity].join(", ");
            assert!(
                out.dyd.contains(&format!("{}({zeros})", row.native)),
                "{kind:?} emits its native: {}",
                out.dyd
            );
            let imported = import_from_dyd(&out.dyd, "native.dyd", None)
                .unwrap_or_else(|e| panic!("{kind:?} import: {e}"));
            assert_eq!(
                imported.nodes.iter().filter(|n| n.kind == kind).count(),
                1,
                "{kind:?} survives the import"
            );
            assert_eq!(imported.emit().unwrap().dyd, out.dyd, "{kind:?}");
        }
    }

    #[test]
    fn nested_native_nodes_keep_their_arguments() {
        let mut graph = Graph::new("nested.dyd");
        let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let mv = graph.add_node(NodeKind::Translate, 300.0, 0.0);
        let clamp = graph.add_node(NodeKind::Clamp, 200.0, 0.0);
        let clamp01 = graph.add_node(NodeKind::Clamp01, 100.0, 0.0);
        let sin = graph.add_node(NodeKind::Sin, 0.0, 0.0);
        graph
            .connect(&format!("{root}.out"), &format!("{mv}.in"))
            .unwrap();
        graph
            .connect(&format!("{sin}.value"), &format!("{clamp01}.a"))
            .unwrap();
        graph
            .connect(&format!("{clamp01}.value"), &format!("{clamp}.a"))
            .unwrap();
        graph
            .connect(&format!("{clamp}.value"), &format!("{mv}.y"))
            .unwrap();
        let out = graph.emit().unwrap();
        assert!(out
            .dyd
            .contains("caffeine_math_clamp(caffeine_math_clamp01(caffeine_math_sin(0)), 0, 0)"));
        let imported = import_from_dyd(&out.dyd, "nested.dyd", None).unwrap();
        assert!(imported.nodes.iter().any(|n| n.kind == NodeKind::Clamp));
        assert!(imported.nodes.iter().any(|n| n.kind == NodeKind::Clamp01));
        assert_eq!(imported.emit().unwrap().dyd, out.dyd);
    }

    #[test]
    fn catalog_lists_every_kind_once() {
        assert_eq!(catalog().len(), NodeKind::ALL.len());
        for kind in NodeKind::ALL {
            let entry = catalog_entry(kind.type_name()).expect("catalog row");
            assert_eq!(entry.kind, kind);
            assert_eq!(entry.display_name, kind.display_name());
            assert_eq!(entry.category, kind.category());
            assert_eq!(entry.description, kind.description());
        }
    }
}
