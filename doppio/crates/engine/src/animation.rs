//! Animation (C ABI v22): the PhaseShift state machine and 2D rigs, as plain Rust types.
//!
//! The engine keeps the state; these calls read it and write parameters through the C ABI. Everything here is
//! optional at load time: a library older than v22 leaves the functions `None` and the calls return `Error`.

use super::{result, Engine, EngineError, EngineResult};
use std::ffi::{c_void, CString};

/// Name fields in the C ABI are 32 bytes, NUL-terminated.
pub const NAME_LEN: usize = 32;
/// Clip names in the C ABI are 260 bytes, NUL-terminated.
pub const CLIP_LEN: usize = 260;

// ── Raw C layout (must match include/caffeine/capi/EngineAPI.h) ───────────

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawPhaseShiftCounts {
    parameters: u32,
    states: u32,
    transitions: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawPhaseShiftParameter {
    name: [u8; NAME_LEN],
    kind: u32,
    value: f32,
    smoothing: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawPhaseShiftState {
    name: [u8; NAME_LEN],
    clip: [u8; CLIP_LEN],
    speed: f32,
    is_current: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawPhaseShiftCondition {
    parameter: [u8; NAME_LEN],
    op: u32,
    value: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawPhaseShiftTransition {
    from: [u8; NAME_LEN],
    to: [u8; NAME_LEN],
    blend_time: f32,
    has_exit_time: u32,
    condition_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawRig2dCounts {
    bones: u32,
    vertices: u32,
    triangles: u32,
    parameters: u32,
    bindings: u32,
    loaded: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawRig2dBone {
    name: [u8; NAME_LEN],
    parent: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawRig2dParameter {
    name: [u8; NAME_LEN],
    minimum: f32,
    default_value: f32,
    maximum: f32,
    value: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawRig2dVertex {
    influence_count: u32,
    bones: [u32; 4],
    weights: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct RawRig2dBinding {
    parameter: u32,
    bone: u32,
    channel: u32,
    key_count: u32,
}

pub(crate) type PhaseShiftCountsFn =
    unsafe extern "C" fn(*const c_void, u32, *mut RawPhaseShiftCounts) -> i32;
pub(crate) type PhaseShiftParameterFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawPhaseShiftParameter) -> i32;
pub(crate) type PhaseShiftStateFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawPhaseShiftState) -> i32;
pub(crate) type PhaseShiftTransitionFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawPhaseShiftTransition) -> i32;
pub(crate) type PhaseShiftConditionFn =
    unsafe extern "C" fn(*const c_void, u32, u32, u32, *mut RawPhaseShiftCondition) -> i32;
pub(crate) type PhaseShiftSetFloatFn = unsafe extern "C" fn(*mut c_void, u32, u32, f32) -> i32;
pub(crate) type PhaseShiftSetSmoothingFn = unsafe extern "C" fn(*mut c_void, u32, u32, f32) -> i32;
pub(crate) type Rig2dAttachFn =
    unsafe extern "C" fn(*mut c_void, u32, *const std::ffi::c_char) -> i32;
pub(crate) type Rig2dCountsFn =
    unsafe extern "C" fn(*const c_void, u32, *mut RawRig2dCounts) -> i32;
pub(crate) type Rig2dBoneFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawRig2dBone) -> i32;
pub(crate) type Rig2dParameterFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawRig2dParameter) -> i32;
pub(crate) type Rig2dVertexFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawRig2dVertex) -> i32;
pub(crate) type Rig2dBindingFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawRig2dBinding) -> i32;
pub(crate) type Rig2dSetParameterFn = unsafe extern "C" fn(*mut c_void, u32, u32, f32) -> i32;
pub(crate) type PhaseShiftAddFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
pub(crate) type PhaseShiftAddParameterFn =
    unsafe extern "C" fn(*mut c_void, u32, *const std::ffi::c_char, u32) -> i32;
pub(crate) type PhaseShiftRemoveParameterFn = unsafe extern "C" fn(*mut c_void, u32, u32) -> i32;

// ── Owned types ──────────────────────────────────────────────────────────

/// Value kind of a PhaseShift parameter.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PhaseShiftKind {
    Bool,
    Float,
    Int,
    Trigger,
}

impl PhaseShiftKind {
    /// The ABI value of the kind (the engine's ParameterType order).
    fn to_raw(self) -> u32 {
        match self {
            Self::Bool => 0,
            Self::Float => 1,
            Self::Int => 2,
            Self::Trigger => 3,
        }
    }

    fn from_raw(value: u32) -> Option<Self> {
        Some(match value {
            0 => Self::Bool,
            1 => Self::Float,
            2 => Self::Int,
            3 => Self::Trigger,
            _ => return None,
        })
    }
}

/// Comparison used by a transition condition.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConditionOp {
    Equals,
    NotEquals,
    Greater,
    Less,
    GreaterOrEqual,
    LessOrEqual,
}

