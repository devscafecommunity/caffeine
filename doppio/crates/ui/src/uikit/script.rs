//! Script editor state and file model.
//!
//! The pane is a thin, engine-free layer: it owns the open `.lua`/`.dyd`
//! documents, tracks dirty state against the text last read from disk and
//! writes back on save. Attaching a script to an entity is an editor concern
//! and lives in `windows/main.rs`; this module only reports the active
//! document so the window can bind it.

use iced_code_editor::CodeEditor;
use std::path::{Path, PathBuf};

/// One open script file.
pub struct ScriptDocument {
    /// Absolute (or caller-provided) path on disk, used for save/reload.
    pub path: PathBuf,
    /// Project-relative path shown in the pane and used for engine binding.
    pub relative_path: String,
    /// Tab label: the file name.
    pub title: String,
    /// Canvas-based code editor used by the pane.
    pub editor: CodeEditor,
    /// Text as last read from or written to disk.
    pub original: String,
    /// Syntax name passed to the highlighter.
    syntax: &'static str,
}

impl ScriptDocument {
    /// The current buffer as a plain string.
    pub fn text(&self) -> String {
        self.editor.content()
    }

    /// Whether the buffer differs from the last saved text.
    pub fn is_dirty(&self) -> bool {
        self.text() != self.original
    }
}

/// All open script documents plus the active tab and the last status line.
#[derive(Default)]
pub struct State {
    pub documents: Vec<ScriptDocument>,
    pub active: Option<usize>,
    pub status: Option<String>,
}

impl State {
    pub fn new() -> Self {
        Self::default()
    }

    /// Open a script, reusing the tab when the same file is already open.
    ///
    /// `relative` is the project-relative path shown in the pane; when it is
    /// absent the given path is displayed verbatim.
    pub fn open(
        &mut self,
        path: impl Into<PathBuf>,
        relative: Option<String>,
    ) -> Result<usize, String> {
        let path = path.into();

        if let Some(index) = self
            .documents
            .iter()
            .position(|document| same_file(&document.path, &path))
        {
            self.active = Some(index);
            self.status = Some(format!("Switched to {}", self.documents[index].title));
            return Ok(index);
        }

        let content = match std::fs::read_to_string(&path) {
            Ok(content) => content,
            Err(error) => {
                let message = format!("Could not open {}: {error}", path.display());
                self.status = Some(message.clone());
                return Err(message);
            }
        };

        let title = document_title(&path);
        let relative_path = relative.unwrap_or_else(|| path.display().to_string());
        let syntax = syntax_for(&path);
        self.documents.push(ScriptDocument {
            path,
            relative_path,
            title: title.clone(),
            editor: CodeEditor::new(&content, syntax),
            original: content,
            syntax,
        });

        let index = self.documents.len() - 1;
        self.active = Some(index);
        self.status = Some(format!("Opened {title}"));
        Ok(index)
    }

    /// Make the document at `index` the active tab. Out-of-range indices are
    /// ignored so stale agent commands cannot break the pane.
    pub fn select(&mut self, index: usize) {
        if index < self.documents.len() {
            self.active = Some(index);
        }
    }

    /// Close a tab, selecting the next neighbour (or the previous one at the
    /// end of the strip).
    pub fn close(&mut self, index: usize) {
        if index >= self.documents.len() {
            return;
        }

        let title = self.documents[index].title.clone();
        self.documents.remove(index);
        self.active = match self.active {
            None => None,
            Some(_) if self.documents.is_empty() => None,
            Some(active) if active == index => Some(index.min(self.documents.len() - 1)),
            Some(active) if active > index => Some(active - 1),
            Some(active) => Some(active),
        };
        self.status = Some(format!("Closed {title}"));
    }

    /// Write the buffer to disk and clear its dirty state.
    pub fn save(&mut self, index: usize) -> Result<(), String> {
        let Some(document) = self.documents.get_mut(index) else {
            let message = format!("Cannot save: no script tab {index}");
            self.status = Some(message.clone());
            return Err(message);
        };

        let text = document.editor.content();
        if let Err(error) = std::fs::write(&document.path, text.as_bytes()) {
            let message = format!("Could not save {}: {error}", document.title);
            self.status = Some(message.clone());
            return Err(message);
        }

        document.original = text;
        self.status = Some(format!("Saved {}", document.title));
        Ok(())
    }

