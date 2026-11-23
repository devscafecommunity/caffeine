//! The shared **asset picker**: one overlay every asset field opens.
//!
//! A field (an Inspector component's `Mesh Path`, a terrain layer's picture, a water look's material…)
//! asks for an asset by naming a [`PickTarget`] and the kinds it accepts. The picker lists the project's
//! matching assets with thumbnails, filters by kind and search, marks the current value, offers *None*,
//! and on a choice writes the path through the same edit the field's text box would have produced — so
//! the choice is one undo step and the field cannot tell it from typing.

use super::{App, LogLevel};
use crate::uikit::asset_kind::{self, AssetKind};
use crate::uikit::components::asset_picker;

/// Which field the picker will fill.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PickTarget {
    /// A string field of a component on the selected entity.
    ComponentField { component: usize, field: usize },
    /// A string slot of a Terrain Generator node.
    TerrainString { node: u64, index: usize },
    /// A string slot of a Water Generator node.
    WaterString { node: u64, index: usize },
}

/// What the picker's controls do.
#[derive(Debug, Clone, PartialEq)]
pub enum PickEdit {
    Search(String),
    /// Show one kind only; `None` shows every accepted kind.
    Filter(Option<AssetKind>),
    Pick(String),
    Clear,
    Close,
}

/// The picker while it is open.
#[derive(Debug, Clone)]
pub(super) struct AssetPick {
    pub target: PickTarget,
    pub kinds: Vec<AssetKind>,
    pub title: String,
    pub current: String,
    pub search: String,
    pub filter: Option<AssetKind>,
}

#[derive(Default)]
pub(super) struct AssetPickState {
    pub open: Option<AssetPick>,
}

/// At most this many rows are built: a project can hold thousands of files.
const MAX_ROWS: usize = 300;

impl App {
    /// Opens the picker for `target`, listing assets of `kinds`.
    pub(super) fn open_asset_pick(
        &mut self,
        target: PickTarget,
        kinds: Vec<AssetKind>,
        title: String,
        current: String,
    ) {
        self.open_menu = None;
        self.asset_pick.open = Some(AssetPick {
            target,
            kinds,
            title,
            current,
            search: String::new(),
            filter: None,
        });
    }

    /// The kinds a string field of the selected entity's component accepts and a label for messages, when the
    /// field holds an asset (judged by its name); `None` for a plain text field or another kind of value.
    pub(super) fn component_asset_field(
        &self,
        component: usize,
        field: usize,
    ) -> Option<(Vec<AssetKind>, String)> {
        let data = self.components.get(component)?;
        if !matches!(
            data.values.get(field),
            Some(doppio_engine::FieldValue::String(_))
        ) {
            return None;
        }
        let info = self
            .component_types
            .iter()
            .find(|kind| kind.id == data.info.type_id)
            .and_then(|kind| kind.fields.get(field))?;
        let kinds = asset_kind::for_field(&info.name)?;
        Some((kinds, format!("{} · {}", data.info.name, info.name)))
    }

    /// Opens the picker for a string field of the selected entity's component.
    pub(super) fn open_field_pick(&mut self, component: usize, field: usize) {
        let Some(data) = self.components.get(component) else {
            return;
        };
        let Some(info) = self
            .component_types
            .iter()
            .find(|kind| kind.id == data.info.type_id)
            .and_then(|kind| kind.fields.get(field))
        else {
            return;
        };
        let Some(kinds) = asset_kind::for_field(&info.name) else {
            self.log(
                LogLevel::Ui,
                format!("{} does not take an asset", info.name),
            );
            return;
        };
        let current = match data.values.get(field) {
            Some(doppio_engine::FieldValue::String(text)) => text.clone(),
            _ => String::new(),
        };
        let title = format!("{} · {}", data.info.name, info.name);
        self.open_asset_pick(
            PickTarget::ComponentField { component, field },
            kinds,
            title,
            current,
        );
    }

    pub(super) fn edit_asset_pick(&mut self, edit: PickEdit) {
        match edit {
            PickEdit::Search(text) => {
                if let Some(pick) = self.asset_pick.open.as_mut() {
                    pick.search = text;
                }
            }
            PickEdit::Filter(kind) => {
                if let Some(pick) = self.asset_pick.open.as_mut() {
                    pick.filter = kind;
                }
            }
            PickEdit::Pick(path) => self.finish_asset_pick(path),
            PickEdit::Clear => self.finish_asset_pick(String::new()),
            PickEdit::Close => self.asset_pick.open = None,
        }
    }

    /// Writes the choice into the field the picker was opened for and closes it.
    fn finish_asset_pick(&mut self, path: String) {
        let Some(pick) = self.asset_pick.open.take() else {
            return;
        };
        if path.is_empty() {
            self.log(LogLevel::Action, format!("Cleared {}", pick.title));
        } else {
            self.log(LogLevel::Action, format!("{} → {path}", pick.title));
        }
        match pick.target {
            PickTarget::ComponentField { component, field } => {
                self.edit_component_text(component, field, 0, path);
            }
            PickTarget::TerrainString { node, index } => {
                self.edit_terrain(crate::uikit::panels::terrain_generator::Edit::SetString(
                    node, index, path,
                ));
            }
            PickTarget::WaterString { node, index } => {
                self.edit_water(crate::uikit::panels::water_generator::Edit::SetString(
                    node, index, path,
                ));
            }
        }
    }

    /// The rows the open picker shows: accepted kinds, then the kind filter, then the search.
    pub(super) fn asset_pick_rows(&self, pick: &AssetPick) -> Vec<asset_picker::Item> {
        let needle = pick.search.trim().to_lowercase();
        let mut rows: Vec<(usize, &doppio_engine::AssetInfo)> = self
            .assets
            .iter()
            .enumerate()
            .filter(|(_, asset)| !asset.is_directory && !asset.name.starts_with('.'))
            .filter(|(_, asset)| {
                let kind = AssetKind::from_key(&asset.kind);
                pick.kinds.contains(&kind) && pick.filter.is_none_or(|only| only == kind)
            })
            .filter(|(_, asset)| {
                needle.is_empty()
                    || asset.name.to_lowercase().contains(&needle)
                    || asset.relative_path.to_lowercase().contains(&needle)
            })
            .collect();
        // The current value first, then by name: the thing you are replacing is never lost in a list.
        rows.sort_by(|(_, a), (_, b)| {
            (b.relative_path == pick.current)
                .cmp(&(a.relative_path == pick.current))
                .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
        });
        rows.into_iter()
            .take(MAX_ROWS)
            .map(|(index, asset)| asset_picker::Item {
                key: asset.relative_path.clone(),
                label: asset.name.clone(),
                detail: asset.relative_path.clone(),
                thumbnail: self.asset_thumbnails.get(index).and_then(Option::clone),
            })
            .collect()
    }

    /// The kinds of `pick.kinds` the project actually has: the filter chips offer only those.
    pub(super) fn asset_pick_present_kinds(&self, pick: &AssetPick) -> Vec<AssetKind> {
        pick.kinds
            .iter()
            .copied()
            .filter(|kind| {
                self.assets
                    .iter()
                    .any(|asset| !asset.is_directory && AssetKind::from_key(&asset.kind) == *kind)
            })
            .collect()
    }
}
