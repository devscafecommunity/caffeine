//! Visual tokens for the Doppio workspace.
//!
//! Panels and components read colours, radii and spacing from here so the
//! editor reads as one surface instead of a pile of default widgets.

use iced::widget::{button, container, progress_bar};
use iced::{Background, Border, Color, Shadow, Theme};

pub const BACKGROUND: Color = Color::from_rgba8(0x15, 0x15, 0x15, 1.0);
pub const PANEL: Color = Color::from_rgba8(0x24, 0x24, 0x24, 1.0);
pub const PANEL_ALT: Color = Color::from_rgba8(0x2e, 0x2e, 0x2e, 1.0);
pub const SURFACE: Color = Color::from_rgba8(0x1a, 0x1a, 0x1a, 1.0);
pub const TOOLBAR: Color = Color::from_rgba8(0x1e, 0x1e, 0x1e, 1.0);
pub const BORDER: Color = Color::from_rgba8(0x31, 0x31, 0x31, 1.0);
pub const BORDER_STRONG: Color = Color::from_rgba8(0x47, 0x47, 0x47, 1.0);
pub const TEXT: Color = Color::from_rgba8(0xd6, 0xd6, 0xd6, 1.0);
pub const TEXT_MUTED: Color = Color::from_rgba8(0x8e, 0x8e, 0x8e, 1.0);
pub const ACCENT: Color = Color::from_rgba8(0x4a, 0x8f, 0xe0, 1.0);
pub const ACCENT_HOVER: Color = Color::from_rgba8(0x62, 0xa1, 0xea, 1.0);
pub const ACCENT_PRESSED: Color = Color::from_rgba8(0x2f, 0x5f, 0x96, 1.0);
pub const DANGER: Color = Color::from_rgba8(0xc2, 0x4f, 0x4f, 1.0);
pub const DANGER_HOVER: Color = Color::from_rgba8(0xd3, 0x62, 0x62, 1.0);
pub const WARNING: Color = Color::from_rgba8(0xd1, 0xa0, 0x3c, 1.0);
pub const SUCCESS: Color = Color::from_rgba8(0x5d, 0xa5, 0x6b, 1.0);
pub const ICON_PURPLE: Color = Color::from_rgba8(0xb8, 0x8c, 0xf0, 1.0);
pub const ICON_TEAL: Color = Color::from_rgba8(0x5c, 0xc7, 0xcf, 1.0);
pub const ICON_CYAN: Color = Color::from_rgba8(0x6f, 0xb7, 0xe8, 1.0);
/// Viewport overlays (transform gizmo, scene markers).
pub const OVERLAY_HALO: Color = Color::from_rgba(0.0, 0.0, 0.0, 0.35);
pub const OVERLAY_NEUTRAL: Color = Color::from_rgb(0.86, 0.86, 0.9);
pub const OVERLAY_SPHERE_FILL: Color = Color::from_rgba(0.75, 0.8, 0.95, 0.05);
pub const OVERLAY_SPHERE_EDGE: Color = Color::from_rgba(0.85, 0.88, 1.0, 0.22);
pub const OVERLAY_WHITE: Color = Color::from_rgb(1.0, 1.0, 1.0);
pub const OVERLAY_PLATE: Color = Color::from_rgba(0.06, 0.07, 0.09, 0.62);
pub const ICON_ORANGE: Color = Color::from_rgba8(0xf0, 0x9a, 0x4a, 1.0);
pub const ICON_PINK: Color = Color::from_rgba8(0xe8, 0x7f, 0xb4, 1.0);

/// Colours of the lines in a chart, one per series (the profiler gives each CPU core its own). Chosen to stay
/// apart from each other on the dark panel; they wrap around after twelve.
pub const SERIES: [Color; 12] = [
    Color::from_rgba8(0x4a, 0x8f, 0xe0, 1.0),
    Color::from_rgba8(0xf0, 0x9a, 0x4a, 1.0),
    Color::from_rgba8(0x5d, 0xa5, 0x6b, 1.0),
    Color::from_rgba8(0xe8, 0x7f, 0xb4, 1.0),
    Color::from_rgba8(0xb8, 0x8c, 0xf0, 1.0),
    Color::from_rgba8(0x5c, 0xc7, 0xcf, 1.0),
    Color::from_rgba8(0xd1, 0xc2, 0x3c, 1.0),
    Color::from_rgba8(0xc2, 0x4f, 0x4f, 1.0),
    Color::from_rgba8(0x8f, 0xd1, 0x4a, 1.0),
    Color::from_rgba8(0x4a, 0xd1, 0xa0, 1.0),
    Color::from_rgba8(0xd1, 0x6a, 0x3c, 1.0),
    Color::from_rgba8(0x8e, 0x9b, 0xf0, 1.0),
];

