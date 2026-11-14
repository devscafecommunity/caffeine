//! Hierarchical widget ids: `group.subgroup.object`.
//!
//! Iced ids are opaque strings. Doppio gives them a fixed taxonomy so tests,
//! docs scripts and agents can address a widget without guessing:
//!
//! ```
//! use doppio_ui::uikit::id;
//!
//! assert_eq!(id::ADD_ENTITY.key(), "editor.toolbar.add_entity");
//! assert_eq!(id::PANE_VIEWPORT.key(), "editor.pane.viewport");
//! ```
//!
//! Dynamic rows (entities, assets) use [`UiId::dynamic`] and keep the same
//! prefix, e.g. `hierarchy.list.entity_row.3`.

use iced::widget::Id;

/// A stable, hierarchical widget id.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct UiId(UiKey);

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
enum UiKey {
    Fixed(&'static str),
    Dynamic(String),
}

impl UiId {
    /// A compile-time id such as `editor.toolbar.add_entity`.
    pub const fn fixed(key: &'static str) -> Self {
        Self(UiKey::Fixed(key))
    }

    /// An id whose last segment is built at runtime (list rows, indices…).
    pub fn dynamic(key: impl Into<String>) -> Self {
        Self(UiKey::Dynamic(key.into()))
    }

    pub fn key(&self) -> &str {
        match &self.0 {
            UiKey::Fixed(key) => key,
            UiKey::Dynamic(key) => key,
        }
    }

    /// The `group` segment (`editor`, `hierarchy`, `inspector`, …).
    pub fn group(&self) -> &str {
        self.segments().0
    }

    /// The `subgroup` segment (`toolbar`, `pane`, `list`, `camera`, …).
    pub fn subgroup(&self) -> &str {
        self.segments().1
    }

    /// The `object` segment, including any dynamic suffix.
    pub fn object(&self) -> &str {
        self.segments().2
    }

