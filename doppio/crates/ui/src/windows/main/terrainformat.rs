//! **Terrain data format**: an old project's terrains are stored as `.cterrain`; the native container is `.terrain`.
//!
//! An old terrain opens and edits as it is (the engine reads either format), so nothing is forced. The editor says
//! which terrains are still in the old format when a project opens, and `Scene > Convert terrains to .terrain` (the
//! `convert_terrains` agent action) saves each as a `.terrain` next to the old file, which stays. The engine checks the
//! new file by reading it back bit for bit before the scene points at it.

use super::{App, LogLevel};

impl App {
    /// The terrains of the scene whose data is still in the old format: `(entity id, name)`.
    pub(super) fn legacy_terrains(&self) -> Vec<(u32, String)> {
        let Some(engine) = self.engine.as_ref() else {
            return Vec::new();
        };
        self.entities
            .iter()
            .filter(|entity| engine.terrain_data_is_legacy(entity.id) == Some(true))
            .map(|entity| (entity.id, entity.name.clone()))
            .collect()
    }

    /// Tells the console which terrains are in the old format (nothing when none is).
    pub(super) fn warn_legacy_terrains(&mut self) {
        let legacy = self.legacy_terrains();
        if legacy.is_empty() {
            return;
        }
        let names = legacy
            .iter()
            .map(|(_, name)| name.as_str())
            .collect::<Vec<_>>()
            .join(", ");
        self.log(
            LogLevel::Log,
            format!(
                "{} terrain(s) use the old .cterrain format ({names}). They work as they are; \
                 Scene > Convert terrains to .terrain saves them in the native format (the old files stay).",
                legacy.len()
            ),
        );
    }

    /// Converts every terrain still in the old format; returns how many were converted.
    pub(super) fn convert_legacy_terrains(&mut self) -> usize {
        let legacy = self.legacy_terrains();
        if legacy.is_empty() {
            self.log(LogLevel::Log, "No terrain uses the old .cterrain format");
            return 0;
        }
        let mut converted = 0;
        for (id, name) in legacy {
            let outcome = self
                .engine
                .as_mut()
                .map(|engine| engine.terrain_convert_native(id));
            match outcome {
                Some(Ok(path)) => {
                    converted += 1;
                    self.log(LogLevel::Action, format!("Converted terrain {name} to {path}"));
                }
                _ => self.log(
                    LogLevel::Error,
                    format!(
                        "Could not convert terrain {name}: the project needs a folder and the data file has to read back identical"
                    ),
                ),
            }
        }
        // The engine marks the scene changed (it now points at new files), so saving it keeps the conversion.
        converted
    }
}
