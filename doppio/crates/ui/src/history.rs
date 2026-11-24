//! Undo/redo timeline for the editor.
//!
//! Each state is a full editor snapshot: the UI entity list, an optional
//! in-memory engine scene blob, the dirty flag and the selection. Snapshot
//! states are cheap enough for an editor, and they let undo cover every
//! component the engine can serialize without a per-field command for each.
//!
//! Terrain heights are not in the scene snapshot (they live in the engine's
//! height cache), so a brush stroke is recorded as a **stroke step**: its state
//! carries the engine's stroke record id, and crossing the step undoes or redoes
//! only that record, tile by tile, however large the terrain is.

use crate::uikit::EntityView;

/// Maximum number of undoable steps kept in memory.
pub const MAX_STEPS: usize = 200;
/// Approximate memory cap for the timeline.
pub const MAX_BYTES: usize = 128 * 1024 * 1024;

#[derive(Debug, Clone, PartialEq)]
pub struct State {
    pub entities: Vec<EntityView>,
    /// In-memory scene snapshot; `None` when the session has no engine.
    pub engine: Option<Vec<u8>>,
    pub dirty: bool,
    pub selected: usize,
    /// The engine's record of the terrain stroke that produced this state, if it was a stroke step.
    pub terrain_stroke: Option<u32>,
}

/// What a move through the timeline asks of the terrain: the stroke record to revert (undo) or reapply (redo).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StrokeMove {
    Revert(u32),
    Reapply(u32),
}

/// The result of one undo or redo.
#[derive(Debug, Clone, PartialEq)]
pub struct Moved {
    pub name: String,
    pub state: State,
    /// Set when the step crossed was a terrain stroke; the scene needs no restore then.
    pub stroke: Option<StrokeMove>,
}

