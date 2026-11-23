//! PhaseShift panel: the state machine of an entity's animator (the engine's former `Animator`).
//!
//! A pure view over [`PhaseShiftModel`]. The model is plain Rust data; the app fills it from the engine and
//! applies every edit through [`PhaseShiftModel::apply`], so the view and the tests never touch the engine.
//! Parameters are the interface: animation sources write named values, transitions read them, and a float
//! can be smoothed with a critically damped spring (`smoothing` seconds, 0 = raw value).
//!
//! The panel shows three lists: the parameters (with a live value and a smoothing slider), the states (the
//! current one highlighted) and the transitions (from, to, blend time and the conditions that fire them).
//! [`PhaseShiftModel::warnings`] lists what the engine would ignore: transitions to unknown states and
//! conditions on unknown parameters.

use crate::uikit::components::{button, list_row, panel, slider, status};
use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{column, container, row, text};
use iced::{Element, Length};

/// Longest smoothing the slider offers, in seconds.
pub const SMOOTHING_MAX: f32 = 2.0;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParamKind {
    Bool,
    Float,
    Int,
    Trigger,
}

impl ParamKind {
    pub fn label(self) -> &'static str {
        match self {
            ParamKind::Bool => "bool",
            ParamKind::Float => "float",
            ParamKind::Int => "int",
            ParamKind::Trigger => "trigger",
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ParameterRow {
    pub name: String,
    pub kind: ParamKind,
    /// Float and int values as f32, bool as 0 or 1.
    pub value: f32,
    pub min: f32,
    pub max: f32,
    /// Seconds for a float to reach its target. Zero means no smoothing.
    pub smoothing: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct StateRow {
    pub name: String,
    pub clip: Option<String>,
    pub speed: f32,
}

/// One condition of a transition, e.g. `speed > 0.5`.
#[derive(Debug, Clone, PartialEq)]
pub struct ConditionRow {
    pub parameter: String,
    pub op: String,
    pub value: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TransitionRow {
    pub from: String,
    pub to: String,
    pub blend_time: f32,
    pub has_exit_time: bool,
    pub conditions: Vec<ConditionRow>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PhaseShiftModel {
    /// Name of the entity this graph belongs to, for the title.
    pub owner: String,
    pub current_state: Option<String>,
    pub states: Vec<StateRow>,
    pub parameters: Vec<ParameterRow>,
    pub transitions: Vec<TransitionRow>,
}

/// Messages the panel emits. The app turns them into engine calls and undo steps.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    SelectState(usize),
    SetFloat {
        index: usize,
        value: f32,
    },
    SetSmoothing {
        index: usize,
        seconds: f32,
    },
    /// Gives the selected entity a PhaseShift.
    AddPhaseShift,
    /// Adds a parameter of this kind; the app picks a free name.
    AddParameter(ParamKind),
    RemoveParameter(usize),
}

impl PhaseShiftModel {
    /// Applies a value edit to the model. Returns true when the model changed.
    /// Selection is not stored here: the app keeps it.
    pub fn apply(&mut self, edit: &Edit) -> bool {
        match *edit {
            // Selection and structure are the app's: it talks to the engine and refreshes the model.
            Edit::SelectState(_)
            | Edit::AddPhaseShift
            | Edit::AddParameter(_)
            | Edit::RemoveParameter(_) => false,
            Edit::SetFloat { index, value } => match self.parameters.get_mut(index) {
                Some(p) if p.kind == ParamKind::Float => {
                    let clamped = value.clamp(p.min, p.max);
                    let changed = p.value != clamped;
                    p.value = clamped;
                    changed
                }
                _ => false,
            },
            Edit::SetSmoothing { index, seconds } => match self.parameters.get_mut(index) {
                Some(p) if p.kind == ParamKind::Float => {
                    let clamped = seconds.clamp(0.0, SMOOTHING_MAX);
                    let changed = p.smoothing != clamped;
                    p.smoothing = clamped;
                    changed
                }
                _ => false,
            },
        }
    }

    /// Problems the engine would silently ignore at runtime.
    pub fn warnings(&self) -> Vec<String> {
        let mut out = Vec::new();
        let has_state = |name: &str| name == "Exit" || self.states.iter().any(|s| s.name == name);
        for t in &self.transitions {
            if !has_state(&t.from) {
                out.push(format!("transition from unknown state '{}'", t.from));
            }
            if !has_state(&t.to) {
                out.push(format!("transition to unknown state '{}'", t.to));
            }
            for c in &t.conditions {
                if !self.parameters.iter().any(|p| p.name == c.parameter) {
                    out.push(format!(
                        "condition on unknown parameter '{}' ({} -> {})",
                        c.parameter, t.from, t.to
                    ));
                }
            }
        }
        out
    }

    /// Number of float parameters that run through a smoothing spring.
    pub fn smoothed_count(&self) -> usize {
        self.parameters
            .iter()
            .filter(|p| p.kind == ParamKind::Float && p.smoothing > 0.0)
            .count()
    }
}

// ── Ids ───────────────────────────────────────────────────────────────────

pub fn add_phase_shift_button() -> UiId {
    UiId::fixed("phaseshift.add")
}

pub fn add_parameter_button(kind: ParamKind) -> UiId {
    UiId::dynamic(format!("phaseshift.parameters.add.{}", kind.label()))
}

pub fn remove_parameter_button(index: usize) -> UiId {
    UiId::dynamic(format!("phaseshift.parameters.remove.{index}"))
}

/// A parameter name that no parameter of the model uses: `<kind>_1`, `<kind>_2`, ...
pub fn free_parameter_name(model: Option<&PhaseShiftModel>, kind: ParamKind) -> String {
    let taken = |name: &str| model.is_some_and(|m| m.parameters.iter().any(|p| p.name == name));
    (1..)
        .map(|n| format!("{}_{n}", kind.label()))
        .find(|name| !taken(name))
        .unwrap_or_default()
}

pub fn parameter_row(index: usize) -> UiId {
    UiId::dynamic(format!("phaseshift.parameters.row.{index}"))
}

pub fn state_row(index: usize) -> UiId {
    UiId::dynamic(format!("phaseshift.states.row.{index}"))
}

pub fn smoothing_slider(index: usize) -> UiId {
    UiId::dynamic(format!("phaseshift.parameters.smoothing.{index}"))
}

// ── View ──────────────────────────────────────────────────────────────────

/// What the pane shows for a selected entity without a PhaseShift: a note and the button that gives it one.
pub fn empty_view<'a>() -> Element<'a, Edit> {
    column![
        text("This entity has no PhaseShift yet.")
            .size(11)
            .color(theme::TEXT_MUTED),
        button::compact(
            add_phase_shift_button(),
            "Add PhaseShift",
            Edit::AddPhaseShift
        ),
    ]
    .spacing(6)
    .into()
}

pub fn view<'a>(model: &'a PhaseShiftModel, selected: Option<usize>) -> Element<'a, Edit> {
    let subtitle = if model.owner.is_empty() {
        None
    } else {
        Some(model.owner.clone())
    };
    let header = panel::title("PhaseShift", subtitle);

    let mut body = column![
        heading("Parameters"),
        parameters(model),
        heading("States"),
        states(model, selected),
        heading("Transitions"),
        transitions(model),
    ]
    .spacing(6);

    let warnings = model.warnings();
    if !warnings.is_empty() {
        body = body.push(heading("Warnings"));
        for w in warnings {
            body = body.push(status::chip(w, theme::ChipTone::Warning));
        }
    }

    column![header, panel::body(body.into())].into()
}

fn heading<'a>(label: &'a str) -> Element<'a, Edit> {
    text(label).size(11).color(theme::TEXT_MUTED).into()
}