/// The colour of series `index` (wraps around).
pub fn series_color(index: usize) -> Color {
    SERIES[index % SERIES.len()]
}

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
            radius: 3.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn panel_title(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(TOOLBAR.into()),
        text_color: Some(TEXT),
        ..container::Style::default()
    }
}

/// Thin vertical bar in the Texture Generator's orange, the visual cue that separates it from
/// the Material Generator.
pub fn accent_bar(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(ICON_ORANGE.into()),
        border: Border {
            radius: 1.5.into(),
            ..Border::default()
        },
        ..container::Style::default()
    }
}

/// Thin vertical bar in green: the Nature Generator's identity next to the cyan Material and the
/// orange Texture generators.
pub fn nature_bar(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(SUCCESS.into()),
        border: Border {
            radius: 1.5.into(),
            ..Border::default()
        },
        ..container::Style::default()
    }
}

pub fn water_bar(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(ICON_CYAN.into()),
        border: Border {
            radius: 1.5.into(),
            ..Border::default()
        },
        ..container::Style::default()
    }
}

pub fn terrain_bar(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(ICON_ORANGE.into()),
        border: Border {
            radius: 1.5.into(),
            ..Border::default()
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
            radius: 2.0.into(),
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
            radius: 3.0.into(),
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
            radius: 2.0.into(),
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
            radius: 3.0.into(),
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
            radius: 3.0.into(),
        },
        shadow: Shadow {
            color: Color::from_rgba8(0, 0, 0, 0.35),
            offset: iced::Vector::new(0.0, 4.0),
            blur_radius: 10.0,
        },
        ..container::Style::default()
    }
}

/// The hairline between two groups of a menu.
pub fn menu_separator(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(BORDER.into()),
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
            radius: 2.0.into(),
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
        background: if drop_target {
            Some(Color { a: 0.25, ..ACCENT }.into())
        } else if active {
            Some(PANEL.into())
        } else if hovered {
            Some(PANEL_ALT.into())
        } else {
            None
        },
        border: Border::default(),
        text_color: Some(if active || hovered { TEXT } else { TEXT_MUTED }),
        ..container::Style::default()
    }
}

/// 2 px bar under the active tab.
pub fn tab_indicator(active: bool) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: active.then(|| ACCENT.into()),
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
            radius: 3.0.into(),
        },
        shadow: Shadow::default(),
        snap: true,
    }
}

/// A transport button (Play, Pause, Stop): a flat square that lights up in its colour when it is the active state.
pub fn transport_button(
    active: bool,
    color: Color,
) -> impl Fn(&Theme, button::Status) -> button::Style {
    move |_theme, status| {
        let hovered = matches!(status, button::Status::Hovered | button::Status::Pressed);
        button::Style {
            background: if active {
                Some(Color { a: 0.22, ..color }.into())
            } else if hovered {
                Some(PANEL_ALT.into())
            } else {
                Some(SURFACE.into())
            },
            text_color: if active || hovered { color } else { TEXT },
            border: Border {
                color: if active { color } else { BORDER_STRONG },
                width: 1.0,
                radius: 4.0.into(),
            },
            shadow: Shadow::default(),
            snap: true,
        }
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
            radius: 2.0.into(),
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
            radius: 3.0.into(),
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
                radius: 2.0.into(),
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
            radius: 3.0.into(),
        },
        ..container::Style::default()
    }
}

