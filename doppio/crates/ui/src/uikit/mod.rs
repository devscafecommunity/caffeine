pub mod asset_wizard;
pub mod behaviour;
pub mod components;
pub mod dryad;
pub mod generators;
pub mod height_pack;
pub mod icons;
pub mod id;
pub mod material_library;
pub mod materialcraft;
pub mod materialcraft_io;
pub mod mesh_material_import;
pub mod panels;
pub mod prefabs;
pub mod procedural;
pub mod project;
pub mod script;
pub mod texture_set;
pub mod theme;

/// A UI-side view of an engine entity. The engine crate owns the real data;
/// panels consume this plain struct so they stay testable without the C++ lib.
#[derive(Debug, Clone, PartialEq)]
pub struct EntityView {
    pub id: u32,
    pub name: String,
    pub position: [f32; 3],
    pub rotation: [f32; 3],
    pub scale: [f32; 3],
    pub parent: Option<u32>,
    pub icon: crate::uikit::icons::Icon,
}