fn parameters(model: &PhaseShiftModel) -> Element<'_, Edit> {
    if model.parameters.is_empty() {
        return text("No parameters. Add one in the graph to drive transitions.")
            .size(11)
            .color(theme::TEXT_MUTED)
            .into();
    }
    let mut list = column![].spacing(4);
    for (index, p) in model.parameters.iter().enumerate() {
        let head = row![
            text(p.name.as_str()).size(12).color(theme::TEXT),
            text(p.kind.label()).size(10).color(theme::TEXT_MUTED),
        ]
        .spacing(8);
        let mut entry = column![head].spacing(2);
        if p.kind == ParamKind::Float {
            entry = entry.push(slider::property(
                parameter_row(index),
                "value",
                p.min..=p.max,
                p.value,
                move |value| Edit::SetFloat { index, value },
            ));
            entry = entry.push(slider::property(
                smoothing_slider(index),
                "smoothing",
                0.0..=SMOOTHING_MAX,
                p.smoothing,
                move |seconds| Edit::SetSmoothing { index, seconds },
            ));
        }
        entry = entry.push(button::compact(
            remove_parameter_button(index),
            "Remove",
            Edit::RemoveParameter(index),
        ));
        list = list.push(
            container(entry)
                .id(parameter_row(index))
                .width(Length::Fill),
        );
    }
    list = list.push(add_row());
    list.into()
}