impl State {
    pub fn new(entities: Vec<EntityView>, engine: Option<Vec<u8>>, dirty: bool) -> Self {
        Self {
            entities,
            engine,
            dirty,
            selected: 0,
            terrain_stroke: None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Step {
    pub name: String,
    /// Groups repeated edits of the same thing (typing in one field).
    pub key: Option<String>,
}

pub struct History {
    states: Vec<State>,
    steps: Vec<Step>,
    cursor: usize,
    bytes: usize,
}

impl History {
    pub fn new(initial: State) -> Self {
        let mut history = Self {
            states: vec![initial],
            steps: Vec::new(),
            cursor: 0,
            bytes: 0,
        };
        history.recount();
        history
    }

    pub fn can_undo(&self) -> bool {
        self.cursor > 0
    }

    pub fn can_redo(&self) -> bool {
        self.cursor + 1 < self.states.len()
    }

    pub fn undo_name(&self) -> Option<&str> {
        self.can_undo()
            .then(|| {
                self.steps
                    .get(self.cursor - 1)
                    .map(|step| step.name.as_str())
            })
            .flatten()
    }

    pub fn redo_name(&self) -> Option<&str> {
        self.can_redo()
            .then(|| self.steps.get(self.cursor).map(|step| step.name.as_str()))
            .flatten()
    }

    /// Marks the current state as a new step, or folds it into the previous
    /// step when `key` matches (typing in the same field).
    pub fn push(&mut self, after: State, name: impl Into<String>, key: Option<String>) {
        if self.states.get(self.cursor) == Some(&after) {
            return;
        }
        let name = name.into();
        let coalesce = key.is_some()
            && self.cursor > 0
            && self.cursor == self.steps.len()
            && self.steps[self.cursor - 1].key == key;

        if coalesce {
            self.states[self.cursor] = after;
            if let Some(step) = self.steps.get_mut(self.cursor - 1) {
                step.name = name;
            }
        } else {
            self.states.truncate(self.cursor + 1);
            self.steps.truncate(self.cursor);
            self.states.push(after);
            self.steps.push(Step { name, key });
            self.cursor += 1;
        }
        self.recount();
        self.trim();
    }

    /// Updates the current state without creating a history step (selection,
    /// dirty flag after a save).
    pub fn amend_current(&mut self, update: impl FnOnce(&mut State)) {
        if let Some(state) = self.states.get_mut(self.cursor) {
            update(state);
            self.recount();
        }
    }

    pub fn current(&self) -> &State {
        &self.states[self.cursor]
    }

    pub fn undo(&mut self) -> Option<(String, State)> {
        self.undo_step().map(|moved| (moved.name, moved.state))
    }

    pub fn redo(&mut self) -> Option<(String, State)> {
        self.redo_step().map(|moved| (moved.name, moved.state))
    }

    /// Undo that also says whether a terrain stroke has to be reverted.
    pub fn undo_step(&mut self) -> Option<Moved> {
        if !self.can_undo() {
            return None;
        }
        let stroke = self.states[self.cursor]
            .terrain_stroke
            .map(StrokeMove::Revert);
        self.cursor -= 1;
        Some(Moved {
            name: self.steps[self.cursor].name.clone(),
            state: self.states[self.cursor].clone(),
            stroke,
        })
    }

    /// Redo that also says whether a terrain stroke has to be reapplied.
    pub fn redo_step(&mut self) -> Option<Moved> {
        if !self.can_redo() {
            return None;
        }
        let name = self.steps[self.cursor].name.clone();
        self.cursor += 1;
        let state = self.states[self.cursor].clone();
        let stroke = state.terrain_stroke.map(StrokeMove::Reapply);
        Some(Moved {
            name,
            state,
            stroke,
        })
    }

    pub fn steps(&self) -> &[Step] {
        &self.steps
    }

    pub fn cursor(&self) -> usize {
        self.cursor
    }

    pub fn len(&self) -> usize {
        self.steps.len()
    }

    pub fn is_empty(&self) -> bool {
        self.steps.is_empty()
    }

    pub fn bytes(&self) -> usize {
        self.bytes
    }

    fn recount(&mut self) {
        self.bytes = self.states.iter().map(state_size).sum();
    }

    fn trim(&mut self) {
        while self.cursor > 0
            && (self.steps.len() > MAX_STEPS || (self.bytes > MAX_BYTES && !self.steps.is_empty()))
        {
            self.states.remove(0);
            self.steps.remove(0);
            self.cursor -= 1;
            self.recount();
        }
    }
}

fn state_size(state: &State) -> usize {
    let names: usize = state
        .entities
        .iter()
        .map(|entity| entity.name.len() + 64)
        .sum();
    state.engine.as_ref().map_or(0, Vec::len) + names + 64
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state(value: u8) -> State {
        State::new(Vec::new(), Some(vec![value; 32]), false)
    }

    #[test]
    fn undo_and_redo_move_through_states() {
        let mut history = History::new(state(1));
        history.push(state(2), "Add Entity", None);
        history.push(state(3), "Delete Entity", None);

        assert!(history.can_undo());
        let (name, undone) = history.undo().unwrap();
        assert_eq!(name, "Delete Entity");
        assert_eq!(undone.engine, Some(vec![2; 32]));
        assert_eq!(history.undo_name(), Some("Add Entity"));

        let (name, redone) = history.redo().unwrap();
        assert_eq!(name, "Delete Entity");
        assert_eq!(redone.engine, Some(vec![3; 32]));
        assert!(!history.can_redo());
    }

    #[test]
    fn a_new_step_drops_the_redo_branch() {
        let mut history = History::new(state(1));
        history.push(state(2), "A", None);
        history.push(state(3), "B", None);
        history.undo();
        history.push(state(4), "C", None);

        assert!(!history.can_redo());
        assert_eq!(history.len(), 2);
        assert_eq!(history.current().engine, Some(vec![4; 32]));
        assert_eq!(history.undo().unwrap().0, "C");
        assert_eq!(history.undo().unwrap().0, "A");
    }

    #[test]
    fn repeated_keys_coalesce_into_one_step() {
        let mut history = History::new(state(1));
        history.push(state(2), "Rename", Some("rename:0".into()));
        history.push(state(3), "Rename", Some("rename:0".into()));
        history.push(state(4), "Rename", Some("rename:0".into()));

        assert_eq!(history.len(), 1);
        assert_eq!(history.current().engine, Some(vec![4; 32]));
        let (_, initial) = history.undo().unwrap();
        assert_eq!(initial.engine, Some(vec![1; 32]));
        assert!(!history.can_undo());

        history.push(state(5), "Move", None);
        history.push(state(6), "Rename", Some("rename:0".into()));
        assert_eq!(history.len(), 2);
    }

    #[test]
    fn identical_states_do_not_create_a_step() {
        let mut history = History::new(state(1));
        history.push(state(1), "No-op", None);
        assert!(history.is_empty());
        assert!(!history.can_undo());
    }

    #[test]
    fn history_drops_the_oldest_steps_at_the_cap() {
        let mut history = History::new(state(0));
        for value in 1..=(MAX_STEPS as u8 + 10) {
            history.push(state(value), format!("Step {value}"), None);
        }
        assert_eq!(history.len(), MAX_STEPS);
        assert_eq!(
            history.undo_name(),
            Some(format!("Step {}", MAX_STEPS as u8 + 10).as_str())
        );
        for _ in 0..MAX_STEPS {
            history.undo();
        }
        assert!(!history.can_undo());
    }

    fn stroke_state(value: u8, record: u32) -> State {
        let mut state = state(value);
        state.terrain_stroke = Some(record);
        state
    }

    #[test]
    fn a_stroke_step_asks_for_its_record_to_be_reverted_and_reapplied() {
        let mut history = History::new(state(1));
        history.push(stroke_state(1, 7), "Sculpt Terrain", None);
        // Same scene as before: the stroke state still differs by its record id.
        assert_eq!(history.len(), 1);

        let undone = history.undo_step().unwrap();
        assert_eq!(undone.name, "Sculpt Terrain");
        assert_eq!(undone.stroke, Some(StrokeMove::Revert(7)));
        assert_eq!(undone.state.terrain_stroke, None);

        let redone = history.redo_step().unwrap();
        assert_eq!(redone.stroke, Some(StrokeMove::Reapply(7)));
        assert_eq!(redone.state.terrain_stroke, Some(7));
    }

    #[test]
    fn strokes_and_scene_edits_interleave_in_order() {
        let mut history = History::new(state(1));
        history.push(stroke_state(1, 1), "Stroke A", None);
        history.push(state(2), "Rename", None);
        history.push(stroke_state(2, 2), "Stroke B", None);

        assert_eq!(
            history.undo_step().unwrap().stroke,
            Some(StrokeMove::Revert(2))
        );
        // Undoing the rename crosses a scene step: no stroke is touched.
        assert_eq!(history.undo_step().unwrap().stroke, None);
        assert_eq!(
            history.undo_step().unwrap().stroke,
            Some(StrokeMove::Revert(1))
        );
        assert!(!history.can_undo());

        assert_eq!(
            history.redo_step().unwrap().stroke,
            Some(StrokeMove::Reapply(1))
        );
        assert_eq!(history.redo_step().unwrap().stroke, None);
        assert_eq!(
            history.redo_step().unwrap().stroke,
            Some(StrokeMove::Reapply(2))
        );
    }

    #[test]
    fn two_strokes_in_a_row_are_two_steps() {
        let mut history = History::new(state(1));
        history.push(stroke_state(1, 1), "Sculpt Terrain", None);
        history.push(stroke_state(1, 2), "Sculpt Terrain", None);
        assert_eq!(history.len(), 2);
    }

    #[test]
    fn amend_current_updates_without_a_step() {
        let mut history = History::new(state(1));
        history.amend_current(|current| current.selected = 7);
        assert_eq!(history.current().selected, 7);
        assert!(history.is_empty());
    }
}
