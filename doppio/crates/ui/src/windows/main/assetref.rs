//! **Asset references**: every path an Inspector field or a generator node keeps goes through the engine's
//! asset manager first.
//!
//! The manager decides what a path means (relative to the project, to the assets folder or to the engine's
//! own assets, or absolute). A real file that is *outside* the project is imported into it, in the folder of
//! its kind, and the field keeps the project's own form of the path — so a scene or a graph never points at
//! `/home/someone/Downloads/rock.png`, and moves to another machine with everything it needs. A path that
//! names nothing yet (the text is still being typed) is kept as typed; a path of the wrong kind (a mesh in a
//! texture field) is refused with a message.

use super::{App, LogLevel};
use crate::uikit::asset_kind::{self, AssetKind};
use doppio_engine::{Engine, ReferenceStatus};
use std::path::Path;

/// What to do with a path that was typed or picked.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) enum Resolved {
    /// Keep this: the project's form of the path, or the text as typed while it names nothing yet.
    Store(String),
    /// Do not accept it; the message says why.
    Refuse(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) struct Outcome {
    pub resolved: Resolved,
    /// Something worth telling the user: a file that was imported.
    pub note: Option<String>,
}

fn store(text: impl Into<String>) -> Outcome {
    Outcome {
        resolved: Resolved::Store(text.into()),
        note: None,
    }
}

/// Asks the asset manager about `typed` and decides what a field of `kinds` stores (an empty `kinds` accepts
/// anything). Without an engine, or with a library older than the asset manager, the text is kept as it is.
pub(super) fn resolve_value(engine: Option<&Engine>, typed: &str, kinds: &[AssetKind]) -> Outcome {
    let trimmed = typed.trim();
    if trimmed.is_empty() {
        return store(String::new());
    }
    // First look without touching the project: the kind of the file decides whether it is welcome at all, so a
    // file of the wrong kind is refused *before* anything is copied in.
    let Some(probe) = engine.and_then(|engine| engine.asset_reference(trimmed, false)) else {
        return store(typed);
    };
    match probe.status {
        ReferenceStatus::Ok | ReferenceStatus::OutsideProject => {
            let name = if probe.asset_path.is_empty() {
                probe.absolute_path.clone()
            } else {
                probe.asset_path.clone()
            };
            if let Some(kind) = asset_kind::classify(Path::new(&probe.absolute_path)) {
                if !kinds.is_empty() && !kinds.contains(&kind) {
                    let accepted = kinds
                        .iter()
                        .map(|kind| kind.label())
                        .collect::<Vec<_>>()
                        .join(" or ");
                    return Outcome {
                        resolved: Resolved::Refuse(format!(
                            "{name} is a {}, this takes a {accepted}",
                            kind.label()
                        )),
                        note: None,
                    };
                }
            }
            // A file that is already in the project (or an engine built-in) is used where it is; one outside
            // is copied in, and the field keeps the copy's path.
            let reference = if probe.status == ReferenceStatus::OutsideProject {
                match engine.and_then(|engine| engine.asset_reference(trimmed, true)) {
                    Some(imported) if imported.is_ok() => imported,
                    _ => {
                        return Outcome {
                            resolved: Resolved::Refuse(format!(
                                "{trimmed} is outside the project and could not be imported"
                            )),
                            note: None,
                        }
                    }
                }
            } else {
                probe
            };
            let note = reference.imported.then(|| {
                format!(
                    "Imported {trimmed} into the project as {}",
                    reference.asset_path
                )
            });
            Outcome {
                resolved: Resolved::Store(reference.asset_path),
                note,
            }
        }
        // Nothing there (yet): the text is still being typed, or it names a file that is to come.
        ReferenceStatus::Empty | ReferenceStatus::Missing | ReferenceStatus::NotAFile => {
            store(typed)
        }
    }
}

impl App {
    /// [`resolve_value`] for the open engine: logs what happened and returns the text to store, or `None`
    /// when the path is refused.
    pub(super) fn reference_text(
        &mut self,
        typed: String,
        kinds: &[AssetKind],
        what: &str,
    ) -> Option<String> {
        let outcome = resolve_value(self.engine.as_ref(), &typed, kinds);
        if let Some(note) = outcome.note {
            self.log(LogLevel::Action, note);
            self.refresh_assets();
        }
        match outcome.resolved {
            Resolved::Store(text) => Some(text),
            Resolved::Refuse(message) => {
                self.log(LogLevel::Error, format!("{what}: {message}"));
                None
            }
        }
    }
}
