//! Terrain Path panel: roads and rivers drawn on a terrain.
//!
//! A pure view. The path (its control points, shape and surface) lives in the app (`windows/main/terrainpath.rs`);
//! the engine answers with the path it would follow and the app turns that into the elevation profile and the numbers
//! shown here, so the user sees what the road will cut and fill before applying it. The panel draws what it is given and
//! reports an [`Edit`]; the mouse work (drop, drag and remove points) happens in the viewport.

use crate::uikit::components::line_chart::{self, Chart, Series};
use crate::uikit::components::{
    button, checkbox, dropdown, palette_row, scroll, slider, status, text_field, toggle_chip,
};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use doppio_engine::SplineKind;
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::rc::Rc;

/// A ready-made road or river: one click sets every number.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Preset {
    pub name: &'static str,
    pub kind: SplineKind,
    pub width_m: f32,
    pub shoulder_m: f32,
    pub depth_m: f32,
    pub smooth_m: f32,
    pub max_grade: f32,
    /// The mask the path writes by default.
    pub mask: &'static str,
    pub blurb: &'static str,
}

pub const PRESETS: [Preset; 5] = [
    Preset {
        name: "Footpath",
        kind: SplineKind::Road,
        width_m: 1.5,
        shoulder_m: 1.5,
        depth_m: 0.0,
        smooth_m: 8.0,
        max_grade: 0.30,
        mask: "path",
        blurb: "A narrow trail that follows the ground",
    },
    Preset {
        name: "Dirt road",
        kind: SplineKind::Road,
        width_m: 4.5,
        shoulder_m: 3.0,
        depth_m: 0.0,
        smooth_m: 25.0,
        max_grade: 0.15,
        mask: "road",
        blurb: "Gentle grades, soft shoulders",
    },
    Preset {
        name: "Highway",
        kind: SplineKind::Road,
        width_m: 12.0,
        shoulder_m: 6.0,
        depth_m: 0.0,
        smooth_m: 80.0,
        max_grade: 0.06,
        mask: "road",
        blurb: "Wide and flat: cuts through hills",
    },
    Preset {
        name: "Creek",
        kind: SplineKind::River,
        width_m: 3.0,
        shoulder_m: 3.0,
        depth_m: 0.8,
        smooth_m: 12.0,
        max_grade: 0.0,
        mask: "river",
        blurb: "A shallow stream",
    },
    Preset {
        name: "River",
        kind: SplineKind::River,
        width_m: 12.0,
        shoulder_m: 8.0,
        depth_m: 2.5,
        smooth_m: 40.0,
        max_grade: 0.0,
        mask: "river",
        blurb: "A deep channel with wide banks",
    },
];

/// A number of the path.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Number {
    Width,
    Shoulder,
    Depth,
    Smooth,
    Grade,
    LayerStrength,
    /// The width at the selected point only (0 follows the path's width).
    PointWidth,
}

impl Number {
    pub const ALL: [Number; 7] = [
        Number::Width,
        Number::Shoulder,
        Number::Depth,
        Number::Smooth,
        Number::Grade,
        Number::LayerStrength,
        Number::PointWidth,
    ];

    pub fn key(self) -> &'static str {
        match self {
            Number::Width => "width",
            Number::Shoulder => "shoulder",
            Number::Depth => "depth",
            Number::Smooth => "smooth",
            Number::Grade => "grade",
            Number::LayerStrength => "layer_strength",
            Number::PointWidth => "point_width",
        }
    }

    pub fn from_key(key: &str) -> Option<Number> {
        Number::ALL
            .into_iter()
            .find(|number| number.key() == key.to_lowercase())
    }

    pub fn label(self) -> &'static str {
        match self {
            Number::Width => "Width (m)",
            Number::Shoulder => "Shoulder (m)",
            Number::Depth => "Depth (m)",
            Number::Smooth => "Smoothing (m)",
            Number::Grade => "Max grade (%)",
            Number::LayerStrength => "Layer strength",
            Number::PointWidth => "Width here (m)",
        }
    }

    pub fn range(self) -> std::ops::RangeInclusive<f32> {
        match self {
            Number::Width => 0.5..=60.0,
            Number::Shoulder => 0.0..=40.0,
            Number::Depth => 0.0..=20.0,
            Number::Smooth => 0.0..=300.0,
            Number::Grade => 1.0..=60.0,
            Number::LayerStrength => 0.0..=1.0,
            Number::PointWidth => 0.0..=60.0,
        }
    }

    pub fn step(self) -> f32 {
        match self {
            Number::Width | Number::Shoulder | Number::Depth | Number::PointWidth => 0.1,
            Number::Smooth | Number::Grade => 1.0,
            Number::LayerStrength => 0.01,
        }
    }

    pub fn id(self) -> UiId {
        match self {
            Number::Width => id::TPATH_WIDTH,
            Number::Shoulder => id::TPATH_SHOULDER,
            Number::Depth => id::TPATH_DEPTH,
            Number::Smooth => id::TPATH_SMOOTH,
            Number::Grade => id::TPATH_GRADE,
            Number::LayerStrength => id::TPATH_LAYER_STRENGTH,
            Number::PointWidth => id::TPATH_POINT_WIDTH,
        }
    }
}

