//! Editor state and message handling of the **PhaseShift** and **Rig 2D** panes.
//!
//! Both panes show the selected entity. Each refresh reads the engine (`phase_shift`, `rig2d`) into plain snapshots,
//! turns them into the panel models with [`phase_shift_model`] and [`rig_model`] (pure functions, tested without an
//! engine), and every edit goes back through the engine's animation calls with one undo step per gesture.

use super::{App, LogLevel, Message, PaneKind};
use crate::uikit::components::status;
use crate::uikit::panels::phaseshift::{
    self as phaseshift_panel, ConditionRow, Edit as PhaseShiftEdit, ParamKind, ParameterRow,
    PhaseShiftModel, StateRow, TransitionRow,
};
use crate::uikit::panels::rig2d::{
    self as rig2d_panel, BindingRow, BoneRow, Edit as RigEdit, Rig2dModel, RigParameterRow,
    VertexRow,
};
use doppio_engine::{ConditionOp, PhaseShiftKind, PhaseShiftSnapshot, Rig2dSnapshot, RigChannel};
use iced::Element;

/// The engine keeps no range for PhaseShift floats, so the panel uses this one.
pub const PHASE_SHIFT_FLOAT_RANGE: (f32, f32) = (-1.0, 1.0);

#[derive(Default)]
pub(super) struct AnimationPanes {
    pub phase_shift: Option<PhaseShiftModel>,
    /// The entity the PhaseShift model was read from.
    pub phase_shift_entity: Option<u32>,
    pub phase_shift_state: Option<usize>,
    pub rig: Option<Rig2dModel>,
    /// The entity the rig model was read from.
    pub rig_entity: Option<u32>,
    pub rig_bone: Option<usize>,
    /// The attach field of the Rig 2D pane (the path typed, not yet attached).
    pub rig_draft: String,
    pub status: Option<String>,
}

/// The engine's kind of a parameter the panel adds.
fn engine_kind(kind: ParamKind) -> PhaseShiftKind {
    match kind {
        ParamKind::Bool => PhaseShiftKind::Bool,
        ParamKind::Float => PhaseShiftKind::Float,
        ParamKind::Int => PhaseShiftKind::Int,
        ParamKind::Trigger => PhaseShiftKind::Trigger,
    }
}

fn kind_of(kind: PhaseShiftKind) -> ParamKind {
    match kind {
        PhaseShiftKind::Bool => ParamKind::Bool,
        PhaseShiftKind::Float => ParamKind::Float,
        PhaseShiftKind::Int => ParamKind::Int,
        PhaseShiftKind::Trigger => ParamKind::Trigger,
    }
}

fn op_of(op: ConditionOp) -> String {
    op.symbol().to_owned()
}

fn channel_label(channel: RigChannel) -> &'static str {
    match channel {
        RigChannel::Rotation => "rotation",
        RigChannel::TranslateX => "translate x",
        RigChannel::TranslateY => "translate y",
        RigChannel::ScaleX => "scale x",
        RigChannel::ScaleY => "scale y",
    }
}

/// The PhaseShift panel's model for one entity's snapshot. `owner` is the entity name, shown as the subtitle.
pub fn phase_shift_model(snapshot: &PhaseShiftSnapshot, owner: &str) -> PhaseShiftModel {
    let (min, max) = PHASE_SHIFT_FLOAT_RANGE;
    PhaseShiftModel {
        owner: owner.to_owned(),
        current_state: snapshot
            .states
            .iter()
            .find(|s| s.current)
            .map(|s| s.name.clone()),
        states: snapshot
            .states
            .iter()
            .map(|s| StateRow {
                name: s.name.clone(),
                clip: (!s.clip.is_empty()).then(|| s.clip.clone()),
                speed: s.speed,
            })
            .collect(),
        parameters: snapshot
            .parameters
            .iter()
            .map(|p| ParameterRow {
                name: p.name.clone(),
                kind: kind_of(p.kind),
                value: p.value,
                min,
                max,
                smoothing: p.smoothing,
            })
            .collect(),
        transitions: snapshot
            .transitions
            .iter()
            .map(|t| TransitionRow {
                from: t.from.clone(),
                to: t.to.clone(),
                blend_time: t.blend_time,
                has_exit_time: t.has_exit_time,
                conditions: t
                    .conditions
                    .iter()
                    .map(|c| ConditionRow {
                        parameter: c.parameter.clone(),
                        op: op_of(c.op),
                        value: c.value,
                    })
                    .collect(),
            })
            .collect(),
    }
}