fn add_row<'a>() -> Element<'a, Edit> {
    row![
        button::compact(
            add_parameter_button(ParamKind::Float),
            "+ float",
            Edit::AddParameter(ParamKind::Float)
        ),
        button::compact(
            add_parameter_button(ParamKind::Bool),
            "+ bool",
            Edit::AddParameter(ParamKind::Bool)
        ),
        button::compact(
            add_parameter_button(ParamKind::Int),
            "+ int",
            Edit::AddParameter(ParamKind::Int)
        ),
        button::compact(
            add_parameter_button(ParamKind::Trigger),
            "+ trigger",
            Edit::AddParameter(ParamKind::Trigger)
        ),
    ]
    .spacing(4)
    .into()
}

fn states(model: &PhaseShiftModel, selected: Option<usize>) -> Element<'_, Edit> {
    if model.states.is_empty() {
        return text("No states yet.")
            .size(11)
            .color(theme::TEXT_MUTED)
            .into();
    }
    let mut list = column![].spacing(2);
    for (index, s) in model.states.iter().enumerate() {
        let current = model.current_state.as_deref() == Some(s.name.as_str());
        let label = match (&s.clip, current) {
            (Some(clip), true) => format!("{}  ▶  {}  (current)", s.name, clip),
            (Some(clip), false) => format!("{}  ·  {}", s.name, clip),
            (None, true) => format!("{}  ▶  (current)", s.name),
            (None, false) => s.name.clone(),
        };
        list = list.push(list_row::view(
            state_row(index),
            label,
            selected == Some(index),
            Edit::SelectState(index),
            Edit::SelectState(index),
            Edit::SelectState(index),
        ));
    }
    list.into()
}