/// A change the panel asks for.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    /// The drawing tool: clicks on the terrain drop points.
    Active(bool),
    Kind(SplineKind),
    /// One of `PRESETS`.
    Preset(usize),
    Number(Number, f32),
    /// The palette layer painted on the path; `None` paints none.
    Layer(Option<usize>),
    WriteMask(bool),
    MaskName(String),
    SelectPoint(usize),
    RemovePoint(usize),
    /// Drops the last point (Backspace).
    RemoveLast,
    /// Starts over (Esc).
    Clear,
    Reverse,
    /// Drops a point at a world position (the agent action; the mouse does it in the viewport). The height is found on
    /// the terrain.
    AddPoint {
        world: [f64; 3],
    },
    MovePoint {
        index: usize,
        world: [f64; 3],
    },
    /// Shapes the ground, paints the layer and writes the mask: one undo step (Enter).
    Apply,
}

/// One row of the point list.
#[derive(Debug, Clone, PartialEq)]
pub struct PointRow {
    pub x: f64,
    pub z: f64,
    /// 0 follows the path's width.
    pub width_m: f32,
}

/// What the path would do, from the engine's preview.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Stats {
    pub length_m: f32,
    /// The deepest cut and the highest fill of the ground, metres.
    pub cut_m: f32,
    pub fill_m: f32,
    /// The steepest stretch of the profile, as a percentage (rise over run).
    pub grade_percent: f32,
    /// River: how far the bed falls from source to mouth.
    pub drop_m: f32,
}

/// The ground and the path's profile along it, sampled evenly: the elevation chart.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Profile {
    pub ground: Vec<f32>,
    pub path: Vec<f32>,
}

pub struct View<'a> {
    pub active: bool,
    /// The terrain being drawn on, by name; `None` when the scene has none.
    pub terrain: Option<&'a str>,
    pub kind: SplineKind,
    pub preset: Option<usize>,
    pub width_m: f32,
    pub shoulder_m: f32,
    pub depth_m: f32,
    pub smooth_m: f32,
    /// Percent.
    pub grade_percent: f32,
    pub layer: Option<usize>,
    /// The palette's layer names; empty while layered painting is off.
    pub layer_names: &'a [String],
    pub layer_strength: f32,
    pub write_mask: bool,
    pub mask_name: &'a str,
    pub points: &'a [PointRow],
    pub selected: Option<usize>,
    pub stats: Option<&'a Stats>,
    pub profile: &'a Profile,
    pub status: Option<&'a str>,
}

/// The one-line summary under the panel title.
pub fn summary(active: bool, kind: SplineKind, points: usize) -> String {
    let what = match kind {
        SplineKind::Road => "road",
        SplineKind::River => "river",
    };
    match (active, points) {
        (true, 0) => format!("drawing a {what}"),
        (true, n) => format!("drawing a {what} · {n} points"),
        (false, 0) => "off".to_owned(),
        (false, n) => format!("{what} · {n} points"),
    }
}

fn card<'a, Message: 'a>(title: &'a str, body: Element<'a, Message>) -> Element<'a, Message> {
    container(column![text(title).size(11).color(theme::TEXT), body].spacing(5))
        .padding([6, 8])
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

fn kind_tone(kind: SplineKind) -> iced::Color {
    match kind {
        SplineKind::Road => theme::ICON_ORANGE,
        SplineKind::River => theme::ICON_CYAN,
    }
}

