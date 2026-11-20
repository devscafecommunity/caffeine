//! Script editor state and file model.
//!
//! The pane is a thin, engine-free layer: it owns the open `.lua`/`.dyd`
//! documents, tracks dirty state against the text last read from disk and
//! writes back on save. Attaching a script to an entity is an editor concern
//! and lives in `windows/main.rs`; this module only reports the active
//! document so the window can bind it.

use crate::uikit::script_assist;
use crate::uikit::script_reference::{self, Language};
use iced_code_editor::{
    ArrowDirection, CodeEditor, IndentStyle, LspClient, LspDocument, LspOverlayMessage,
    LspOverlayState, LspPosition, LspRange, LspTextChange,
};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

/// In-process hover provider: the editor reports where the pointer rests and the
/// document answers from the scripting reference (no language server involved).
/// How long the reference card stays after the pointer leaves the word.
const HOVER_GRACE: std::time::Duration = std::time::Duration::from_millis(450);

struct HoverClient {
    pending: Arc<Mutex<Option<LspPosition>>>,
}

impl LspClient for HoverClient {
    fn request_hover(&mut self, _document: &LspDocument, position: LspPosition) {
        if let Ok(mut slot) = self.pending.lock() {
            *slot = Some(position);
        }
    }
}

fn build_editor(
    content: &str,
    syntax: &str,
    path: &Path,
    pending: &Arc<Mutex<Option<LspPosition>>>,
) -> CodeEditor {
    let mut editor = CodeEditor::new(content, syntax);
    // Everything a modern editor offers that does not need a language server.
    editor.set_line_numbers_enabled(true);
    editor.set_bracket_match_highlight_enabled(true);
    editor.set_bracket_pair_colorization_enabled(true);
    editor.set_auto_close_brackets(true);
    editor.set_auto_indent_enabled(true);
    editor.set_show_indent_guides(true);
    editor.set_folding_enabled(true);
    editor.set_sticky_scroll_enabled(true);
    editor.set_search_replace_enabled(true);
    editor.set_command_palette_enabled(true);
    editor.set_indent_style(IndentStyle::Spaces(4));
    editor.attach_lsp(
        Box::new(HoverClient {
            pending: Arc::clone(pending),
        }),
        LspDocument::new(format!("file://{}", path.display()), syntax),
    );
    editor
}

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
    /// Which reference table hover lookups use.
    pub language: Language,
    /// Reference card state for the code editor.
    pub overlay: LspOverlayState,
    pending_hover: Arc<Mutex<Option<LspPosition>>>,
    /// Name of the entry currently shown, to avoid re-parsing the card on every move.
    hover_entry: Option<String>,
    /// When the pointer left the hovered word; the card lingers briefly so the pointer can reach it.
    hover_left: Option<std::time::Instant>,
    /// Line and start column of the word a completion will replace.
    completion_start: Option<(usize, usize)>,
}

impl ScriptDocument {
    /// Resolve the pointer position reported by the editor into a reference card.
    /// Where the pointer rests, as a document position.
    fn position_at(&mut self, point: iced::Point) -> Option<LspPosition> {
        self.editor.lsp_request_hover_at(point);
        self.pending_hover
            .lock()
            .ok()
            .and_then(|mut slot| slot.take())
    }

    fn line_at(&self, line: u32) -> String {
        self.editor
            .content()
            .lines()
            .nth(line as usize)
            .unwrap_or_default()
            .to_owned()
    }