fn transitions(model: &PhaseShiftModel) -> Element<'_, Edit> {
    if model.transitions.is_empty() {
        return text("No transitions.")
            .size(11)
            .color(theme::TEXT_MUTED)
            .into();
    }
    let mut list = column![].spacing(4);
    for t in &model.transitions {
        let mut parts: Vec<String> = t
            .conditions
            .iter()
            .map(|c| format!("{} {} {}", c.parameter, c.op, c.value))
            .collect();
        if t.has_exit_time {
            parts.push("at exit time".to_string());
        }
        let when = if parts.is_empty() {
            "immediately".to_string()
        } else {
            parts.join(", ")
        };
        let line = format!(
            "{} → {}  ·  blend {:.2}s  ·  {}",
            t.from, t.to, t.blend_time, when
        );
        list = list.push(text(line).size(11).color(theme::TEXT));
    }
    list.into()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn float(name: &str, value: f32, smoothing: f32) -> ParameterRow {
        ParameterRow {
            name: name.into(),
            kind: ParamKind::Float,
            value,
            min: -1.0,
            max: 1.0,
            smoothing,
        }
    }

    fn sample() -> PhaseShiftModel {
        PhaseShiftModel {
            owner: "Hero".into(),
            current_state: Some("idle".into()),
            states: vec![
                StateRow {
                    name: "idle".into(),
                    clip: Some("idle.anim".into()),
                    speed: 1.0,
                },
                StateRow {
                    name: "run".into(),
                    clip: None,
                    speed: 1.0,
                },
            ],
            parameters: vec![
                float("speed", 0.0, 0.0),
                ParameterRow {
                    name: "grounded".into(),
                    kind: ParamKind::Bool,
                    value: 1.0,
                    min: 0.0,
                    max: 1.0,
                    smoothing: 0.0,
                },
            ],
            transitions: vec![TransitionRow {
                from: "idle".into(),
                to: "run".into(),
                blend_time: 0.2,
                has_exit_time: false,
                conditions: vec![ConditionRow {
                    parameter: "speed".into(),
                    op: ">".into(),
                    value: 0.5,
                }],
            }],
        }
    }

    #[test]
    fn set_float_clamps_to_the_parameter_range() {
        let mut m = sample();
        assert!(m.apply(&Edit::SetFloat {
            index: 0,
            value: 4.0
        }));
        assert_eq!(m.parameters[0].value, 1.0);
        assert!(
            !m.apply(&Edit::SetFloat {
                index: 0,
                value: 4.0
            }),
            "same value is not a change"
        );
    }

    #[test]
    fn smoothing_is_only_for_floats_and_is_capped() {
        let mut m = sample();
        assert!(m.apply(&Edit::SetSmoothing {
            index: 0,
            seconds: 10.0
        }));
        assert_eq!(m.parameters[0].smoothing, SMOOTHING_MAX);
        assert!(
            !m.apply(&Edit::SetSmoothing {
                index: 1,
                seconds: 0.5
            }),
            "bool cannot be smoothed"
        );
        assert!(
            !m.apply(&Edit::SetFloat {
                index: 1,
                value: 1.0
            }),
            "bool cannot take a float"
        );
        assert_eq!(m.smoothed_count(), 1);
    }

    #[test]
    fn select_state_is_not_a_model_change() {
        let mut m = sample();
        assert!(!m.apply(&Edit::SelectState(1)));
        assert_eq!(m.current_state.as_deref(), Some("idle"));
    }

    #[test]
    fn a_clean_graph_has_no_warnings() {
        assert!(sample().warnings().is_empty());
    }

    #[test]
    fn warnings_name_unknown_states_and_parameters() {
        let mut m = sample();
        m.transitions.push(TransitionRow {
            from: "run".into(),
            to: "jump".into(),
            blend_time: 0.0,
            has_exit_time: true,
            conditions: vec![ConditionRow {
                parameter: "jump_pressed".into(),
                op: "==".into(),
                value: 1.0,
            }],
        });
        let w = m.warnings();
        assert!(w.iter().any(|s| s.contains("unknown state 'jump'")));
        assert!(w
            .iter()
            .any(|s| s.contains("unknown parameter 'jump_pressed'")));
    }

    #[test]
    fn exit_is_a_known_target() {
        let mut m = sample();
        m.transitions[0].to = "Exit".into();
        assert!(m.warnings().is_empty());
    }

    #[test]
    fn view_builds_for_a_populated_and_an_empty_model() {
        let m = sample();
        let _ = view(&m, Some(1));
        let _ = view(&PhaseShiftModel::default(), None);
    }

    #[test]
    fn free_names_skip_the_taken_ones() {
        let m = sample();
        assert_eq!(free_parameter_name(Some(&m), ParamKind::Float), "float_1");
        let mut taken = sample();
        taken.parameters.push(float("float_1", 0.0, 0.0));
        assert_eq!(
            free_parameter_name(Some(&taken), ParamKind::Float),
            "float_2"
        );
        assert_eq!(free_parameter_name(None, ParamKind::Int), "int_1");
    }

    #[test]
    fn structural_edits_are_not_model_changes() {
        let mut m = sample();
        assert!(!m.apply(&Edit::AddPhaseShift));
        assert!(!m.apply(&Edit::AddParameter(ParamKind::Float)));
        assert!(!m.apply(&Edit::RemoveParameter(0)));
        assert_eq!(m.parameters.len(), 2);
    }

    #[test]
    fn row_ids_are_stable_and_hierarchical() {
        assert_eq!(parameter_row(3).key(), "phaseshift.parameters.row.3");
        assert_eq!(state_row(0).key(), "phaseshift.states.row.0");
        assert_eq!(
            smoothing_slider(2).key(),
            "phaseshift.parameters.smoothing.2"
        );
    }
}