impl ConditionOp {
    /// The symbol shown in the UI.
    pub fn symbol(self) -> &'static str {
        match self {
            Self::Equals => "==",
            Self::NotEquals => "!=",
            Self::Greater => ">",
            Self::Less => "<",
            Self::GreaterOrEqual => ">=",
            Self::LessOrEqual => "<=",
        }
    }

    fn from_raw(value: u32) -> Option<Self> {
        Some(match value {
            0 => Self::Equals,
            1 => Self::NotEquals,
            2 => Self::Greater,
            3 => Self::Less,
            4 => Self::GreaterOrEqual,
            5 => Self::LessOrEqual,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct PhaseShiftParameter {
    pub name: String,
    pub kind: PhaseShiftKind,
    /// Float and int values as f32, bool as 0 or 1.
    pub value: f32,
    /// Seconds for a float to reach its target. Zero means not smoothed.
    pub smoothing: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PhaseShiftState {
    pub name: String,
    /// Motion name, or the clip name when the state has no motion. Empty when the state has no clip.
    pub clip: String,
    pub speed: f32,
    pub current: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PhaseShiftCondition {
    pub parameter: String,
    pub op: ConditionOp,
    pub value: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PhaseShiftTransition {
    /// "Any State" for any-state transitions.
    pub from: String,
    pub to: String,
    pub blend_time: f32,
    pub has_exit_time: bool,
    pub conditions: Vec<PhaseShiftCondition>,
}

/// Everything the PhaseShift panel shows, read in one go.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct PhaseShiftSnapshot {
    pub parameters: Vec<PhaseShiftParameter>,
    pub states: Vec<PhaseShiftState>,
    pub transitions: Vec<PhaseShiftTransition>,
}

/// Bone channel a rig binding drives.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RigChannel {
    Rotation,
    TranslateX,
    TranslateY,
    ScaleX,
    ScaleY,
}

impl RigChannel {
    fn from_raw(value: u32) -> Option<Self> {
        Some(match value {
            0 => Self::Rotation,
            1 => Self::TranslateX,
            2 => Self::TranslateY,
            3 => Self::ScaleX,
            4 => Self::ScaleY,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Rig2dBone {
    pub name: String,
    /// Index of the parent bone, or `None` for a root.
    pub parent: Option<usize>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Rig2dParameter {
    pub name: String,
    pub minimum: f32,
    pub default_value: f32,
    pub maximum: f32,
    /// The value the last tick evaluated.
    pub value: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Rig2dVertex {
    /// Bone index and weight of each influence (at most four).
    pub influences: Vec<(usize, f32)>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Rig2dBinding {
    pub parameter: usize,
    pub bone: usize,
    pub channel: RigChannel,
    pub key_count: usize,
}

/// A 2D rig attached to an entity, as the Rig 2D panel reads it.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Rig2dSnapshot {
    pub bones: Vec<Rig2dBone>,
    pub parameters: Vec<Rig2dParameter>,
    pub vertices: Vec<Rig2dVertex>,
    pub triangles: usize,
    pub bindings: Vec<Rig2dBinding>,
}

// ── Conversions ───────────────────────────────────────────────────────────

/// Reads a NUL-terminated C name. Bytes after the first NUL are ignored.
fn c_name(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..end]).into_owned()
}

fn zeroed<T>() -> T {
    // SAFETY: every raw struct here is plain old data (integers and float arrays), so all-zero is valid.
    unsafe { std::mem::zeroed() }
}

fn to_c_path(path: &str) -> EngineResult<CString> {
    CString::new(path).map_err(|_| EngineError::InvalidArgument)
}

fn invalid_op() -> EngineError {
    EngineError::Error
}

// ── Engine calls ──────────────────────────────────────────────────────────

impl Engine {
    /// Reads the PhaseShift of an entity: parameters, states and base-layer transitions.
    pub fn phase_shift(&self, entity: u32) -> EngineResult<PhaseShiftSnapshot> {
        let counts_fn = self
            .functions
            .phase_shift_counts
            .ok_or(EngineError::Error)?;
        let mut counts: RawPhaseShiftCounts = zeroed();
        result(unsafe { counts_fn(self.session, entity, &mut counts) })?;

        let parameter_fn = self
            .functions
            .phase_shift_parameter
            .ok_or(EngineError::Error)?;
        let mut parameters = Vec::with_capacity(counts.parameters as usize);
        for index in 0..counts.parameters {
            let mut raw: RawPhaseShiftParameter = zeroed();
            result(unsafe { parameter_fn(self.session, entity, index, &mut raw) })?;
            parameters.push(PhaseShiftParameter {
                name: c_name(&raw.name),
                kind: PhaseShiftKind::from_raw(raw.kind).ok_or_else(invalid_op)?,
                value: raw.value,
                smoothing: raw.smoothing,
            });
        }

        let state_fn = self.functions.phase_shift_state.ok_or(EngineError::Error)?;
        let mut states = Vec::with_capacity(counts.states as usize);
        for index in 0..counts.states {
            let mut raw: RawPhaseShiftState = zeroed();
            result(unsafe { state_fn(self.session, entity, index, &mut raw) })?;
            states.push(PhaseShiftState {
                name: c_name(&raw.name),
                clip: c_name(&raw.clip),
                speed: raw.speed,
                current: raw.is_current != 0,
            });
        }

        let transition_fn = self
            .functions
            .phase_shift_transition
            .ok_or(EngineError::Error)?;
        let condition_fn = self
            .functions
            .phase_shift_condition
            .ok_or(EngineError::Error)?;
        let mut transitions = Vec::with_capacity(counts.transitions as usize);
        for index in 0..counts.transitions {
            let mut raw: RawPhaseShiftTransition = zeroed();
            result(unsafe { transition_fn(self.session, entity, index, &mut raw) })?;
            let mut conditions = Vec::with_capacity(raw.condition_count as usize);
            for c in 0..raw.condition_count {
                let mut cond: RawPhaseShiftCondition = zeroed();
                result(unsafe { condition_fn(self.session, entity, index, c, &mut cond) })?;
                conditions.push(PhaseShiftCondition {
                    parameter: c_name(&cond.parameter),
                    op: ConditionOp::from_raw(cond.op).ok_or_else(invalid_op)?,
                    value: cond.value,
                });
            }
            transitions.push(PhaseShiftTransition {
                from: c_name(&raw.from),
                to: c_name(&raw.to),
                blend_time: raw.blend_time,
                has_exit_time: raw.has_exit_time != 0,
                conditions,
            });
        }

        Ok(PhaseShiftSnapshot {
            parameters,
            states,
            transitions,
        })
    }

    /// Writes a float parameter of the PhaseShift.
    pub fn set_phase_shift_float(
        &mut self,
        entity: u32,
        index: u32,
        value: f32,
    ) -> EngineResult<()> {
        let set = self
            .functions
            .phase_shift_set_float
            .ok_or(EngineError::Error)?;
        result(unsafe { set(self.session, entity, index, value) })
    }

    /// Smooths a float parameter over `seconds`. Zero turns smoothing off.
    pub fn set_phase_shift_smoothing(
        &mut self,
        entity: u32,
        index: u32,
        seconds: f32,
    ) -> EngineResult<()> {
        let set = self
            .functions
            .phase_shift_set_smoothing
            .ok_or(EngineError::Error)?;
        result(unsafe { set(self.session, entity, index, seconds) })
    }

    /// Attaches a `.rig2d` file to an entity. The rig is read on the next access.
    pub fn attach_rig2d(&mut self, entity: u32, path: &str) -> EngineResult<()> {
        let attach = self.functions.rig2d_attach.ok_or(EngineError::Error)?;
        let path = to_c_path(path)?;
        result(unsafe { attach(self.session, entity, path.as_ptr()) })
    }

    /// The 2D rig of an entity, or `None` when no rig is attached or the file did not load.
    pub fn rig2d(&self, entity: u32) -> EngineResult<Option<Rig2dSnapshot>> {
        let counts_fn = self.functions.rig2d_counts.ok_or(EngineError::Error)?;
        let mut counts: RawRig2dCounts = zeroed();
        result(unsafe { counts_fn(self.session, entity, &mut counts) })?;
        if counts.loaded == 0 {
            return Ok(None);
        }

        let bone_fn = self.functions.rig2d_bone.ok_or(EngineError::Error)?;
        let mut bones = Vec::with_capacity(counts.bones as usize);
        for index in 0..counts.bones {
            let mut raw: RawRig2dBone = zeroed();
            result(unsafe { bone_fn(self.session, entity, index, &mut raw) })?;
            bones.push(Rig2dBone {
                name: c_name(&raw.name),
                parent: usize::try_from(raw.parent).ok(),
            });
        }

        let parameter_fn = self.functions.rig2d_parameter.ok_or(EngineError::Error)?;
        let mut parameters = Vec::with_capacity(counts.parameters as usize);
        for index in 0..counts.parameters {
            let mut raw: RawRig2dParameter = zeroed();
            result(unsafe { parameter_fn(self.session, entity, index, &mut raw) })?;
            parameters.push(Rig2dParameter {
                name: c_name(&raw.name),
                minimum: raw.minimum,
                default_value: raw.default_value,
                maximum: raw.maximum,
                value: raw.value,
            });
        }

        let vertex_fn = self.functions.rig2d_vertex.ok_or(EngineError::Error)?;
        let mut vertices = Vec::with_capacity(counts.vertices as usize);
        for index in 0..counts.vertices {
            let mut raw: RawRig2dVertex = zeroed();
            result(unsafe { vertex_fn(self.session, entity, index, &mut raw) })?;
            let n = (raw.influence_count as usize).min(4);
            let influences = (0..n)
                .map(|i| (raw.bones[i] as usize, raw.weights[i]))
                .collect();
            vertices.push(Rig2dVertex { influences });
        }

        let binding_fn = self.functions.rig2d_binding.ok_or(EngineError::Error)?;
        let mut bindings = Vec::with_capacity(counts.bindings as usize);
        for index in 0..counts.bindings {
            let mut raw: RawRig2dBinding = zeroed();
            result(unsafe { binding_fn(self.session, entity, index, &mut raw) })?;
            bindings.push(Rig2dBinding {
                parameter: raw.parameter as usize,
                bone: raw.bone as usize,
                channel: RigChannel::from_raw(raw.channel).ok_or_else(invalid_op)?,
                key_count: raw.key_count as usize,
            });
        }

        Ok(Some(Rig2dSnapshot {
            bones,
            parameters,
            vertices,
            triangles: counts.triangles as usize,
            bindings,
        }))
    }

    /// Gives the entity a PhaseShift (no-op when it has one).
    pub fn add_phase_shift(&mut self, entity: u32) -> EngineResult<()> {
        let add = self.functions.phase_shift_add.ok_or(EngineError::Error)?;
        result(unsafe { add(self.session, entity) })
    }

    /// Adds a parameter to the entity's PhaseShift. The name is at most 31 bytes and unused; the engine refuses
    /// anything else with `InvalidArgument`, and an entity without a PhaseShift with `NotFound`.
    pub fn add_phase_shift_parameter(
        &mut self,
        entity: u32,
        name: &str,
        kind: PhaseShiftKind,
    ) -> EngineResult<()> {
        let add = self
            .functions
            .phase_shift_add_parameter
            .ok_or(EngineError::Error)?;
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { add(self.session, entity, name.as_ptr(), kind.to_raw()) })
    }

    /// Removes a parameter of the entity's PhaseShift by index. Later indices shift down.
    pub fn remove_phase_shift_parameter(&mut self, entity: u32, index: u32) -> EngineResult<()> {
        let remove = self
            .functions
            .phase_shift_remove_parameter
            .ok_or(EngineError::Error)?;
        result(unsafe { remove(self.session, entity, index) })
    }

    /// Sets a rig parameter. The engine writes the PhaseShift float with the same name, clamped to the rig range.
    pub fn set_rig2d_parameter(&mut self, entity: u32, index: u32, value: f32) -> EngineResult<()> {
        let set = self
            .functions
            .rig2d_set_parameter
            .ok_or(EngineError::Error)?;
        result(unsafe { set(self.session, entity, index, value) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn c_names_stop_at_the_first_nul() {
        let mut bytes = [0u8; NAME_LEN];
        bytes[..5].copy_from_slice(b"speed");
        bytes[6] = b'x';
        assert_eq!(c_name(&bytes), "speed");
        assert_eq!(
            c_name(&[b'a'; 4]),
            "aaaa",
            "a name that fills the buffer has no terminator"
        );
    }

    #[test]
    fn enums_decode_the_abi_values_and_reject_unknown_ones() {
        assert_eq!(PhaseShiftKind::from_raw(1), Some(PhaseShiftKind::Float));
        assert_eq!(PhaseShiftKind::from_raw(9), None);
        assert_eq!(ConditionOp::from_raw(5), Some(ConditionOp::LessOrEqual));
        assert_eq!(ConditionOp::from_raw(6), None);
        assert_eq!(ConditionOp::GreaterOrEqual.symbol(), ">=");
        assert_eq!(RigChannel::from_raw(4), Some(RigChannel::ScaleY));
        assert_eq!(RigChannel::from_raw(5), None);
    }

    #[test]
    fn kinds_map_to_the_engine_order() {
        assert_eq!(PhaseShiftKind::Bool.to_raw(), 0);
        assert_eq!(PhaseShiftKind::Trigger.to_raw(), 3);
        assert_eq!(
            PhaseShiftKind::from_raw(PhaseShiftKind::Int.to_raw()),
            Some(PhaseShiftKind::Int)
        );
    }

    #[test]
    fn paths_with_a_nul_are_refused_before_the_call() {
        assert!(to_c_path("assets/hero.rig2d").is_ok());
        assert_eq!(
            to_c_path("bad\0path").unwrap_err(),
            EngineError::InvalidArgument
        );
    }
}