fn metres(value: f32) -> String {
    if value >= 1000.0 {
        format!("{:.2} km", value / 1000.0)
    } else {
        format!("{value:.0} m")
    }
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let mut content = column![].spacing(6).width(Length::Fill);

    let Some(terrain) = view.terrain else {
        content = content.push(status::note(
            "No terrain in the scene. Add one, then select it: roads and rivers are drawn on a terrain.",
        ));
        return scroll::vertical(container(content).padding(8)).into();
    };
    let road = view.kind == SplineKind::Road;
    let tone = kind_tone(view.kind);
    let word = if road { "road" } else { "river" };

    // The tool and the primary action, first: this is what the user came for.
    let active_emit = emit.clone();
    let toggle: Element<'a, Message> = if view.active {
        button::toolbar(
            id::TPATH_ACTIVE,
            "Stop drawing",
            active_emit(Edit::Active(false)),
        )
    } else {
        button::primary(
            id::TPATH_ACTIVE,
            format!("Draw a {word} on {terrain}"),
            active_emit(Edit::Active(true)),
        )
    };
    content = content.push(
        row![
            status::chip(
                if view.active { "Drawing" } else { "Off" },
                if view.active {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                },
            ),
            toggle,
        ]
        .spacing(8)
        .align_y(iced::Alignment::Center),
    );
    if view.active {
        content = content.push(
            text("Click the ground to drop points. Drag a handle to move it, right-click to remove it.")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    }
    let apply_emit = emit.clone();
    let clear_emit = emit.clone();
    content = content.push(
        row![
            button::primary(
                id::TPATH_APPLY,
                format!("Apply {word}  (Enter)"),
                apply_emit(Edit::Apply),
            ),
            button::compact(id::TPATH_CLEAR, "Clear  (Esc)", clear_emit(Edit::Clear)),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    );
    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }

    // Kind and presets.
    let road_emit = emit.clone();
    let river_emit = emit.clone();
    let mut chips = column![
        text("Type").size(9).color(theme::TEXT_MUTED),
        row![
            toggle_chip::view(
                id::TPATH_KIND_ROAD,
                "Road",
                road,
                kind_tone(SplineKind::Road),
                road_emit(Edit::Kind(SplineKind::Road)),
            ),
            toggle_chip::view(
                id::TPATH_KIND_RIVER,
                "River",
                !road,
                kind_tone(SplineKind::River),
                river_emit(Edit::Kind(SplineKind::River)),
            ),
        ]
        .spacing(4),
        text("Presets").size(9).color(theme::TEXT_MUTED),
    ]
    .spacing(4);
    let mut presets = row![].spacing(4);
    for (index, preset) in PRESETS.iter().enumerate() {
        if preset.kind != view.kind {
            continue;
        }
        let preset_emit = emit.clone();
        presets = presets.push(toggle_chip::view(
            id::tpath_preset(index),
            preset.name,
            view.preset == Some(index),
            kind_tone(preset.kind),
            preset_emit(Edit::Preset(index)),
        ));
    }
    chips = chips.push(presets);
    if let Some(preset) = view.preset.and_then(|i| PRESETS.get(i)) {
        chips = chips.push(text(preset.blurb).size(9).color(theme::TEXT_MUTED));
    }
    content = content.push(card("Kind", chips.into()));

    // The points.
    let mut points = column![].spacing(2);
    if view.points.is_empty() {
        points = points.push(
            text(format!(
                "Click on the terrain to drop the first point of the {word}. Drag a point to move it, right-click to remove it, Backspace drops the last one."
            ))
            .size(9)
            .color(theme::TEXT_MUTED),
        );
    }
    for (index, point) in view.points.iter().enumerate() {
        let select_emit = emit.clone();
        let detail = if point.width_m > 0.0 {
            format!("{:.1} m wide", point.width_m)
        } else {
            String::new()
        };
        points = points.push(palette_row::view(
            id::tpath_point(index),
            index + 1,
            &format!("{:.0}, {:.0}", point.x, point.z),
            [tone.r, tone.g, tone.b],
            &detail,
            view.selected == Some(index),
            select_emit(Edit::SelectPoint(index)),
        ));
    }
    if !view.points.is_empty() {
        let last_emit = emit.clone();
        let reverse_emit = emit.clone();
        let remove_emit = emit.clone();
        let selected = view.selected;
        points = points.push(
            row![
                button::compact(
                    id::TPATH_UNDO_POINT,
                    "Drop last",
                    last_emit(Edit::RemoveLast)
                ),
                button::compact(id::TPATH_REVERSE, "Reverse", reverse_emit(Edit::Reverse)),
                button::compact(
                    id::TPATH_REMOVE_POINT,
                    "Remove selected",
                    remove_emit(selected.map_or(Edit::RemoveLast, Edit::RemovePoint)),
                ),
            ]
            .spacing(4),
        );
    }
    content = content.push(card("Path", points.into()));

    // What it would do: the elevation profile and the numbers.
    if let Some(stats) = view.stats.filter(|_| view.profile.path.len() >= 2) {
        let samples = view.profile.path.len().max(2);
        let lowest = view
            .profile
            .ground
            .iter()
            .chain(&view.profile.path)
            .copied()
            .fold(f32::MAX, f32::min);
        let highest = view
            .profile
            .ground
            .iter()
            .chain(&view.profile.path)
            .copied()
            .fold(f32::MIN, f32::max);
        let pad = ((highest - lowest) * 0.1).max(0.5);
        let chart = line_chart::view::<Message>(
            id::TPATH_PROFILE,
            Chart {
                series: vec![
                    Series {
                        label: "Ground",
                        color: theme::TEXT_MUTED,
                        values: &view.profile.ground,
                    },
                    Series {
                        label: if road { "Road" } else { "River bed" },
                        color: tone,
                        values: &view.profile.path,
                    },
                ],
                window: samples,
                min: lowest - pad,
                max: highest + pad,
                unit: " m",
                height: 86.0,
            },
        );
        let mut numbers = row![
            status::chip(metres(stats.length_m), ChipTone::Neutral),
            status::chip(format!("cut {:.1} m", stats.cut_m), ChipTone::Warning),
            status::chip(format!("fill {:.1} m", stats.fill_m), ChipTone::Neutral),
        ]
        .spacing(4);
        numbers = numbers.push(if road {
            status::chip(
                format!("grade {:.1} %", stats.grade_percent),
                if stats.grade_percent > view.grade_percent + 0.5 {
                    ChipTone::Warning
                } else {
                    ChipTone::Success
                },
            )
        } else {
            status::chip(format!("falls {:.1} m", stats.drop_m), ChipTone::Success)
        });
        content = content.push(card(
            "Elevation",
            column![
                chart,
                numbers,
                text("Grey: the ground now. Coloured: what the path levels or cuts it to.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(5)
            .into(),
        ));
    }

    // The shape.
    let mut shape = column![].spacing(4);
    let mut numbers = vec![
        (Number::Width, view.width_m),
        (Number::Shoulder, view.shoulder_m),
    ];
    if road {
        numbers.push((Number::Smooth, view.smooth_m));
        numbers.push((Number::Grade, view.grade_percent));
    } else {
        numbers.push((Number::Depth, view.depth_m));
        numbers.push((Number::Smooth, view.smooth_m));
    }
    for (number, value) in numbers {
        let number_emit = emit.clone();
        shape = shape.push(slider::property_step(
            number.id(),
            number.label(),
            number.range(),
            value,
            number.step(),
            move |v| number_emit(Edit::Number(number, v)),
        ));
    }
    if let Some(point) = view.selected.and_then(|i| view.points.get(i)) {
        let point_emit = emit.clone();
        shape = shape.push(slider::property_step(
            Number::PointWidth.id(),
            Number::PointWidth.label(),
            Number::PointWidth.range(),
            point.width_m,
            Number::PointWidth.step(),
            move |v| point_emit(Edit::Number(Number::PointWidth, v)),
        ));
    }
    content = content.push(card("Shape", shape.into()));

    // The surface: a layer and a mask.
    let mut surface = column![].spacing(4);
    if view.layer_names.is_empty() {
        surface = surface.push(
            text("Turn on layered painting in the Terrain Paint tab to paint a surface (gravel, asphalt, mud) along the path.")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    } else {
        let mut options = vec!["None".to_owned()];
        options.extend(view.layer_names.iter().cloned());
        let lookup = options.clone();
        let layer_emit = emit.clone();
        let strength_emit = emit.clone();
        surface = surface.push(dropdown::property(
            id::TPATH_LAYER,
            "Paint layer",
            options.clone(),
            Some(
                view.layer
                    .and_then(|i| options.get(i + 1).cloned())
                    .unwrap_or_else(|| "None".to_owned()),
            ),
            move |picked: String| {
                layer_emit(Edit::Layer(
                    lookup
                        .iter()
                        .position(|o| *o == picked)
                        .and_then(|i| i.checked_sub(1)),
                ))
            },
        ));
        if view.layer.is_some() {
            surface = surface.push(slider::property_step(
                Number::LayerStrength.id(),
                Number::LayerStrength.label(),
                Number::LayerStrength.range(),
                view.layer_strength,
                Number::LayerStrength.step(),
                move |v| strength_emit(Edit::Number(Number::LayerStrength, v)),
            ));
        }
    }
    let mask_emit = emit.clone();
    let name_emit = emit.clone();
    surface = surface.push(checkbox::property(
        id::TPATH_WRITE_MASK,
        "Write a mask of the path",
        view.write_mask,
        move |on| mask_emit(Edit::WriteMask(on)),
    ));
    if view.write_mask {
        surface = surface.push(text_field::property(
            id::TPATH_MASK_NAME,
            "Mask",
            view.mask_name,
            move |t| name_emit(Edit::MaskName(t)),
        ));
        surface = surface.push(
            text(if road {
                "Add a rule on this mask to a vegetation species that is 1 only where the mask is 0, and plants stay off the road."
            } else {
                "Add a rule on this mask to a vegetation species, and reeds grow along the banks."
            })
            .size(9)
            .color(theme::TEXT_MUTED),
        );
    }
    content = content.push(card("Surface", surface.into()));

    scroll::vertical(container(content).padding(8)).into()
}

/// A mask name the engine takes: 1 to 31 letters, digits, `_` and `-`.
pub fn mask_name_ok(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 31
        && name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn numbers_are_named_by_key_and_have_sane_ranges() {
        for number in Number::ALL {
            assert_eq!(Number::from_key(number.key()), Some(number));
            let range = number.range();
            assert!(range.start() < range.end(), "{}", number.label());
            assert!(number.step() > 0.0);
        }
        assert_eq!(Number::from_key("nope"), None);
    }

    #[test]
    fn every_preset_fits_the_ranges_and_names_a_mask() {
        for preset in PRESETS {
            assert!(Number::Width.range().contains(&preset.width_m));
            assert!(Number::Shoulder.range().contains(&preset.shoulder_m));
            assert!(Number::Smooth.range().contains(&preset.smooth_m));
            assert!(mask_name_ok(preset.mask));
            if preset.kind == SplineKind::River {
                assert!(preset.depth_m > 0.0, "{}", preset.name);
            } else {
                assert!(Number::Grade.range().contains(&(preset.max_grade * 100.0)));
            }
        }
    }

    #[test]
    fn every_widget_id_is_catalogued() {
        for number in Number::ALL {
            assert!(
                id::description(number.id().key()).is_some(),
                "{}",
                number.key()
            );
        }
        for key in [
            "terrain_path.preset.4",
            "terrain_path.points.row.9",
            "terrain_path.tool.active",
            "terrain_path.apply",
            "terrain_path.clear",
            "terrain_path.kind.road",
            "terrain_path.kind.river",
            "terrain_path.points.undo",
            "terrain_path.points.reverse",
            "terrain_path.points.remove",
            "terrain_path.profile",
            "terrain_path.surface.layer",
            "terrain_path.surface.mask",
            "terrain_path.surface.mask_name",
        ] {
            assert!(id::description(key).is_some(), "{key}");
        }
    }

    #[test]
    fn the_summary_says_what_is_being_drawn() {
        assert_eq!(summary(false, SplineKind::Road, 0), "off");
        assert_eq!(summary(true, SplineKind::River, 0), "drawing a river");
        assert_eq!(
            summary(true, SplineKind::Road, 3),
            "drawing a road · 3 points"
        );
        assert_eq!(summary(false, SplineKind::Road, 2), "road · 2 points");
    }

    #[test]
    fn lengths_read_in_metres_then_kilometres() {
        assert_eq!(metres(216.4), "216 m");
        assert_eq!(metres(1500.0), "1.50 km");
    }
}