    /// The documented API entry at a position.
    fn entry_at(&mut self, point: iced::Point) -> Option<&'static script_reference::Entry> {
        let position = self.position_at(point)?;
        let line = self.line_at(position.line);
        script_reference::lookup(self.language, &line, position.character as usize)
    }

    /// A symbol declared in this file, with the line it jumps to.
    fn local_definition_at(&mut self, point: iced::Point) -> Option<script_assist::Definition> {
        let position = self.position_at(point)?;
        let line = self.line_at(position.line);
        let name = script_reference::token_at(&line, position.character as usize)?;
        let text = self.editor.content();
        let definitions = script_assist::definitions(self.language, &text);
        script_assist::find_definition(&definitions, &name, position.line as usize).cloned()
    }

    fn refresh_hover(&mut self, point: iced::Point) {
        self.hover_left = None;
        if let Some(entry) = self.entry_at(point) {
            if self.hover_entry.as_deref() != Some(entry.name) {
                self.overlay
                    .show_hover(script_reference::hover_markdown(entry));
                self.hover_entry = Some(entry.name.to_owned());
            }
            self.overlay.set_hover_position(point);
            return;
        }
        if let Some(definition) = self.local_definition_at(point) {
            let key = format!("local:{}:{}", definition.name, definition.line);
            if self.hover_entry.as_deref() != Some(key.as_str()) {
                let kind = if definition.is_function {
                    "function"
                } else {
                    "variable"
                };
                self.overlay.show_hover(format!(
                    "**{}** · {kind} declared in this file\n\n`{}`\n\nLine {}\n\n*Ctrl+click jumps to the declaration*",
                    definition.name,
                    definition.signature,
                    definition.line + 1
                ));
                self.hover_entry = Some(key);
            }
            self.overlay.set_hover_position(point);
            return;
        }
        self.linger_hover();
    }

    /// The pointer is off any documented word: keep the card for a moment (it may be
    /// travelling to it), unless the pointer is already on the card.
    fn linger_hover(&mut self) {
        if !self.overlay.hover_visible {
            self.clear_hover();
            return;
        }
        if self.overlay.hover_interactive {
            return;
        }
        let since = *self.hover_left.get_or_insert_with(std::time::Instant::now);
        if since.elapsed() > HOVER_GRACE {
            self.clear_hover();
        }
    }

    fn clear_hover(&mut self) {
        self.hover_left = None;
        if self.hover_entry.is_some() || self.overlay.hover_visible {
            self.overlay.clear_hover();
            self.hover_entry = None;
        }
    }

    /// Offer completions for the word before the cursor.
    fn refresh_completions(&mut self) {
        let (line, column) = self.editor.cursor_position();
        let text = self.editor.content();
        match script_assist::completions(self.language, &text, line, column) {
            Some(found) => {
                let mut point = self.editor.cursor_screen_position().unwrap_or_default();
                point.y += 20.0;
                self.overlay.set_completions(found.items, point);
                self.completion_start = Some((line, found.start));
            }
            None => self.clear_completions(),
        }
    }

    fn clear_completions(&mut self) {
        if self.overlay.completion_visible || self.completion_start.is_some() {
            self.overlay.clear_completions();
            self.completion_start = None;
        }
    }

    /// Replace the typed word with the highlighted candidate.
    fn accept_completion(&mut self) -> bool {
        let Some(item) = self.overlay.selected_item().map(str::to_owned) else {
            return false;
        };
        let Some((line, start)) = self.completion_start else {
            return false;
        };
        let (_, column) = self.editor.cursor_position();
        let edit = LspTextChange {
            range: LspRange {
                start: LspPosition {
                    line: line as u32,
                    character: start as u32,
                },
                end: LspPosition {
                    line: line as u32,
                    character: column as u32,
                },
            },
            text: item.clone(),
        };
        let applied = self.editor.apply_lsp_text_edits(&[edit]);
        let _ = self.editor.update(&iced_code_editor::Message::GotoPosition(
            line,
            start + item.chars().count(),
        ));
        self.clear_completions();
        applied
    }
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
    pending_reference: Option<&'static script_reference::Entry>,
}

