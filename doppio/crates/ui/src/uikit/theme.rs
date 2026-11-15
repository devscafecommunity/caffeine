//! Visual tokens for the Doppio workspace.
//!
//! Panels and components read colours, radii and spacing from here so the
//! editor reads as one surface instead of a pile of default widgets.

use iced::widget::{button, container, progress_bar};
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
pub const ICON_PURPLE: Color = Color::from_rgba8(0xb8, 0x8c, 0xf0, 1.0);
pub const ICON_TEAL: Color = Color::from_rgba8(0x5c, 0xc7, 0xcf, 1.0);
pub const ICON_CYAN: Color = Color::from_rgba8(0x6f, 0xb7, 0xe8, 1.0);

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

pub fn panel_title(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL.into()),
        text_color: Some(TEXT),
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

pub fn material_preview(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(SURFACE.into()),
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 6.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn texture_slot(filled: bool) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: Some(if filled { PANEL_ALT } else { SURFACE }.into()),
        border: Border {
            color: if filled { ACCENT_PRESSED } else { BORDER },
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

pub fn menu(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL_ALT.into()),
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 5.0.into(),
        },
        shadow: Shadow {
            color: Color::from_rgba8(0, 0, 0, 0.35),
            offset: iced::Vector::new(0.0, 4.0),
            blur_radius: 10.0,
        },
        ..container::Style::default()
    }
}

pub fn menu_trigger(open: bool) -> impl Fn(&Theme, button::Status) -> button::Style {
    move |_theme, _status| button::Style {
        background: if open { Some(PANEL_ALT.into()) } else { None },
        text_color: if open { TEXT } else { TEXT_MUTED },
        border: Border::default(),
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn rgba(color: [f32; 4]) -> Color {
    Color::from_rgba(
        color[0].clamp(0.0, 1.0),
        color[1].clamp(0.0, 1.0),
        color[2].clamp(0.0, 1.0),
        color[3].clamp(0.0, 1.0),
    )
}

pub fn swatch(color: Color) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: Some(color.into()),
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 4.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn tab_item(
    active: bool,
    hovered: bool,
    drop_target: bool,
) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: if active || drop_target {
            Some(ACCENT_PRESSED.into())
        } else if hovered {
            Some(PANEL_ALT.into())
        } else {
            None
        },
        border: if drop_target {
            Border {
                color: ACCENT,
                width: 1.0,
                radius: 3.0.into(),
            }
        } else {
            Border::default()
        },
        text_color: Some(if active { TEXT } else { TEXT_MUTED }),
        ..container::Style::default()
    }
}

pub fn tab(active: bool) -> impl Fn(&Theme, button::Status) -> button::Style {
    move |_theme, status| button::Style {
        background: if active || matches!(status, button::Status::Hovered | button::Status::Pressed)
        {
            Some(PANEL_ALT.into())
        } else {
            None
        },
        text_color: if active { TEXT } else { TEXT_MUTED },
        border: Border::default(),
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn filter_chip(active: bool, color: Color) -> impl Fn(&Theme, button::Status) -> button::Style {
    move |_theme, status| button::Style {
        background: if active {
            Some(Color { a: 0.16, ..color }.into())
        } else if matches!(status, button::Status::Hovered | button::Status::Pressed) {
            Some(PANEL_ALT.into())
        } else {
            None
        },
        text_color: if active { color } else { TEXT_MUTED },
        border: Border {
            color: if active { color } else { BORDER },
            width: 1.0,
            radius: 9.0.into(),
        },
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn menu_item(_theme: &Theme, status: button::Status) -> button::Style {
    button::Style {
        background: match status {
            button::Status::Hovered | button::Status::Pressed => Some(PANEL.into()),
            _ => None,
        },
        text_color: TEXT,
        border: Border::default(),
        shadow: Shadow::default(),
        snap: true,
    }
}

pub fn tree_row(
    selected: bool,
    striped: bool,
    hovered: bool,
    drop_target: bool,
) -> impl Fn(&Theme) -> container::Style {
    move |_theme| {
        let background = if selected {
            Some(ACCENT_PRESSED.into())
        } else if drop_target {
            Some(Color { a: 0.30, ..ACCENT }.into())
        } else if hovered {
            Some(
                Color {
                    a: 0.55,
                    ..PANEL_ALT
                }
                .into(),
            )
        } else if striped {
            Some(
                Color {
                    a: 0.45,
                    ..PANEL_ALT
                }
                .into(),
            )
        } else {
            None
        };
        container::Style {
            background,
            border: if drop_target {
                Border {
                    color: ACCENT,
                    width: 1.0,
                    radius: 3.0.into(),
                }
            } else {
                Border::default()
            },
            text_color: Some(TEXT),
            ..container::Style::default()
        }
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

pub fn component_card(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL_ALT.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 5.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn component_header(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL.into()),
        text_color: Some(TEXT),
        ..container::Style::default()
    }
}

pub fn property_row(striped: bool) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: striped.then(|| {
            Color {
                a: 0.45,
                ..PANEL_ALT
            }
            .into()
        }),
        text_color: Some(TEXT),
        ..container::Style::default()
    }
}

pub fn pane_drop_target(drop: bool) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: drop.then(|| Color { a: 0.10, ..ACCENT }.into()),
        border: if drop {
            Border {
                color: ACCENT,
                width: 2.0,
                radius: 4.0.into(),
            }
        } else {
            Border::default()
        },
        ..container::Style::default()
    }
}

pub fn inspector_header(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 5.0.into(),
        },
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

pub fn progress(_theme: &Theme) -> progress_bar::Style {
    progress_bar::Style {
        background: SURFACE.into(),
        bar: ACCENT.into(),
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 3.0.into(),
        },
    }
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

/// Full-window dimmer behind modal dialogs.
pub fn scrim(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(translucent_surface()),
        ..container::Style::default()
    }
}