/// The Rig 2D panel's model for one entity's rig snapshot.
pub fn rig_model(snapshot: &Rig2dSnapshot, owner: &str) -> Rig2dModel {
    Rig2dModel {
        owner: owner.to_owned(),
        bones: snapshot
            .bones
            .iter()
            .map(|b| BoneRow {
                name: b.name.clone(),
                parent: b.parent,
            })
            .collect(),
        parameters: snapshot
            .parameters
            .iter()
            .map(|p| RigParameterRow {
                name: p.name.clone(),
                min: p.minimum,
                default: p.default_value,
                max: p.maximum,
                value: p.value,
            })
            .collect(),
        vertices: snapshot
            .vertices
            .iter()
            .map(|v| VertexRow {
                influences: v.influences.clone(),
            })
            .collect(),
        triangles: snapshot.triangles,
        bindings: snapshot
            .bindings
            .iter()
            .map(|b| BindingRow {
                parameter: b.parameter,
                bone: b.bone,
                channel: channel_label(b.channel),
            })
            .collect(),
    }
}

impl App {
    /// The entity the Animation panes show: the one selected in the hierarchy.
    fn animation_entity(&self) -> Option<(u32, String)> {
        self.entities
            .get(self.selected)
            .map(|entity| (entity.id, entity.name.clone()))
    }

    fn pane_showing(&self, kind: PaneKind) -> bool {
        self.panes
            .iter()
            .any(|(_, group)| group.pages.get(group.active) == Some(&kind))
    }

    /// Reads the PhaseShift and the rig of the selected entity again (after a selection, an edit, an undo, a load).
    pub(super) fn refresh_animation_panes(&mut self) {
        let Some((entity, owner)) = self.animation_entity() else {
            self.animation.phase_shift = None;
            self.animation.phase_shift_entity = None;
            self.animation.rig = None;
            self.animation.rig_entity = None;
            return;
        };
        let (phase_shift, rig) = match self.engine.as_ref() {
            Some(engine) => (
                engine
                    .phase_shift(entity)
                    .ok()
                    .map(|snapshot| phase_shift_model(&snapshot, &owner)),
                engine
                    .rig2d(entity)
                    .ok()
                    .flatten()
                    .map(|snapshot| rig_model(&snapshot, &owner)),
            ),
            None => (None, None),
        };
        self.animation.phase_shift_entity = phase_shift.as_ref().map(|_| entity);
        self.animation.phase_shift = phase_shift;
        self.animation.rig_entity = rig.as_ref().map(|_| entity);
        self.animation.rig = rig;
        if let (Some(index), Some(model)) = (
            self.animation.phase_shift_state,
            self.animation.phase_shift.as_ref(),
        ) {
            if index >= model.states.len() {
                self.animation.phase_shift_state = None;
            }
        }
        if let (Some(index), Some(model)) = (self.animation.rig_bone, self.animation.rig.as_ref()) {
            if index >= model.bones.len() {
                self.animation.rig_bone = None;
            }
        }
    }

    /// Keeps the panes live while one of them shows, like the Audio tab does.
    pub(super) fn animation_tick(&mut self) {
        if self.pane_showing(PaneKind::PhaseShift) || self.pane_showing(PaneKind::Rig2d) {
            self.refresh_animation_panes();
        }
    }

    fn finish_animation_edit(&mut self, result: Result<(), String>, name: &str, key: String) {
        match result {
            Ok(()) => {
                self.refresh_animation_panes();
                // Sliders send a stream of values: one undo step per parameter, not per value.
                self.commit(name.to_owned(), Some(key));
            }
            Err(error) => {
                self.animation.status = Some(format!("Could not change the animation: {error}"));
                self.log(LogLevel::Error, format!("Animation: {error}"));
                self.refresh_animation_panes();
            }
        }
    }

    fn engine_call(
        &mut self,
        call: impl FnOnce(&mut doppio_engine::Engine) -> doppio_engine::EngineResult<()>,
    ) -> Result<(), String> {
        match self.engine.as_mut() {
            Some(engine) => call(engine).map_err(|error| format!("{error:?}")),
            None => Err("The engine is not available".to_owned()),
        }
    }

