//! Visual tokens for the Doppio workspace.
//!
//! Panels and components read colours, radii and spacing from here so the
//! editor reads as one surface instead of a pile of default widgets.

use iced::widget::{button, container};
use iced::{Background, Border, Color, Shadow, Theme};

pub const BACKGROUND: Color = Color::from_rgba8(0x12, 0x15, 0x1c, 1.0);
pub const PANEL: Color = Color::from_rgba8(0x1a, 0x1e, 0x27, 1.0);
pub const PANEL_ALT: Color = Color::from_rgba8(0x21, 0x26, 0x31, 1.0);
pub const SURFACE: Color = Color::from_rgba8(0x0d, 0x10, 0x15, 1.0);
pub const TOOLBAR: Color = Color::from_rgba8(0x16, 0x1a, 0x22, 1.0);
pub const BORDER: Color = Color::from_rgba8(0x2c, 0x33, 0x42, 1.0);
pub const BORDER_STRONG: Color = Color::from_rgba8(0x3a, 0x44, 0x58, 1.0);
pub const TEXT: Color = Color::from_rgba8(0xe7, 0xec, 0xf5, 1.0);
pub const TEXT_MUTED: Color = Color::from_rgba8(0x8f, 0x99, 0xad, 1.0);
pub const ACCENT: Color = Color::from_rgba8(0x5b, 0x7c, 0xfa, 1.0);
pub const ACCENT_HOVER: Color = Color::from_rgba8(0x6d, 0x8d, 0xff, 1.0);
pub const ACCENT_PRESSED: Color = Color::from_rgba8(0x46, 0x64, 0xd8, 1.0);
pub const DANGER: Color = Color::from_rgba8(0xc9, 0x4b, 0x5b, 1.0);
pub const DANGER_HOVER: Color = Color::from_rgba8(0xdc, 0x5c, 0x6d, 1.0);
pub const WARNING: Color = Color::from_rgba8(0xd9, 0xa4, 0x41, 1.0);
pub const SUCCESS: Color = Color::from_rgba8(0x55, 0xb8, 0x8a, 1.0);

pub fn background(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(BACKGROUND.into()),
        ..container::Style::default()
    }
}

pub fn panel(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 6.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn surface(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(SURFACE.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 4.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn toolbar(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(TOOLBAR.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 6.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn divider(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(BORDER.into()),
        border: Border::default(),
        ..container::Style::default()
    }
}

pub fn list_row(selected: bool) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: Some(if selected { ACCENT_PRESSED } else { PANEL }.into()),
        border: Border {
            color: if selected { ACCENT } else { BORDER },
            width: 1.0,
            radius: 4.0.into(),
        },
        text_color: Some(TEXT),
        ..container::Style::default()
    }
}

pub fn chip(tone: ChipTone) -> impl Fn(&Theme) -> container::Style {
    let (background, border, text) = match tone {
        ChipTone::Neutral => (PANEL_ALT, BORDER, TEXT_MUTED),
        ChipTone::Accent => (ACCENT_PRESSED, ACCENT, TEXT),
        ChipTone::Warning => (Color::from_rgba8(0x3a, 0x2f, 0x18, 1.0), WARNING, WARNING),
        ChipTone::Success => (Color::from_rgba8(0x1c, 0x32, 0x2a, 1.0), SUCCESS, SUCCESS),
        ChipTone::Danger => (Color::from_rgba8(0x39, 0x22, 0x27, 1.0), DANGER, DANGER),
    };
    move |_theme| container::Style {
        background: Some(background.into()),
        border: Border {
            color: border,
            width: 1.0,
            radius: 10.0.into(),
        },
        text_color: Some(text),
        ..container::Style::default()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChipTone {
    Neutral,
    Accent,
    Warning,
    Success,
    Danger,
}

pub fn button_secondary(_theme: &Theme, status: button::Status) -> button::Style {
    let background = match status {
        button::Status::Hovered => ACCENT_PRESSED,
        button::Status::Pressed => ACCENT,
        button::Status::Disabled => PANEL_ALT,
        _ => PANEL_ALT,
    };
    button::Style {
        background: Some(background.into()),
        text_color: TEXT,
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 4.0.into(),
        },
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn button_primary(_theme: &Theme, status: button::Status) -> button::Style {
    let background = match status {
        button::Status::Hovered => ACCENT_HOVER,
        button::Status::Pressed => ACCENT_PRESSED,
        button::Status::Disabled => ACCENT_PRESSED,
        _ => ACCENT,
    };
    button::Style {
        background: Some(background.into()),
        text_color: Color::WHITE,
        border: Border {
            color: ACCENT_HOVER,
            width: 1.0,
            radius: 4.0.into(),
        },
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn button_danger(_theme: &Theme, status: button::Status) -> button::Style {
    let background = match status {
        button::Status::Hovered => DANGER_HOVER,
        button::Status::Pressed => DANGER,
        button::Status::Disabled => DANGER,
        _ => DANGER,
    };
    button::Style {
        background: Some(background.into()),
        text_color: Color::WHITE,
        border: Border {
            color: DANGER_HOVER,
            width: 1.0,
            radius: 4.0.into(),
        },
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn button_text(_theme: &Theme, status: button::Status) -> button::Style {
    button::Style {
        background: None,
        text_color: match status {
            button::Status::Hovered => TEXT,
            _ => TEXT_MUTED,
        },
        border: Border::default(),
        shadow: Shadow::default(),
        snap: true,
    }
}

#[allow(dead_code)]
pub fn translucent_surface() -> Background {
    Color::from_rgba8(0x0b, 0x0e, 0x13, 0.8).into()
}
