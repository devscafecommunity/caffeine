//! What every generator needs and none of them should have to ask for: a place to write, and a way to be heard.
//!
//! * **A workspace without a project.** The generators write meshes, materials, terrains and plans as project assets.
//!   With no project open there used to be nowhere to write, and *Place in scene* answered with one small line in a
//!   side panel. [`App::ensure_workspace`] now gives the engine a **scratch project** folder in the user's data
//!   directory the first time a generator needs one, and says so: the files show up in the Assets pane and the work
//!   is not lost when a real project is opened later (copy the folder).
//! * **A notice anyone can see.** Generators keep a `status` line in their own panel, which is easy to miss.
//!   [`App::announce_generator_status`] turns every change of one of them (and of the Scatter action's) into a notice:
//!   a chip in the top bar for a few seconds, plus an entry in the Console.

use super::{App, LogLevel};
use crate::uikit::theme::ChipTone;
use std::path::PathBuf;
use std::time::{Duration, Instant};

/// How long a notice stays in the top bar.
pub(super) const NOTICE_SECONDS: u64 = 12;

/// A message to the user, shown in the top bar for a while.
#[derive(Debug, Clone)]
pub(super) struct Notice {
    pub text: String,
    pub tone: ChipTone,
    pub at: Instant,
}

impl Notice {
    pub fn fresh(&self) -> bool {
        self.at.elapsed() < Duration::from_secs(NOTICE_SECONDS)
    }
}

/// The generators' status lines, to see which one changed.
pub(super) type StatusSnapshot = [Option<String>; 5];

/// Where the scratch project lives: `$CAFFEINE_SCRATCH`, else the user's data directory.
pub(super) fn default_scratch_dir() -> PathBuf {
    if let Some(path) = std::env::var_os("CAFFEINE_SCRATCH") {
        return PathBuf::from(path);
    }
    let base = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("APPDATA").map(PathBuf::from))
        .or_else(|| {
            std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".local").join("share"))
        })
        .unwrap_or_else(std::env::temp_dir);
    base.join("caffeine").join("doppio").join("scratch")
}

pub(super) use crate::uikit::components::status::tone_for_text as tone_for;

impl App {
    pub(super) fn scratch_root(&self) -> PathBuf {
        self.scratch_override
            .clone()
            .unwrap_or_else(default_scratch_dir)
    }

    /// Shows `text` in the top bar for a few seconds and writes it to the Console.
    pub(super) fn notify(&mut self, tone: ChipTone, text: impl Into<String>) {
        let text = text.into();
        let level = match tone {
            ChipTone::Danger => LogLevel::Error,
            _ => LogLevel::Action,
        };
        self.log(level, text.clone());
        self.notice = Some(Notice {
            text,
            tone,
            at: Instant::now(),
        });
    }

    /// Makes sure generators have somewhere to write. With a project open (or a root already set) that is the
    /// project's assets folder; otherwise the scratch project, which becomes the engine's asset root. `false` only
    /// when there is no engine to tell.
    pub(super) fn ensure_workspace(&mut self) -> bool {
        if self.asset_root_dir().is_some() {
            return true;
        }
        let scratch = self.scratch_root();
        let Some(engine) = self.engine.as_mut() else {
            return false;
        };
        if std::fs::create_dir_all(&scratch).is_err()
            || engine.set_asset_root(&scratch.to_string_lossy()).is_err()
        {
            return false;
        }
        let _ = engine.refresh_assets();
        self.assets = engine.assets();
        crate::uikit::asset_kind::refine(&mut self.assets);
        self.notify(
            ChipTone::Accent,
            format!(
                "No project is open: generated files go to the scratch folder {}",
                scratch.display()
            ),
        );
        true
    }

    pub(super) fn generator_status(&self) -> StatusSnapshot {
        [
            self.nature.status.clone(),
            self.watergen.status.clone(),
            self.terraingen.status.clone(),
            self.texgen.status.clone(),
            self.scatter_status.clone(),
        ]
    }

    /// Notices every status line that changed since `before`.
    pub(super) fn announce_generator_status(&mut self, before: &StatusSnapshot) {
        let after = self.generator_status();
        for (old, new) in before.iter().zip(after.iter()) {
            if let Some(text) = new {
                if old.as_ref() != Some(text) {
                    self.notify(tone_for(text), text.clone());
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_lines_get_a_tone_from_what_they_say() {
        assert_eq!(tone_for("Placed Oak (2 parts)"), ChipTone::Success);
        assert_eq!(
            tone_for("Scattered 500 instances (…), seed 7"),
            ChipTone::Success
        );
        assert_eq!(
            tone_for("Placed with problems: trunk: x"),
            ChipTone::Warning
        );
        assert_eq!(
            tone_for("Could not write the meshes: denied"),
            ChipTone::Danger
        );
        assert_eq!(
            tone_for("the selected entity is not a terrain"),
            ChipTone::Danger
        );
    }
}