    pub(super) fn edit_phase_shift(&mut self, edit: PhaseShiftEdit) {
        match edit {
            PhaseShiftEdit::AddPhaseShift => {
                let Some((entity, _)) = self.animation_entity() else {
                    return;
                };
                let result = self.engine_call(|engine| engine.add_phase_shift(entity));
                self.finish_animation_edit(
                    result,
                    "Add PhaseShift",
                    format!("phaseshift:{entity}:add"),
                );
            }
            PhaseShiftEdit::AddParameter(kind) => {
                let Some(entity) = self.animation.phase_shift_entity else {
                    return;
                };
                let name = phaseshift_panel::free_parameter_name(
                    self.animation.phase_shift.as_ref(),
                    kind,
                );
                let engine_kind = engine_kind(kind);
                let result = self.engine_call(|engine| {
                    engine.add_phase_shift_parameter(entity, &name, engine_kind)
                });
                self.finish_animation_edit(
                    result,
                    "Add parameter",
                    format!("phaseshift:{entity}:add:{name}"),
                );
            }
            PhaseShiftEdit::RemoveParameter(index) => {
                let Some(entity) = self.animation.phase_shift_entity else {
                    return;
                };
                let result = self.engine_call(|engine| {
                    engine.remove_phase_shift_parameter(entity, index as u32)
                });
                self.finish_animation_edit(
                    result,
                    "Remove parameter",
                    format!("phaseshift:{entity}:remove:{index}"),
                );
            }
            PhaseShiftEdit::SelectState(index) => {
                self.animation.phase_shift_state = Some(index);
            }
            PhaseShiftEdit::SetFloat { index, value } => {
                let Some(entity) = self.animation.phase_shift_entity else {
                    return;
                };
                let result = self.engine_call(|engine| {
                    engine.set_phase_shift_float(entity, index as u32, value)
                });
                self.finish_animation_edit(
                    result,
                    "Set parameter",
                    format!("phaseshift:{entity}:{index}:value"),
                );
            }
            PhaseShiftEdit::SetSmoothing { index, seconds } => {
                let Some(entity) = self.animation.phase_shift_entity else {
                    return;
                };
                let result = self.engine_call(|engine| {
                    engine.set_phase_shift_smoothing(entity, index as u32, seconds)
                });
                self.finish_animation_edit(
                    result,
                    "Set smoothing",
                    format!("phaseshift:{entity}:{index}:smoothing"),
                );
            }
        }
    }

    pub(super) fn edit_rig2d(&mut self, edit: RigEdit) {
        match edit {
            RigEdit::SetDraft(text) => {
                self.animation.rig_draft = text;
                return;
            }
            RigEdit::Attach => {
                let path = self.animation.rig_draft.trim().to_owned();
                let Some((entity, _)) = self.animation_entity() else {
                    return;
                };
                if path.is_empty() {
                    self.animation.status =
                        Some("Type the path of a .rig2d file first.".to_owned());
                    return;
                }
                let result = self.engine_call(|engine| engine.attach_rig2d(entity, &path));
                self.finish_animation_edit(
                    result,
                    "Attach rig",
                    format!("rig2d:{entity}:attach:{path}"),
                );
                return;
            }
            _ => {}
        }
        let Some(entity) = self.animation.rig_entity else {
            return;
        };
        match edit {
            RigEdit::SelectBone(index) => self.animation.rig_bone = Some(index),
            RigEdit::SetParameter { index, value } => {
                let result = self
                    .engine_call(|engine| engine.set_rig2d_parameter(entity, index as u32, value));
                self.finish_animation_edit(
                    result,
                    "Set rig parameter",
                    format!("rig2d:{entity}:{index}"),
                );
            }
            // Handled above: the draft and the attach never need the rig to exist.
            RigEdit::SetDraft(_) | RigEdit::Attach => {}
            RigEdit::ResetParameters => {
                let changed: Vec<(usize, f32)> = self
                    .animation
                    .rig
                    .as_ref()
                    .map(|rig| {
                        rig.parameters
                            .iter()
                            .enumerate()
                            .filter(|(_, p)| p.value != p.default)
                            .map(|(i, p)| (i, p.default))
                            .collect()
                    })
                    .unwrap_or_default();
                let mut result = Ok(());
                for (index, value) in changed {
                    result = self.engine_call(|engine| {
                        engine.set_rig2d_parameter(entity, index as u32, value)
                    });
                    if result.is_err() {
                        break;
                    }
                }
                self.finish_animation_edit(result, "Reset pose", format!("rig2d:{entity}:reset"));
            }
        }
    }