    /// Discard the buffer and restore the last saved text.
    pub fn revert(&mut self, index: usize) -> Result<(), String> {
        let Some(document) = self.documents.get_mut(index) else {
            let message = format!("Cannot revert: no script tab {index}");
            self.status = Some(message.clone());
            return Err(message);
        };

        document.editor = CodeEditor::new(&document.original, document.syntax);
        self.status = Some(format!("Reverted {}", document.title));
        Ok(())
    }

    /// Reload a clean open document after another tool rewrote its file.
    /// Returns `Ok(false)` when there is nothing to reload (missing tab or
    /// unsaved local edits).
    pub fn reload_if_clean(&mut self, path: &Path) -> Result<bool, String> {
        let Some(index) = self
            .documents
            .iter()
            .position(|document| same_file(&document.path, path))
        else {
            return Ok(false);
        };
        if self.documents[index].is_dirty() {
            return Ok(false);
        }
        let content = std::fs::read_to_string(path)
            .map_err(|error| format!("Could not reload {}: {error}", path.display()))?;
        let document = &mut self.documents[index];
        document.original = content.clone();
        document.editor = CodeEditor::new(&content, document.syntax);
        self.status = Some(format!("Reloaded {}", document.title));
        Ok(true)
    }

    /// Apply a code-editor message to the active buffer.
    pub fn update(&mut self, message: iced_code_editor::Message) {
        if let Some(document) = self.active.and_then(|index| self.documents.get_mut(index)) {
            let _ = document.editor.update(&message);
        }
    }

    pub fn current(&self) -> Option<&ScriptDocument> {
        self.active.and_then(|index| self.documents.get(index))
    }

    pub fn current_index(&self) -> Option<usize> {
        self.active.filter(|index| *index < self.documents.len())
    }
}

fn syntax_for(path: &Path) -> &'static str {
    match path
        .extension()
        .and_then(|extension| extension.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase()
        .as_str()
    {
        "lua" => "lua",
        "dyd" => "lua",
        _ => "txt",
    }
}

fn document_title(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .map(str::to_owned)
        .unwrap_or_else(|| "script".to_owned())
}

