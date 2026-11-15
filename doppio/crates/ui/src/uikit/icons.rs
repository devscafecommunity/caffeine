//! Small semantic icons shared by the hierarchy and asset trees.
//!
//! They are glyphs rather than image assets: the editor UI already ships a
//! font with the required geometric shapes, and this keeps the tree rows
//! lightweight while assets gain their own tree view.

use crate::uikit::theme;
use iced::Color;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Icon {
    Scene,
    Camera,
    Light,
    Terrain,
    Volume,
    Wind,
    Player,
    Mesh,
    Water,
    Script,
    Audio,
    Physics,
    Folder,
    File,
    Material,
    Texture,
    Generic,
}

impl Icon {
    pub fn glyph(self) -> &'static str {
        match self {
            Icon::Scene => "◈",
            Icon::Camera => "◉",
            Icon::Light => "✦",
            Icon::Terrain => "▲",
            Icon::Volume => "▤",
            Icon::Wind => "≋",
            Icon::Player => "◆",
            Icon::Mesh => "◇",
            Icon::Water => "≈",
            Icon::Script => "≡",
            Icon::Audio => "♪",
            Icon::Physics => "◍",
            Icon::Folder => "▣",
            Icon::File => "▢",
            Icon::Material => "◐",
            Icon::Texture => "▦",
            Icon::Generic => "▢",
        }
    }

    pub fn color(self) -> Color {
        match self {
            Icon::Scene => theme::WARNING,
            Icon::Camera => theme::ACCENT,
            Icon::Light => theme::WARNING,
            Icon::Terrain => theme::SUCCESS,
            Icon::Volume => theme::ICON_PURPLE,
            Icon::Wind => theme::ICON_CYAN,
            Icon::Player => theme::ACCENT_HOVER,
            Icon::Mesh => theme::ICON_TEAL,
            Icon::Water => theme::ICON_CYAN,
            Icon::Script => theme::SUCCESS,
            Icon::Audio => theme::WARNING,
            Icon::Physics => theme::TEXT_MUTED,
            Icon::Folder => theme::WARNING,
            Icon::File => theme::TEXT_MUTED,
            Icon::Material => theme::ICON_PURPLE,
            Icon::Texture => theme::ICON_TEAL,
            Icon::Generic => theme::TEXT_MUTED,
        }
    }
}