pub fn chip(tone: ChipTone) -> impl Fn(&Theme) -> container::Style {
    let (background, border, text) = match tone {
        ChipTone::Neutral => (TOOLBAR, BORDER, TEXT_MUTED),
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
            radius: 3.0.into(),
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
        button::Status::Hovered => Color::from_rgba8(0x3a, 0x3a, 0x3a, 1.0),
        button::Status::Pressed => ACCENT_PRESSED,
        button::Status::Disabled => TOOLBAR,
        _ => PANEL_ALT,
    };
    button::Style {
        background: Some(background.into()),
        text_color: if matches!(status, button::Status::Disabled) {
            TEXT_MUTED
        } else {
            TEXT
        },
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 2.0.into(),
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
            radius: 2.0.into(),
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
            radius: 2.0.into(),
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

/// Thin, low-contrast scrollbar that brightens on hover.
pub fn scrollbar(
    _theme: &Theme,
    status: iced::widget::scrollable::Status,
) -> iced::widget::scrollable::Style {
    use iced::widget::scrollable::{AutoScroll, Rail, Scroller, Status, Style};
    let hovered = matches!(
        status,
        Status::Hovered {
            is_vertical_scrollbar_hovered: true,
            ..
        } | Status::Hovered {
            is_horizontal_scrollbar_hovered: true,
            ..
        } | Status::Dragged { .. }
    );
    let rail = Rail {
        background: Some(Color { a: 0.35, ..SURFACE }.into()),
        border: Border {
            radius: 3.0.into(),
            ..Border::default()
        },
        scroller: Scroller {
            background: if hovered { ACCENT } else { BORDER_STRONG }.into(),
            border: Border {
                radius: 3.0.into(),
                ..Border::default()
            },
        },
    };
    Style {
        container: container::Style::default(),
        vertical_rail: rail,
        horizontal_rail: rail,
        gap: None,
        auto_scroll: AutoScroll {
            background: PANEL_ALT.into(),
            border: Border {
                color: BORDER_STRONG,
                width: 1.0,
                radius: 10.0.into(),
            },
            shadow: Shadow::default(),
            icon: TEXT,
        },
    }
}

/// Filled-rail slider: accent up to the handle, muted after it, with a small
/// round handle that grows on hover/drag.
pub fn slider(_theme: &Theme, status: iced::widget::slider::Status) -> iced::widget::slider::Style {
    use iced::widget::slider::{Handle, HandleShape, Rail, Status, Style};
    let (radius, color) = match status {
        Status::Active => (5.0, ACCENT),
        Status::Hovered => (6.0, ACCENT_HOVER),
        Status::Dragged => (7.0, TEXT),
    };
    Style {
        rail: Rail {
            backgrounds: (ACCENT_PRESSED.into(), BORDER_STRONG.into()),
            width: 4.0,
            border: Border {
                radius: 2.0.into(),
                ..Border::default()
            },
        },
        handle: Handle {
            shape: HandleShape::Circle { radius },
            background: color.into(),
            border_width: 2.0,
            border_color: PANEL,
        },
    }
}

/// Palette handed to Iced so stock widgets (inputs, pick lists, checkboxes)
/// pick up the workspace colours without per-call styling.
pub fn app_theme() -> Theme {
    Theme::custom(
        "Doppio".to_owned(),
        iced::theme::Palette {
            background: BACKGROUND,
            text: TEXT,
            primary: ACCENT,
            success: SUCCESS,
            warning: WARNING,
            danger: DANGER,
        },
    )
}

/// Base text size for the whole editor (Iced defaults to 16, which is too
/// large for a dense tool UI).
pub const BASE_TEXT_SIZE: f32 = 12.0;

/// Window defaults shared by the plain and agent-instrumented entry points.
pub fn window() -> iced::window::Settings {
    iced::window::Settings {
        size: iced::Size::new(1440.0, 860.0),
        min_size: Some(iced::Size::new(1000.0, 620.0)),
        ..iced::window::Settings::default()
    }
}

pub fn settings() -> iced::Settings {
    iced::Settings {
        default_text_size: BASE_TEXT_SIZE.into(),
        antialiasing: true,
        ..iced::Settings::default()
    }
}

/// Idle asset tile: a raised card with a hairline border, so a grid of assets reads as objects.
pub fn asset_card(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 6.0.into(),
        },
        text_color: Some(TEXT),
        ..container::Style::default()
    }
}

