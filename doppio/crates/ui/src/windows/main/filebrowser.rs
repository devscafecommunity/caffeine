//! Editor side of the **file browser**: opening it over the Import assistant, applying its edits and
//! handing the chosen files to the import queue.

use super::{App, LogLevel};
use crate::uikit::file_browser::Browser;
use crate::uikit::panels::file_browser::Edit;

impl App {
    /// Opens the browser where it was last left (or at home) over the Import assistant.
    pub(super) fn open_file_browser(&mut self) {
        let start = self
            .file_browser_last
            .clone()
            .unwrap_or_else(crate::uikit::file_browser::home);
        self.file_browser = Some(Browser::open(&start));
    }

    pub(super) fn edit_file_browser(&mut self, edit: Edit) {
        if edit == Edit::Confirm {
            self.confirm_file_browser();
            return;
        }
        if edit == Edit::Close {
            self.close_file_browser();
            return;
        }
        let Some(browser) = self.file_browser.as_mut() else {
            return;
        };
        match edit {
            Edit::Go(path) => browser.go(&path),
            Edit::Up => browser.up(),
            Edit::Place(index) => {
                if let Some(place) = browser.places.get(index).cloned() {
                    browser.go(&place.path);
                }
            }
            Edit::Press(index) => {
                if let Some(path) = browser.entries.get(index).map(|entry| entry.path.clone()) {
                    browser.toggle(&path);
                }
            }
            Edit::Open(index) => {
                let Some(entry) = browser.entries.get(index).cloned() else {
                    return;
                };
                if entry.is_dir {
                    browser.go(&entry.path);
                } else {
                    // Taking a file by double-click finishes with the selection plus that file.
                    browser.select(&entry.path);
                    self.confirm_file_browser();
                }
            }
            Edit::Search(text) => {
                browser.search = text;
                browser.reload();
            }
            Edit::PathInput(text) => browser.path_input = text,
            Edit::PathGo => browser.go_typed(),
            Edit::Hidden(on) => {
                browser.show_hidden = on;
                browser.reload();
            }
            Edit::Supported(on) => {
                browser.only_supported = on;
                browser.reload();
            }
            Edit::SelectSupported => browser.select_supported(),
            Edit::Clear => browser.selected.clear(),
            Edit::Refresh => browser.reload(),
            Edit::Noop | Edit::Confirm | Edit::Close => {}
        }
    }

    fn close_file_browser(&mut self) {
        if let Some(browser) = self.file_browser.take() {
            self.file_browser_last = Some(browser.dir);
        }
    }

    /// Queues the selection for import and closes the browser.
    fn confirm_file_browser(&mut self) {
        let Some(browser) = self.file_browser.take() else {
            return;
        };
        self.file_browser_last = Some(browser.dir.clone());
        let mut added = 0;
        for path in &browser.selected {
            let text = path.to_string_lossy().into_owned();
            if !self.wizard.sources.contains(&text) {
                self.wizard.sources.push(text);
                added += 1;
            }
        }
        self.wizard.tab = crate::uikit::panels::asset_wizard::Tab::Import;
        if added > 0 {
            self.log(
                LogLevel::Action,
                format!("Queued {added} item(s) for import ({})", browser.summary()),
            );
        }
    }
}