/// Compare two paths, falling back to canonicalisation so a relative and an
/// absolute spelling of the same file share one tab.
fn same_file(left: &Path, right: &Path) -> bool {
    if left == right {
        return true;
    }

    match (std::fs::canonicalize(left), std::fs::canonicalize(right)) {
        (Ok(left), Ok(right)) => left == right,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iced_code_editor::Message as EditorMessage;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static NEXT_DIR: AtomicUsize = AtomicUsize::new(0);

    /// Unique throwaway directory; removed when the test ends.
    struct TempDir(PathBuf);

    impl TempDir {
        fn new() -> Self {
            let unique = format!(
                "doppio-script-{}-{}-{}",
                std::process::id(),
                NEXT_DIR.fetch_add(1, Ordering::SeqCst),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|elapsed| elapsed.as_nanos())
                    .unwrap_or_default()
            );
            let path = std::env::temp_dir().join(unique);
            std::fs::create_dir_all(&path).expect("create temp dir");
            Self(path)
        }

        fn file(&self, name: &str, content: &str) -> PathBuf {
            let path = self.0.join(name);
            std::fs::write(&path, content).expect("write temp script");
            path
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn insert(state: &mut State, text: &str) {
        state.update(EditorMessage::Paste(text.to_owned()));
    }

    #[test]
    fn opens_a_script_and_selects_it() {
        let dir = TempDir::new();
        let path = dir.file("player.lua", "print('hi')\n");
        let mut state = State::new();

        let index = state
            .open(&path, Some("scripts/player.lua".to_owned()))
            .expect("open script");

        assert_eq!(index, 0);
        assert_eq!(state.current_index(), Some(0));
        let document = state.current().expect("active document");
        assert_eq!(document.title, "player.lua");
        assert_eq!(document.relative_path, "scripts/player.lua");
        assert_eq!(document.text(), "print('hi')\n");
        assert!(!document.is_dirty());
        assert_eq!(state.status.as_deref(), Some("Opened player.lua"));
    }

    #[test]
    fn opening_the_same_path_reuses_the_tab() {
        let dir = TempDir::new();
        let path = dir.file("player.lua", "print('hi')\n");
        let mut state = State::new();

        let first = state.open(&path, None).expect("first open");
        let second = state
            .open(&path, Some("scripts/player.lua".to_owned()))
            .expect("reuse tab");

        assert_eq!(first, second);
        assert_eq!(state.documents.len(), 1);
    }

    #[test]
    fn editing_marks_the_document_dirty() {
        let dir = TempDir::new();
        let path = dir.file("player.lua", "print('hi')\n");
        let mut state = State::new();
        state.open(&path, None).expect("open script");

        insert(&mut state, "-- edited\n");

        let document = state.current().expect("active document");
        assert!(document.is_dirty());
        assert_ne!(document.text(), document.original);
    }

    #[test]
    fn save_writes_bytes_and_clears_dirty() {
        let dir = TempDir::new();
        let path = dir.file("player.lua", "print('hi')\n");
        let mut state = State::new();
        state.open(&path, None).expect("open script");

        insert(&mut state, "-- edited\n");
        state.save(0).expect("save");

        let document = state.current().expect("active document");
        assert!(!document.is_dirty());
        assert_eq!(
            std::fs::read_to_string(&path).expect("read saved file"),
            document.text()
        );
        assert_eq!(state.status.as_deref(), Some("Saved player.lua"));
    }

    #[test]
    fn revert_restores_the_saved_text() {
        let dir = TempDir::new();
        let path = dir.file("player.lua", "print('hi')\n");
        let mut state = State::new();
        state.open(&path, None).expect("open script");

        insert(&mut state, "oops");
        assert!(state.current().expect("active").is_dirty());

        state.revert(0).expect("revert");

        let document = state.current().expect("active document");
        assert!(!document.is_dirty());
        assert_eq!(document.text(), "print('hi')\n");
        assert_eq!(
            std::fs::read_to_string(&path).expect("file untouched"),
            "print('hi')\n"
        );
    }

    #[test]
    fn closing_tabs_selects_a_neighbour() {
        let dir = TempDir::new();
        let first = dir.file("a.lua", "a");
        let second = dir.file("b.dyd", "b");
        let mut state = State::new();
        state.open(&first, None).expect("open a");
        state.open(&second, None).expect("open b");
        assert_eq!(state.current_index(), Some(1));

        state.close(0);
        assert_eq!(state.documents.len(), 1);
        assert_eq!(state.current_index(), Some(0));
        assert_eq!(state.current().expect("active").title, "b.dyd");

        state.close(0);
        assert!(state.documents.is_empty());
        assert_eq!(state.current_index(), None);
        assert!(state.current().is_none());
    }

    #[test]
    fn closing_the_active_tab_prefers_the_previous_one_at_the_end() {
        let dir = TempDir::new();
        let first = dir.file("a.lua", "a");
        let second = dir.file("b.lua", "b");
        let mut state = State::new();
        state.open(&first, None).expect("open a");
        state.open(&second, None).expect("open b");

        state.close(1);

        assert_eq!(state.documents.len(), 1);
        assert_eq!(state.current_index(), Some(0));
        assert_eq!(state.current().expect("active").title, "a.lua");
    }

    #[test]
    fn reload_if_clean_refreshes_disk_changes_without_dropping_edits() {
        let dir = TempDir::new();
        let path = dir.file("sync.lua", "return 1\n");
        let mut state = State::new();
        state.open(&path, None).expect("open");
        std::fs::write(&path, "return 2\n").expect("external write");
        assert!(state.reload_if_clean(&path).expect("reload"));
        assert_eq!(state.current().expect("doc").text(), "return 2\n");

        insert(&mut state, "-- local");
        std::fs::write(&path, "return 3\n").expect("external write");
        assert!(!state.reload_if_clean(&path).expect("skip dirty"));
        assert!(state.current().expect("doc").text().contains("-- local"));
    }

    #[test]
    fn missing_file_reports_a_status_error() {
        let dir = TempDir::new();
        let path = dir.0.join("missing.lua");
        let mut state = State::new();

        let error = state.open(&path, None).expect_err("open fails");

        assert!(error.contains("Could not open"));
        assert!(state.documents.is_empty());
        assert_eq!(state.status.as_deref(), Some(error.as_str()));
    }

    #[test]
    fn save_failure_reports_a_status_error() {
        let dir = TempDir::new();
        let path = dir.file("player.lua", "print('hi')\n");
        let mut state = State::new();
        state.open(&path, None).expect("open script");
        insert(&mut state, "x");

        // Remove the folder so the write cannot succeed.
        std::fs::remove_file(&path).expect("remove file");
        std::fs::remove_dir(&dir.0).expect("remove dir");

        let error = state.save(0).expect_err("save fails");

        assert!(error.contains("Could not save"));
        assert_eq!(state.status.as_deref(), Some(error.as_str()));
        assert!(state.current().expect("active").is_dirty());
    }
}