/// A selected asset tile: accent ring over the pressed accent.
pub fn asset_card_selected(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(
            Color {
                a: 0.35,
                ..ACCENT_PRESSED
            }
            .into(),
        ),
        border: Border {
            color: ACCENT,
            width: 1.0,
            radius: 6.0.into(),
        },
        text_color: Some(TEXT),
        ..container::Style::default()
    }
}

/// The well behind an asset's icon: the kind colour, dimmed, with a rounded edge.
pub fn asset_tile(accent: Color) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: Some(Color { a: 0.14, ..accent }.into()),
        border: Border {
            color: Color { a: 0.45, ..accent },
            width: 1.0,
            radius: 5.0.into(),
        },
        ..container::Style::default()
    }
}

/// A thumbnail's frame: transparent fill, so the picture keeps its colours.
pub fn asset_thumbnail_frame(_theme: &Theme) -> container::Style {
    container::Style {
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 5.0.into(),
        },
        ..container::Style::default()
    }
}

/// The small extension / kind chip on a tile corner.
pub fn asset_chip(accent: Color) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: Some(
            Color {
                a: 0.82,
                ..BACKGROUND
            }
            .into(),
        ),
        border: Border {
            color: Color { a: 0.7, ..accent },
            width: 1.0,
            radius: 3.0.into(),
        },
        text_color: Some(accent),
        ..container::Style::default()
    }
}

/// The thin bar across the top of a card that names its kind by colour.
pub fn asset_accent_bar(accent: Color) -> impl Fn(&Theme) -> container::Style {
    move |_theme| container::Style {
        background: Some(accent.into()),
        border: Border {
            radius: 2.0.into(),
            ..Border::default()
        },
        ..container::Style::default()
    }
}

/// A filter chip row above a picker list (kind filters).
pub fn picker_header(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(PANEL.into()),
        border: Border {
            color: BORDER,
            width: 1.0,
            radius: 0.0.into(),
        },
        ..container::Style::default()
    }
}

/// Inset text field: dark well, hairline border, accent ring on focus.
pub fn input(
    _theme: &Theme,
    status: iced::widget::text_input::Status,
) -> iced::widget::text_input::Style {
    use iced::widget::text_input::{Status, Style};
    let (border, background) = match status {
        Status::Active => (BORDER_STRONG, SURFACE),
        Status::Hovered => (Color::from_rgba8(0x5a, 0x5a, 0x5a, 1.0), SURFACE),
        Status::Focused { .. } => (ACCENT, Color::from_rgba8(0x16, 0x16, 0x16, 1.0)),
        Status::Disabled => (BORDER, TOOLBAR),
    };
    Style {
        background: background.into(),
        border: Border {
            color: border,
            width: 1.0,
            radius: 2.0.into(),
        },
        icon: TEXT_MUTED,
        placeholder: Color {
            a: 0.55,
            ..TEXT_MUTED
        },
        value: TEXT,
        selection: Color { a: 0.45, ..ACCENT },
    }
}

/// Closed dropdown: same inset well as text inputs.
pub fn pick_list(
    _theme: &Theme,
    status: iced::widget::pick_list::Status,
) -> iced::widget::pick_list::Style {
    use iced::widget::pick_list::{Status, Style};
    let border = match status {
        Status::Active => BORDER_STRONG,
        Status::Hovered | Status::Opened { is_hovered: true } => {
            Color::from_rgba8(0x5a, 0x5a, 0x5a, 1.0)
        }
        Status::Opened { .. } => ACCENT,
    };
    Style {
        text_color: TEXT,
        placeholder_color: TEXT_MUTED,
        handle_color: TEXT_MUTED,
        background: SURFACE.into(),
        border: Border {
            color: border,
            width: 1.0,
            radius: 2.0.into(),
        },
    }
}

/// Open dropdown list.
pub fn pick_menu(_theme: &Theme) -> iced::overlay::menu::Style {
    iced::overlay::menu::Style {
        background: PANEL_ALT.into(),
        border: Border {
            color: BORDER_STRONG,
            width: 1.0,
            radius: 3.0.into(),
        },
        text_color: TEXT,
        selected_text_color: Color::WHITE,
        selected_background: ACCENT_PRESSED.into(),
        shadow: Shadow {
            color: Color::from_rgba8(0, 0, 0, 0.4),
            offset: iced::Vector::new(0.0, 3.0),
            blur_radius: 8.0,
        },
    }
}