impl State {
    /// The API entry a Ctrl+click asked to jump to, once.
    pub fn take_reference(&mut self) -> Option<&'static script_reference::Entry> {
        self.pending_reference.take()
    }

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
        let pending_hover = Arc::new(Mutex::new(None));
        self.documents.push(ScriptDocument {
            editor: build_editor(&content, syntax, &path, &pending_hover),
            path,
            relative_path,
            title: title.clone(),
            original: content,
            syntax,
            language: language_for(syntax),
            overlay: LspOverlayState::new(),
            pending_hover,
            hover_entry: None,
            hover_left: None,
            completion_start: None,
        });

        let index = self.documents.len() - 1;
        self.active = Some(index);
        self.status = Some(format!("Opened {title}"));
        Ok(index)
    }

    /// Make the document at `index` the active tab. Out-of-range indices are
    /// ignored so stale agent commands cannot break the pane.
    pub fn select(&mut self, index: usize) {
        if let Some(document) = self.documents.get(index) {
            // Typing goes to the tab you just switched to without a click first.
            document.editor.request_focus();
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

        document.editor = build_editor(
            &document.original,
            document.syntax,
            &document.path,
            &document.pending_hover,
        );
        document.clear_hover();
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
        document.editor = build_editor(
            &content,
            document.syntax,
            &document.path,
            &document.pending_hover,
        );
        document.clear_hover();
        self.status = Some(format!("Reloaded {}", document.title));
        Ok(true)
    }

    /// Apply a code-editor message to the active buffer.
    pub fn update(&mut self, message: iced_code_editor::Message) {
        use iced_code_editor::Message as M;
        let Some(document) = self.active.and_then(|index| self.documents.get_mut(index)) else {
            return;
        };
        // While the completion list is open, navigation keys drive it.
        if document.overlay.completion_visible {
            match &message {
                M::ArrowKey(ArrowDirection::Up, false) => {
                    document.overlay.navigate(-1);
                    return;
                }
                M::ArrowKey(ArrowDirection::Down, false) => {
                    document.overlay.navigate(1);
                    return;
                }
                M::Enter | M::Tab if document.accept_completion() => return,
                _ => {}
            }
        }
        // Ctrl+click: jump to the reference of an API name, or to a local declaration.
        if let M::JumpClick(point) = &message {
            document.clear_hover();
            document.clear_completions();
            if let Some(entry) = document.entry_at(*point) {
                self.pending_reference = Some(entry);
            } else if let Some(definition) = document.local_definition_at(*point) {
                let _ = document
                    .editor
                    .update(&M::GotoPosition(definition.line, definition.column));
            }
            return;
        }
        let hover = match &message {
            M::MouseHover(point) => Some(*point),
            _ => None,
        };
        let typing = matches!(
            message,
            M::CharacterInput(_) | M::Backspace | M::Delete | M::DeleteWordBack
        );
        let passive = matches!(
            message,
            M::MouseHover(_)
                | M::Tick
                | M::Scrolled(_)
                | M::HorizontalScrolled(_)
                | M::CanvasFocusGained
                | M::CanvasFocusLost
        );
        let _ = document.editor.update(&message);
        match hover {
            Some(point) => document.refresh_hover(point),
            None if passive => {}
            None => document.clear_hover(),
        }
        if typing {
            document.refresh_completions();
        } else if !passive {
            document.clear_completions();
        }
    }

    /// Route a completion or reference-card message.
    pub fn overlay_message(&mut self, message: LspOverlayMessage) {
        let Some(document) = self.active.and_then(|index| self.documents.get_mut(index)) else {
            return;
        };
        match message {
            LspOverlayMessage::HoverEntered => document.overlay.hover_interactive = true,
            LspOverlayMessage::HoverExited => {
                document.overlay.hover_interactive = false;
                document.clear_hover();
            }
            LspOverlayMessage::CompletionSelected(index) => {
                document.overlay.completion_selected = index;
                document.accept_completion();
            }
            LspOverlayMessage::CompletionConfirm => {
                document.accept_completion();
            }
            LspOverlayMessage::CompletionClosed => document.clear_completions(),
            LspOverlayMessage::CompletionNavigateUp => document.overlay.navigate(-1),
            LspOverlayMessage::CompletionNavigateDown => document.overlay.navigate(1),
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
        // Dryad is a brace language with `//` comments, `let` and `function`: the
        // JavaScript grammar highlights it correctly where the Lua one does not.
        "dyd" => "js",
        _ => "txt",
    }
}

fn language_for(syntax: &str) -> Language {
    if syntax == "js" {
        Language::Dryad
    } else {
        Language::Lua
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
    fn hovering_a_documented_name_shows_the_card() {
        let dir = TempDir::new();
        let path = dir.file("a.lua", "caffeine.time.delta()\n");
        let mut state = State::new();
        state.open(&path, None).expect("open");
        {
            let document = state.documents.get_mut(0).expect("doc");
            for _ in 0..10 {
                let _ = document.editor.update(&iced_code_editor::Message::ArrowKey(
                    iced_code_editor::ArrowDirection::Right,
                    false,
                ));
            }
        }
        let point = state
            .current()
            .and_then(|document| document.editor.cursor_screen_position())
            .expect("cursor point");
        state.update(iced_code_editor::Message::MouseHover(point));
        let document = state.current().expect("doc");
        assert!(document.overlay.hover_visible, "card shows at {point:?}");
    }

    #[test]
    fn card_lingers_until_the_pointer_reaches_it() {
        let dir = TempDir::new();
        let path = dir.file("a.lua", "caffeine.time.delta()\n\n\n\n");
        let mut state = State::new();
        state.open(&path, None).expect("open");
        {
            let document = state.documents.get_mut(0).expect("doc");
            for _ in 0..10 {
                let _ = document.editor.update(&iced_code_editor::Message::ArrowKey(
                    iced_code_editor::ArrowDirection::Right,
                    false,
                ));
            }
        }
        let on_word = state
            .current()
            .and_then(|document| document.editor.cursor_screen_position())
            .expect("cursor point");
        state.update(iced_code_editor::Message::MouseHover(on_word));
        let away = iced::Point::new(on_word.x, on_word.y + 60.0);
        state.update(iced_code_editor::Message::MouseHover(away));
        assert!(
            state.current().expect("doc").overlay.hover_visible,
            "grace keeps it"
        );
        // The pointer lands on the card: it must stay, even for a later move elsewhere.
        state.overlay_message(LspOverlayMessage::HoverEntered);
        state.update(iced_code_editor::Message::Tick);
        assert!(state.current().expect("doc").overlay.hover_visible);
        state.overlay_message(LspOverlayMessage::HoverExited);
        assert!(!state.current().expect("doc").overlay.hover_visible);
    }

    fn doc_with(text: &str, name: &str) -> (TempDir, State) {
        let dir = TempDir::new();
        let path = dir.file(name, text);
        let mut state = State::new();
        state.open(&path, None).expect("open");
        (dir, state)
    }

    fn move_to(state: &mut State, line: usize, column: usize) {
        let document = state.documents.get_mut(0).expect("doc");
        let _ = document
            .editor
            .update(&iced_code_editor::Message::GotoPosition(line, column));
    }

    #[test]
    fn ctrl_click_opens_the_reference_of_an_api_name() {
        let (_dir, mut state) = doc_with("caffeine.time.delta()\n", "a.lua");
        move_to(&mut state, 0, 12);
        let point = state
            .current()
            .and_then(|d| d.editor.cursor_screen_position())
            .expect("point");
        state.update(iced_code_editor::Message::JumpClick(point));
        let entry = state.take_reference().expect("reference requested");
        assert_eq!(entry.name, "caffeine.time.delta");
        assert!(state.take_reference().is_none());
    }

    #[test]
    fn ctrl_click_jumps_to_a_local_declaration() {
        let (_dir, mut state) = doc_with("local function helper() end\n\n\nhelper()\n", "a.lua");
        move_to(&mut state, 3, 2);
        let point = state
            .current()
            .and_then(|d| d.editor.cursor_screen_position())
            .expect("point");
        state.update(iced_code_editor::Message::JumpClick(point));
        assert!(state.take_reference().is_none());
        let (line, _) = state.current().unwrap().editor.cursor_position();
        assert_eq!(line, 0, "cursor moved to the declaration");
    }

    #[test]
    fn typing_offers_completions_and_enter_accepts_the_choice() {
        let (_dir, mut state) = doc_with("", "a.lua");
        // The editor crate has one process-wide focused editor; take it explicitly.
        state.documents[0].editor.request_focus();
        state.update(iced_code_editor::Message::CanvasFocusGained);
        for character in "caffeine.time.de".chars() {
            state.update(iced_code_editor::Message::CharacterInput(character));
        }
        let document = state.current().unwrap();
        assert!(document.overlay.completion_visible, "list is open");
        assert_eq!(
            document.overlay.selected_item(),
            Some("caffeine.time.delta")
        );
        state.update(iced_code_editor::Message::Enter);
        let document = state.current().unwrap();
        assert_eq!(document.text(), "caffeine.time.delta");
        assert!(!document.overlay.completion_visible);
        assert_eq!(document.editor.cursor_position(), (0, 19));
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

    #[test]
    fn dryad_scripts_use_the_brace_grammar_and_their_own_reference() {
        let dir = std::env::temp_dir().join(format!("doppio-script-lang-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let dyd = dir.join("move.dyd");
        let lua = dir.join("move.lua");
        std::fs::write(&dyd, "function onUpdate(entity, dt) {}\n").unwrap();
        std::fs::write(&lua, "function onUpdate(eid, dt) end\n").unwrap();

        let mut state = State::new();
        let a = state.open(&dyd, None).unwrap();
        let b = state.open(&lua, None).unwrap();
        assert_eq!(state.documents[a].editor.syntax(), "js");
        assert_eq!(state.documents[a].language, Language::Dryad);
        assert_eq!(state.documents[b].editor.syntax(), "lua");
        assert_eq!(state.documents[b].language, Language::Lua);
        std::fs::remove_dir_all(&dir).ok();
    }
}
