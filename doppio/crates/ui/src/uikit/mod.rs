pub mod components;
pub mod id;
pub mod panels;
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
}
