//! The Go to panel list: every panel of the editor by name, so a tab a crowded strip scrolls away is one search from view.

use super::*;
use crate::uikit::components::panel_palette::{Row, Section};

/// The group a panel is listed under and what it is for.
fn describe(kind: PaneKind) -> (&'static str, &'static str) {
    match kind {
        PaneKind::Hierarchy => ("Scene", "The entities of the scene as a tree"),
        PaneKind::Inspector => ("Scene", "Components of the selected entity"),
        PaneKind::Viewport => ("Scene", "The 3D view of the scene"),
        PaneKind::Assets => ("Scene", "Project files and folders"),
        PaneKind::Console => ("Scene", "Messages, errors and actions"),
        PaneKind::Gameplay => ("Run and inspect", "Run the game in the editor"),
        PaneKind::CameraPreview => ("Run and inspect", "Preview through a scene camera"),
        PaneKind::Performance => ("Run and inspect", "Render options and frame time"),
        PaneKind::Profiler => ("Run and inspect", "Timed scopes and GPU passes"),
        PaneKind::Photon => ("Run and inspect", "Global illumination"),
        PaneKind::Environment => ("Run and inspect", "Clouds, fog and rain"),
        PaneKind::Audio => ("Run and inspect", "Volumetric sound"),
        PaneKind::Material => ("Materials", "Edit a PBR material"),
        PaneKind::Generator => ("Materials", "Node graph that bakes material maps"),
        PaneKind::TextureGenerator => ("Materials", "Procedural textures"),
        PaneKind::TextureSet => ("Materials", "Paint layers and masks over UVs"),
        PaneKind::TerrainGenerator => ("Generators", "Terrain from a node graph"),
        PaneKind::World => ("Generators", "Compose a whole world"),
        PaneKind::NatureGenerator => ("Generators", "Rocks, trees and plants"),
        PaneKind::WaterGenerator => ("Generators", "Water surfaces"),
        PaneKind::TerrainBrush => ("Terrain tools", "Sculpt the ground"),
        PaneKind::TerrainPaint => ("Terrain tools", "Paint layers and masks"),
        PaneKind::TerrainVegetation => ("Terrain tools", "Plants and rocks on the terrain"),
        PaneKind::TerrainPath => ("Terrain tools", "Roads and rivers by spline"),
        PaneKind::PhaseShift => ("Animation", "Animator states"),
        PaneKind::Rig2d => ("Animation", "2D bones and parameters"),
        PaneKind::Script => ("Code", "Lua script editor"),
        PaneKind::DryadNodes => ("Code", "Dryad node graphs"),
    }
}

const GROUPS: [&str; 8] = [
    "Scene",
    "Run and inspect",
    "Materials",
    "Generators",
    "Terrain tools",
    "Animation",
    "Code",
    "",
];

impl PaneKind {
    /// The message that shows this panel, when it has one that also readies its state.
    fn show_message(self) -> Option<Message> {
        Some(match self {
            PaneKind::Gameplay => Message::ShowGameplay,
            PaneKind::CameraPreview => Message::ShowCameraPreview,
            PaneKind::TextureGenerator => Message::ShowTextureGenerator,
            PaneKind::NatureGenerator => Message::ShowNatureGenerator,
            PaneKind::WaterGenerator => Message::ShowWaterGenerator,
            PaneKind::TerrainGenerator => Message::ShowTerrainGenerator,
            PaneKind::World => Message::ShowWorldComposer,
            PaneKind::Photon => Message::ShowPhoton,
            PaneKind::Environment => Message::ShowEnvironment,
            PaneKind::Audio => Message::ShowAudio,
            PaneKind::Performance => Message::ShowPerformance,
            PaneKind::TerrainBrush => Message::ShowTerrainBrush,
            PaneKind::TerrainPaint => Message::ShowTerrainPaint,
            PaneKind::TerrainVegetation => Message::ShowTerrainVegetation,
            PaneKind::TerrainPath => Message::ShowTerrainPath,
            PaneKind::PhaseShift => Message::ShowPhaseShift,
            PaneKind::Rig2d => Message::ShowRig2d,
            PaneKind::Profiler => Message::ShowProfiler,
            _ => return None,
        })
    }
}

impl App {
    /// Brings a panel to the front of its group (the menu entries' own message when there is one).
    pub(super) fn go_to_pane(&mut self, kind: PaneKind) {
        if let Some(message) = kind.show_message() {
            self.update(message);
            return;
        }
        self.panes.restore();
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group.pages.iter().position(|page| *page == kind) {
                group.active = index;
            }
        }
    }

    /// The panels that match `search` (by name, group or purpose), grouped.
    pub(super) fn panel_palette_sections(&self, search: &str) -> Vec<Section<Message>> {
        let search = search.trim().to_lowercase();
        let showing: Vec<PaneKind> = self
            .panes
            .iter()
            .map(|(_, group)| group.active_kind())
            .collect();
        let mut index = 0;
        GROUPS
            .iter()
            .map(|title| {
                let rows = PaneKind::ALL
                    .iter()
                    .copied()
                    .filter(|kind| describe(*kind).0 == *title)
                    .filter(|kind| {
                        let (group, detail) = describe(*kind);
                        search.is_empty()
                            || kind.title().to_lowercase().contains(&search)
                            || group.to_lowercase().contains(&search)
                            || detail.to_lowercase().contains(&search)
                    })
                    .map(|kind| {
                        let row = Row {
                            id: id::panel_palette_item(index),
                            label: kind.title().to_owned(),
                            detail: describe(kind).1,
                            showing: showing.contains(&kind),
                            on_pick: Message::GoToPane(kind),
                        };
                        index += 1;
                        row
                    })
                    .collect();
                Section { title, rows }
            })
            .collect()
    }
}