    /// Opens the pane: it is brought to the front, or added next to the Inspector when it is not docked.
    pub(super) fn show_animation_pane(&mut self, kind: PaneKind) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group.pages.iter().position(|page| *page == kind) {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(kind);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_animation_panes();
    }

    pub(super) fn phase_shift_body(&self) -> Element<'_, Message> {
        match self.animation.phase_shift.as_ref() {
            Some(model) => phaseshift_panel::view(model, self.animation.phase_shift_state)
                .map(Message::PhaseShift),
            None if self.animation_entity().is_some() => {
                phaseshift_panel::empty_view().map(Message::PhaseShift)
            }
            None => status::note("Select an entity to see or give it a PhaseShift."),
        }
    }

    pub(super) fn rig2d_body(&self) -> Element<'_, Message> {
        rig2d_panel::view(
            self.animation.rig.as_ref(),
            &self.animation.rig_draft,
            self.animation.rig_bone,
        )
        .map(Message::Rig2d)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use doppio_engine::{
        PhaseShiftCondition, PhaseShiftParameter, PhaseShiftState, PhaseShiftTransition,
        Rig2dBinding, Rig2dBone, Rig2dParameter, Rig2dVertex,
    };

    #[test]
    fn phase_shift_snapshot_becomes_the_panel_model() {
        let snapshot = PhaseShiftSnapshot {
            parameters: vec![
                PhaseShiftParameter {
                    name: "speed".into(),
                    kind: PhaseShiftKind::Float,
                    value: 0.4,
                    smoothing: 0.15,
                },
                PhaseShiftParameter {
                    name: "grounded".into(),
                    kind: PhaseShiftKind::Bool,
                    value: 1.0,
                    smoothing: 0.0,
                },
            ],
            states: vec![
                PhaseShiftState {
                    name: "idle".into(),
                    clip: "idle.anim".into(),
                    speed: 1.0,
                    current: true,
                },
                PhaseShiftState {
                    name: "run".into(),
                    clip: String::new(),
                    speed: 1.5,
                    current: false,
                },
            ],
            transitions: vec![PhaseShiftTransition {
                from: "idle".into(),
                to: "run".into(),
                blend_time: 0.2,
                has_exit_time: false,
                conditions: vec![PhaseShiftCondition {
                    parameter: "speed".into(),
                    op: ConditionOp::Greater,
                    value: 0.5,
                }],
            }],
        };

        let model = phase_shift_model(&snapshot, "Hero");
        assert_eq!(model.owner, "Hero");
        assert_eq!(model.current_state.as_deref(), Some("idle"));
        assert_eq!(model.states[0].clip.as_deref(), Some("idle.anim"));
        assert_eq!(
            model.states[1].clip, None,
            "a state with no clip has no clip label"
        );
        assert_eq!(model.parameters[0].kind, ParamKind::Float);
        assert_eq!(model.parameters[0].smoothing, 0.15);
        assert_eq!(model.parameters[1].kind, ParamKind::Bool);
        assert_eq!(model.transitions[0].conditions[0].op, ">");
        assert_eq!(model.smoothed_count(), 1);
        assert!(model.warnings().is_empty());
    }

    #[test]
    fn rig_snapshot_becomes_the_rig_panel_model() {
        let snapshot = Rig2dSnapshot {
            bones: vec![
                Rig2dBone {
                    name: "root".into(),
                    parent: None,
                },
                Rig2dBone {
                    name: "arm".into(),
                    parent: Some(0),
                },
            ],
            parameters: vec![Rig2dParameter {
                name: "Bend".into(),
                minimum: 0.0,
                default_value: 0.0,
                maximum: 1.0,
                value: 0.5,
            }],
            vertices: vec![Rig2dVertex {
                influences: vec![(0, 0.5), (1, 0.5)],
            }],
            triangles: 1,
            bindings: vec![Rig2dBinding {
                parameter: 0,
                bone: 1,
                channel: RigChannel::ScaleY,
                key_count: 2,
            }],
        };

        let model = rig_model(&snapshot, "Hero");
        assert_eq!(model.bones[1].parent, Some(0));
        assert_eq!(model.parameters[0].value, 0.5);
        assert_eq!(model.bindings[0].channel, "scale y");
        assert_eq!(model.posed_count(), 1);
        assert!(model.warnings().is_empty());
    }

    #[test]
    fn an_empty_phase_shift_has_no_current_state() {
        let model = phase_shift_model(&PhaseShiftSnapshot::default(), "Empty");
        assert_eq!(model.current_state, None);
        assert!(model.states.is_empty());
    }
}