    fn segments(&self) -> (&str, &str, &str) {
        let mut parts = self.key().splitn(3, '.');
        (
            parts.next().unwrap_or_default(),
            parts.next().unwrap_or_default(),
            parts.next().unwrap_or_default(),
        )
    }
}

impl From<UiId> for Id {
    fn from(value: UiId) -> Self {
        Id::from(value.key().to_owned())
    }
}

/// One documented widget: the id plus the text agents and docs use for it.
#[derive(Debug, Clone)]
pub struct UiMeta {
    pub id: UiId,
    pub description: &'static str,
    pub kind: &'static str,
}

// ── Editor chrome ─────────────────────────────────────────────────────────
pub const ADD_ENTITY: UiId = UiId::fixed("editor.toolbar.add_entity");
pub const TOGGLE_PLAY: UiId = UiId::fixed("editor.toolbar.toggle_play");
pub const SAVE_SCENE: UiId = UiId::fixed("editor.toolbar.save");
pub const RENDER_VIEWPORT: UiId = UiId::fixed("editor.toolbar.render");
pub const LOG_MESSAGE: UiId = UiId::fixed("editor.toolbar.log");

// ── Panes ────────────────────────────────────────────────────────────────
pub const PANE_HIERARCHY: UiId = UiId::fixed("editor.pane.hierarchy");
pub const PANE_ASSETS: UiId = UiId::fixed("editor.pane.assets");
pub const PANE_VIEWPORT: UiId = UiId::fixed("editor.pane.viewport");
pub const PANE_INSPECTOR: UiId = UiId::fixed("editor.pane.inspector");
pub const PANE_CONSOLE: UiId = UiId::fixed("editor.pane.console");

// ── Viewport ─────────────────────────────────────────────────────────────
pub const ORBIT_LEFT: UiId = UiId::fixed("viewport.camera.orbit_left");
pub const ORBIT_RIGHT: UiId = UiId::fixed("viewport.camera.orbit_right");
pub const ZOOM_IN: UiId = UiId::fixed("viewport.camera.zoom_in");
pub const ZOOM_OUT: UiId = UiId::fixed("viewport.camera.zoom_out");
pub const RESET_CAMERA: UiId = UiId::fixed("viewport.camera.reset");

// ── Inspector ────────────────────────────────────────────────────────────
pub const ENTITY_NAME: UiId = UiId::fixed("inspector.entity.name");
pub const NUDGE_X: UiId = UiId::fixed("inspector.entity.nudge_x");
pub const NUDGE_ROT_Y: UiId = UiId::fixed("inspector.entity.nudge_rot_y");
pub const SCALE_UP: UiId = UiId::fixed("inspector.entity.scale_up");
pub const DELETE_ENTITY: UiId = UiId::fixed("inspector.entity.delete");

/// One row of the hierarchy list.
pub fn entity_row(index: usize) -> UiId {
    UiId::dynamic(format!("hierarchy.list.entity_row.{index}"))
}

/// One card in the asset manager grid.
pub fn asset_card(index: usize) -> UiId {
    UiId::dynamic(format!("assets.grid.asset_card.{index}"))
}

/// The statically documented part of the UI tree.
pub const CATALOG: &[UiMeta] = &[
    UiMeta {
        id: ADD_ENTITY,
        description: "Adds an entity to the current scene.",
        kind: "button",
    },
    UiMeta {
        id: TOGGLE_PLAY,
        description: "Toggles play/pause and ticks the engine.",
        kind: "button",
    },
    UiMeta {
        id: SAVE_SCENE,
        description: "Saves the current .caf scene.",
        kind: "button",
    },
    UiMeta {
        id: RENDER_VIEWPORT,
        description: "Forces an offscreen viewport render.",
        kind: "button",
    },
    UiMeta {
        id: LOG_MESSAGE,
        description: "Writes a test message to the console.",
        kind: "button",
    },
    UiMeta {
        id: PANE_HIERARCHY,
        description: "Scene hierarchy: select, add and delete entities.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_ASSETS,
        description: "Project assets grouped by file kind.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_VIEWPORT,
        description: "Offscreen engine render with camera controls.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_INSPECTOR,
        description: "Selected entity transform and actions.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_CONSOLE,
        description: "Editor log and engine messages.",
        kind: "pane",
    },
    UiMeta {
        id: ORBIT_LEFT,
        description: "Rotates the viewport camera to the left.",
        kind: "button",
    },
    UiMeta {
        id: ORBIT_RIGHT,
        description: "Rotates the viewport camera to the right.",
        kind: "button",
    },
    UiMeta {
        id: ZOOM_IN,
        description: "Moves the camera closer to its focus.",
        kind: "button",
    },
    UiMeta {
        id: ZOOM_OUT,
        description: "Moves the camera away from its focus.",
        kind: "button",
    },
    UiMeta {
        id: RESET_CAMERA,
        description: "Restores the default camera.",
        kind: "button",
    },
    UiMeta {
        id: ENTITY_NAME,
        description: "Editable name of the selected entity.",
        kind: "text_input",
    },
    UiMeta {
        id: NUDGE_X,
        description: "Moves the selected entity +1 on the X axis.",
        kind: "button",
    },
    UiMeta {
        id: NUDGE_ROT_Y,
        description: "Rotates the selected entity +15° on Y.",
        kind: "button",
    },
    UiMeta {
        id: SCALE_UP,
        description: "Scales the selected entity by 1.1.",
        kind: "button",
    },
    UiMeta {
        id: DELETE_ENTITY,
        description: "Deletes the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("hierarchy.list.entity_row"),
        description: "A row in the hierarchy list; rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("assets.grid.asset_card"),
        description: "A project asset card; cards use the .N suffix.",
        kind: "list_row",
    },
];

/// Human description for a catalogued id, including dynamic list rows.
pub fn description(key: &str) -> Option<&'static str> {
    CATALOG
        .iter()
        .find(|meta| meta.id.key() == key)
        .or_else(|| {
            CATALOG
                .iter()
                .find(|meta| meta.kind == "list_row" && key.starts_with(meta.id.key()))
        })
        .map(|meta| meta.description)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keys_are_grouped() {
        assert_eq!(ADD_ENTITY.group(), "editor");
        assert_eq!(ADD_ENTITY.subgroup(), "toolbar");
        assert_eq!(ADD_ENTITY.object(), "add_entity");
        assert_eq!(entity_row(7).group(), "hierarchy");
        assert_eq!(entity_row(7).subgroup(), "list");
        assert_eq!(entity_row(7).object(), "entity_row.7");
    }

    #[test]
    fn catalog_is_unique_and_described() {
        let mut keys = std::collections::HashSet::new();
        for meta in CATALOG {
            assert!(keys.insert(meta.id.key()), "duplicate {}", meta.id.key());
            assert!(!meta.description.is_empty());
        }
        assert!(description("hierarchy.list.entity_row.12").is_some());
        assert!(description("assets.grid.asset_card.2").is_some());
        assert!(description("missing").is_none());
    }
}
