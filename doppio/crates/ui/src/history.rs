//! Undo/redo timeline for the editor.
//!
//! Each state is a full editor snapshot: the UI entity list, an optional
//! in-memory engine scene blob, the dirty flag and the selection. Snapshot
//! states are cheap enough for an editor, and they let undo cover every
//! component the engine can serialize without a per-field command for each.

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
}

impl State {
    pub fn new(entities: Vec<EntityView>, engine: Option<Vec<u8>>, dirty: bool) -> Self {
        Self {
            entities,
            engine,
            dirty,
            selected: 0,
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
        if !self.can_undo() {
            return None;
        }
        self.cursor -= 1;
        let name = self.steps[self.cursor].name.clone();
        Some((name, self.states[self.cursor].clone()))
    }

    pub fn redo(&mut self) -> Option<(String, State)> {
        if !self.can_redo() {
            return None;
        }
        let name = self.steps[self.cursor].name.clone();
        self.cursor += 1;
        Some((name, self.states[self.cursor].clone()))
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

    #[test]
    fn amend_current_updates_without_a_step() {
        let mut history = History::new(state(1));
        history.amend_current(|current| current.selected = 7);
        assert_eq!(history.current().selected, 7);
        assert!(history.is_empty());
    }
}
