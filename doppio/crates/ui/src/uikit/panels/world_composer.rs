//! World Composer panel: a world made of recipes (terrain, water, materials and classes of things that grow).
//!
//! Left: the **Library**, the project's `*.generator.*` recipes by kind; clicking one adds it to the composition
//! (a terrain becomes the ground, a nature recipe a class, a water recipe a river, a material or texture a
//! binding). Middle: the **Composition**, one row per layer, with each class's variants, priority, a density hint
//! and the coarse preview's count. Right: the **Rules** of the selected class (its `where` terms as sliders, the
//! `adapt` mappings), the seed with Reseed, and the edit layer (remove in a region, pin and move rows).
//!
//! The panel is a pure view: `windows/main/worldgen.rs` owns the composition and handles every `Edit`.

use crate::uikit::components::{
    button, checkbox, dropdown, list_row, scroll, slider, status, text_field, toggle_chip,
    tree_view,
};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::recipe::Kind;
use crate::uikit::theme;
use crate::uikit::world::{self, Composition, OverrideOp};
use iced::widget::{column, container, row, text, Column};
use iced::{Element, Length};
use std::rc::Rc;

/// A numeric property of a class (the `class` line).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClassField {
    Variants,
    Priority,
    Cell,
    Radius,
    Density,
    MinScale,
    MaxScale,
    Tilt,
    Sink,
    Tint,
    Base,
}

impl ClassField {
    pub const ALL: [ClassField; 11] = [
        ClassField::Variants,
        ClassField::Priority,
        ClassField::Cell,
        ClassField::Radius,
        ClassField::Density,
        ClassField::MinScale,
        ClassField::MaxScale,
        ClassField::Tilt,
        ClassField::Sink,
        ClassField::Tint,
        ClassField::Base,
    ];

    /// The word the composition text uses (and the id suffix).
    pub fn key(self) -> &'static str {
        match self {
            ClassField::Variants => "variants",
            ClassField::Priority => "priority",
            ClassField::Cell => "cell",
            ClassField::Radius => "radius",
            ClassField::Density => "density",
            ClassField::MinScale => "scale_min",
            ClassField::MaxScale => "scale_max",
            ClassField::Tilt => "tilt",
            ClassField::Sink => "sink",
            ClassField::Tint => "tint",
            ClassField::Base => "base",
        }
    }

    pub fn from_key(key: &str) -> Option<ClassField> {
        ClassField::ALL.into_iter().find(|field| field.key() == key)
    }

    fn label(self) -> &'static str {
        match self {
            ClassField::Variants => "Variants",
            ClassField::Priority => "Priority",
            ClassField::Cell => "Cell (m)",
            ClassField::Radius => "Radius (m)",
            ClassField::Density => "Density",
            ClassField::MinScale => "Scale min",
            ClassField::MaxScale => "Scale max",
            ClassField::Tilt => "Tilt",
            ClassField::Sink => "Sink (m)",
            ClassField::Tint => "Tint jitter",
            ClassField::Base => "Base",
        }
    }

    /// Range and step of the slider.
    pub fn range(self) -> (f32, f32, f32) {
        match self {
            ClassField::Variants => (1.0, 32.0, 1.0),
            ClassField::Priority => (-100.0, 100.0, 1.0),
            ClassField::Cell => (0.5, 64.0, 0.5),
            ClassField::Radius => (0.0, 20.0, 0.1),
            ClassField::Density => (0.0, 1.0, 0.01),
            ClassField::MinScale | ClassField::MaxScale => (0.1, 4.0, 0.05),
            ClassField::Tilt => (0.0, 1.0, 0.01),
            ClassField::Sink => (0.0, 2.0, 0.05),
            ClassField::Tint => (0.0, 0.5, 0.01),
            ClassField::Base => (0.0, 1.0, 0.01),
        }
    }

    pub fn get(self, class: &world::Class) -> f32 {
        match self {
            ClassField::Variants => class.variants as f32,
            ClassField::Priority => class.priority as f32,
            ClassField::Cell => class.cell,
            ClassField::Radius => class.radius,
            ClassField::Density => class.density,
            ClassField::MinScale => class.min_scale,
            ClassField::MaxScale => class.max_scale,
            ClassField::Tilt => class.tilt,
            ClassField::Sink => class.sink,
            ClassField::Tint => class.tint,
            ClassField::Base => class.base,
        }
    }

    /// Sets the field, clamped to its range (and keeps the scale range from decreasing).
    pub fn set(self, class: &mut world::Class, value: f32) {
        let (min, max, step) = self.range();
        let value = value.clamp(min, max);
        let value = if step >= 1.0 { value.round() } else { value };
        match self {
            ClassField::Variants => class.variants = value as u32,
            ClassField::Priority => class.priority = value as i32,
            ClassField::Cell => class.cell = value,
            ClassField::Radius => class.radius = value,
            ClassField::Density => class.density = value,
            ClassField::MinScale => {
                class.min_scale = value;
                class.max_scale = class.max_scale.max(value);
            }
            ClassField::MaxScale => {
                class.max_scale = value;
                class.min_scale = class.min_scale.min(value);
            }
            ClassField::Tilt => class.tilt = value,
            ClassField::Sink => class.sink = value,
            ClassField::Tint => class.tint = value,
            ClassField::Base => class.base = value,
        }
    }
}

/// A numeric property of a water row.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WaterField {
    /// River threshold over the followed mask.
    Min,
    /// Lake level (normalised height).
    Level,
    Carve,
}

/// A numeric property of an override row.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OverrideField {
    CellX,
    CellZ,
    X,
    Z,
    Variant,
}

/// Which hand edit the Add buttons of the edit layer make.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OverrideKind {
    Pin,
    Move,
}

#[derive(Debug, Clone)]
pub enum Edit {
    Undo,
    Redo,
    New,
    Save,
    SaveAsPrefab,
    Build,
    /// Points the viewport camera at the last build and shows the Viewport.
    Frame,
    Preview,
    SetName(String),
    SetSearch(String),
    /// A library recipe (project path): a terrain becomes the ground, a nature recipe a class, a water recipe a
    /// river, a material or texture a binding.
    Add(String),
    /// Adds a class for a nature recipe, with this id when given (the agent's `world_add_class`).
    AddClass {
        reference: String,
        id: Option<String>,
    },
    SelectClass(usize),
    RemoveClass(usize),
    RemoveWater(usize),
    RemoveMaterial(usize),
    SetWater(usize, WaterField, f32),
    SetMaterialSlot(usize, String),
    /// Axis (0 = x, 1 = z), metres (0 = the terrain recipe's own size).
    SetTerrainSize(usize, f32),
    SetSeed(String),
    Reseed,
    SetClass(usize, ClassField, f32),
    /// `SetClass` for the class with this id (the agent's `world_set_class`).
    SetClassById {
        class: String,
        field: ClassField,
        value: f32,
    },
    SetUnique(usize, bool),
    AddTerm(usize),
    RemoveTerm(usize, usize),
    SetTermMask(usize, usize, String),
    /// Class, term, value index (rise0, rise1, fall1, fall0), value.
    SetTerm(usize, usize, usize, f32),
    /// Sets (or adds) the term over `mask` of a class (the agent's `world_set_rule`).
    SetRule {
        class: String,
        mask: String,
        values: [f32; 4],
    },
    AddAdapt(usize),
    RemoveAdapt(usize, usize),
    SetAdaptTarget(usize, usize, String),
    SetAdaptMask(usize, usize, String),
    SetAdaptAmount(usize, usize, f32),
    /// Index (x0, z0, x1, z1), metres from the terrain's corner.
    SetRegion(usize, f32),
    /// Appends `override remove` for every instance of the last build inside the region, then rebuilds.
    RemoveIn([f32; 4]),
    /// Removes every instance of the last build within `radius` metres of `center` (metres from the terrain's
    /// corner), one undo step, then rebuilds: one stroke of the brush (the agent's `world_remove_in_circle`).
    RemoveInCircle {
        center: [f32; 2],
        radius: f32,
    },
    /// The viewport's Remove instances brush on or off.
    SetBrush(bool),
    SetBrushRadius(f32),
    /// A Library recipe was pressed: dragging it onto the Composition adds it.
    DragStart(String),
    /// The pointer was released over the Composition's drop row.
    Drop,
    AddOverride(usize, OverrideKind),
    RemoveOverride(usize),
    SetOverride(usize, OverrideField, f32),
    Noop,
}

/// One recipe of the project, for the Library.
#[derive(Debug, Clone, PartialEq)]
pub struct LibraryItem {
    pub kind: Kind,
    /// Project path (`Nature/Oak.generator.nature`).
    pub path: String,
}

pub struct View<'a> {
    pub comp: &'a Composition,
    pub name: &'a str,
    pub dirty: bool,
    pub selected: Option<usize>,
    pub library: Vec<LibraryItem>,
    pub search: &'a str,
    pub seed_text: &'a str,
    pub region: [f32; 4],
    /// Terrain size in metres (the region sliders' range).
    pub size: [f32; 2],
    /// Instances per class id of the coarse preview.
    pub preview: Option<&'a [(String, u32)]>,
    pub preview_note: &'a str,
    pub problems: &'a [String],
    pub status: Option<&'a str>,
    pub brush: bool,
    pub brush_radius: f32,
    /// A Library recipe is being dragged.
    pub dragging: bool,
}

fn heading<'a, Message: 'a>(label: &'a str) -> Element<'a, Message> {
    text(label).size(8).color(theme::TEXT_MUTED).into()
}

fn card<'a, Message: 'a>(content: impl Into<Element<'a, Message>>) -> Element<'a, Message> {
    container(content)
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface)
        .into()
}

fn kind_icon(kind: Kind) -> Icon {
    match kind {
        Kind::Terrain | Kind::World => Icon::Terrain,
        Kind::Nature => Icon::Nature,
        Kind::Water => Icon::Water,
        Kind::Material => Icon::Material,
        Kind::Texture => Icon::Texture,
    }
}

fn kind_label(kind: Kind) -> &'static str {
    match kind {
        Kind::Terrain => "TERRAIN",
        Kind::Nature => "NATURE",
        Kind::Water => "WATER",
        Kind::Material => "MATERIAL",
        Kind::Texture => "TEXTURE",
        Kind::World => "WORLD",
    }
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);

    let title = row![
        container(text(" ").size(2))
            .width(Length::Fixed(3.0))
            .height(Length::Fixed(24.0))
            .style(theme::terrain_bar),
        column![
            text("World Composer").size(13).color(theme::TEXT),
            text(format!(
                "{}{} · {} class{} · seed {}",
                view.name,
                if view.dirty { " •" } else { "" },
                view.comp.classes.len(),
                if view.comp.classes.len() == 1 {
                    ""
                } else {
                    "es"
                },
                view.comp.seed
            ))
            .size(9)
            .color(theme::TEXT_MUTED),
        ]
        .spacing(0),
    ]
    .spacing(6)
    .align_y(iced::Alignment::Center);
    let header = container(
        row![
            title,
            iced::widget::space::horizontal(),
            container(text_field::property(id::WORLD_NAME, "Name", view.name, {
                let emit = emit.clone();
                move |value| emit(Edit::SetName(value))
            }))
            .width(Length::Fixed(200.0)),
            button::compact(id::WORLD_UNDO, "↶", emit(Edit::Undo)),
            button::compact(id::WORLD_REDO, "↷", emit(Edit::Redo)),
            button::compact(id::WORLD_NEW, "New", emit(Edit::New)),
            button::compact(id::WORLD_SAVE, "Save", emit(Edit::Save)),
            button::compact(id::WORLD_PREVIEW, "Preview", emit(Edit::Preview)),
            button::compact(
                id::WORLD_SAVE_PREFAB,
                "Save as prefab",
                emit(Edit::SaveAsPrefab)
            ),
            button::primary(id::WORLD_BUILD, "Build", emit(Edit::Build)),
            button::compact(id::WORLD_FRAME, "Frame", emit(Edit::Frame)),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center)
        .wrap(),
    )
    .padding([4, 7])
    .style(theme::panel_title);

    let body = row![
        container(scroll::vertical(library(&view, emit.clone()))).width(Length::Fixed(190.0)),
        container(scroll::vertical(composition(&view, emit.clone())))
            .width(Length::Fill)
            .height(Length::Fill),
        container(scroll::vertical(rules(&view, emit.clone()))).width(Length::Fixed(300.0)),
    ]
    .spacing(6)
    .height(Length::Fill);

    let mut footer = Column::new().spacing(2);
    if view.problems.is_empty() {
        footer = footer.push(status::chip(
            "The composition is valid".to_owned(),
            theme::ChipTone::Success,
        ));
    } else {
        for problem in view.problems.iter().take(4) {
            footer = footer.push(status::chip(problem.clone(), theme::ChipTone::Danger));
        }
        if view.problems.len() > 4 {
            footer = footer.push(
                text(format!("… and {} more", view.problems.len() - 4))
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
        }
    }
    if let Some(note) = view.status {
        footer = footer.push(status::note(note));
    }
    let footer = container(footer).id(id::WORLD_STATUS).padding([3, 7]);

    column![header, container(body).height(Length::Fill), footer]
        .spacing(0)
        .into()
}

fn library<'a, Message: Clone + 'a>(
    view: &View<'a>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let mut column = Column::new().spacing(3);
    column = column.push(heading("LIBRARY"));
    column = column.push(text_field::search(
        id::WORLD_LIBRARY_SEARCH,
        "Search recipes…",
        view.search,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetSearch(value))
        },
    ));
    let needle = view.search.trim().to_lowercase();
    let mut any = false;
    for kind in [
        Kind::Terrain,
        Kind::Nature,
        Kind::Water,
        Kind::Material,
        Kind::Texture,
    ] {
        let items: Vec<(usize, &LibraryItem)> = view
            .library
            .iter()
            .enumerate()
            .filter(|(_, item)| item.kind == kind)
            .filter(|(_, item)| needle.is_empty() || item.path.to_lowercase().contains(&needle))
            .collect();
        if items.is_empty() {
            continue;
        }
        any = true;
        column = column.push(heading(kind_label(kind)));
        let mut rows = Vec::new();
        for (index, item) in items {
            let name = item
                .path
                .rsplit('/')
                .next()
                .unwrap_or(&item.path)
                .split(".generator")
                .next()
                .unwrap_or(&item.path)
                .to_owned();
            rows.push(tree_view::Item {
                id: id::world_library_item(index),
                label: format!("+ {name}"),
                icon: kind_icon(kind),
                depth: 0,
                has_children: false,
                expanded: false,
                selected: false,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: emit(Edit::Noop),
                // Press starts a drag; releasing on the same row is a click (add), on the Composition a drop.
                on_press: emit(Edit::DragStart(item.path.clone())),
                on_release: emit(Edit::Add(item.path.clone())),
                on_enter: emit(Edit::Noop),
                on_exit: emit(Edit::Noop),
                on_context: emit(Edit::Add(item.path.clone())),
            });
        }
        column = column.push(tree_view::view(rows));
    }
    if !any {
        column = column.push(
            text("No recipes yet: bake a terrain, a tree or a water body in its generator and its .generator.* file shows up here.")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    }
    column = column.push(
        text("Click a recipe, or drag it onto the Composition, to add it: a terrain becomes the ground, a nature recipe a class, water a river, a material a binding.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );
    column.into()
}

fn composition<'a, Message: Clone + 'a>(
    view: &View<'a>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let comp = view.comp;
    let mut column = Column::new().spacing(5);

    // Where a dragged Library recipe is dropped.
    column = column.push(tree_view::view(vec![tree_view::Item {
        id: id::WORLD_DROP,
        label: if view.dragging {
            "Release here to add the recipe".to_owned()
        } else {
            "Drag a recipe from the Library here".to_owned()
        },
        icon: Icon::Folder,
        depth: 0,
        has_children: false,
        expanded: false,
        selected: false,
        hovered: false,
        drop_target: view.dragging,
        trailing: None,
        extra: None,
        on_toggle: emit(Edit::Noop),
        on_press: emit(Edit::Noop),
        on_release: emit(Edit::Drop),
        on_enter: emit(Edit::Noop),
        on_exit: emit(Edit::Noop),
        on_context: emit(Edit::Noop),
    }]));

    // Terrain.
    let mut terrain = Column::new().spacing(3);
    terrain = terrain.push(heading("TERRAIN"));
    terrain = terrain.push(
        container(
            text(if comp.terrain.is_empty() {
                "No terrain: click a terrain recipe in the Library".to_owned()
            } else {
                comp.terrain.clone()
            })
            .size(11)
            .color(if comp.terrain.is_empty() {
                theme::WARNING
            } else {
                theme::TEXT
            }),
        )
        .id(id::WORLD_TERRAIN),
    );
    for axis in 0..2 {
        let emit = emit.clone();
        terrain = terrain.push(slider::property_step(
            if axis == 0 {
                id::WORLD_TERRAIN_SIZE_X
            } else {
                id::WORLD_TERRAIN_SIZE_Z
            },
            if axis == 0 {
                "Size X (m)"
            } else {
                "Size Z (m)"
            },
            0.0..=4096.0,
            comp.size[axis],
            16.0,
            move |value| emit(Edit::SetTerrainSize(axis, value)),
        ));
    }
    column = column.push(card(terrain));

    // Water.
    if !comp.water.is_empty() {
        let mut water = Column::new().spacing(3);
        water = water.push(heading("WATER"));
        for (index, body) in comp.water.iter().enumerate() {
            let kind = if body.fill_basins { "lake" } else { "river" };
            water = water.push(
                row![
                    container(
                        text(format!("{kind} · {}", body.reference))
                            .size(11)
                            .color(theme::TEXT)
                    )
                    .id(id::world_water_row(index))
                    .width(Length::Fill),
                    button::compact(
                        id::world_water_remove(index),
                        "×",
                        emit(Edit::RemoveWater(index))
                    ),
                ]
                .align_y(iced::Alignment::Center),
            );
            let (field, label, range, value) = if body.fill_basins {
                (WaterField::Level, "Level", 0.0..=1.0, body.level)
            } else {
                (WaterField::Min, "Flow above", 0.0..=1.0, body.min)
            };
            let level_emit = emit.clone();
            water = water.push(slider::property_step(
                id::world_water_value(index, if body.fill_basins { "level" } else { "min" }),
                label,
                range,
                value,
                0.01,
                move |v| level_emit(Edit::SetWater(index, field, v)),
            ));
            let carve_emit = emit.clone();
            water = water.push(slider::property_step(
                id::world_water_value(index, "carve"),
                "Carve (m)",
                0.0..=10.0,
                body.carve,
                0.1,
                move |v| carve_emit(Edit::SetWater(index, WaterField::Carve, v)),
            ));
        }
        column = column.push(card(water));
    }

    // Materials.
    if !comp.materials.is_empty() {
        let mut materials = Column::new().spacing(3);
        materials = materials.push(heading("MATERIALS"));
        for (index, binding) in comp.materials.iter().enumerate() {
            let slot_emit = emit.clone();
            materials = materials.push(
                row![
                    container(text_field::property(
                        id::world_material_slot(index),
                        "Slot",
                        &binding.slot,
                        move |value| slot_emit(Edit::SetMaterialSlot(index, value)),
                    ))
                    .width(Length::FillPortion(2)),
                    text(binding.reference.clone())
                        .size(10)
                        .color(theme::TEXT_MUTED)
                        .width(Length::FillPortion(3)),
                    button::compact(
                        id::world_material_remove(index),
                        "×",
                        emit(Edit::RemoveMaterial(index))
                    ),
                ]
                .spacing(4)
                .align_y(iced::Alignment::Center),
            );
        }
        column = column.push(card(materials));
    }

    // Classes.
    let mut classes = Column::new().spacing(3);
    classes = classes.push(heading("CLASSES (BY PRIORITY: HIGHER PLACES FIRST)"));
    if comp.classes.is_empty() {
        classes = classes.push(
            text("No classes: click a nature recipe in the Library.")
                .size(10)
                .color(theme::TEXT_MUTED),
        );
    }
    let mut order: Vec<usize> = (0..comp.classes.len()).collect();
    order.sort_by_key(|&i| std::cmp::Reverse(comp.classes[i].priority));
    for index in order {
        let class = &comp.classes[index];
        let count = view
            .preview
            .and_then(|counts| counts.iter().find(|(id, _)| *id == class.id))
            .map(|(_, n)| format!(" · preview {n}"))
            .unwrap_or_default();
        let label = format!(
            "{} · {} · {} variant{} · priority {} · ≈{:.0}/ha{}{}",
            class.id,
            class.reference,
            class.variants,
            if class.variants == 1 { "" } else { "s" },
            class.priority,
            class.density_hint(),
            if class.unique { " · unique" } else { "" },
            count
        );
        classes = classes.push(
            row![
                container(list_row::view(
                    id::world_class_row(index),
                    label,
                    view.selected == Some(index),
                    emit(Edit::SelectClass(index)),
                    emit(Edit::SelectClass(index)),
                    emit(Edit::SelectClass(index)),
                ))
                .width(Length::Fill),
                button::compact(
                    id::world_class_remove(index),
                    "×",
                    emit(Edit::RemoveClass(index))
                ),
            ]
            .spacing(3)
            .align_y(iced::Alignment::Center),
        );
    }
    column = column.push(card(classes));

    // Preview.
    let mut preview = Column::new().spacing(2);
    preview = preview.push(heading("PREVIEW"));
    preview = preview.push(
        container(
            text(view.preview_note.to_owned())
                .size(10)
                .color(theme::TEXT_MUTED),
        )
        .id(id::WORLD_PREVIEW_NOTE),
    );
    column = column.push(card(preview));
    column.into()
}

fn rules<'a, Message: Clone + 'a>(
    view: &View<'a>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let comp = view.comp;
    let mut column = Column::new().spacing(5);

    // Seed.
    column = column.push(card(
        column![
            heading("SEED"),
            row![
                container(text_field::property(
                    id::WORLD_SEED,
                    "Seed",
                    view.seed_text,
                    {
                        let emit = emit.clone();
                        move |value| emit(Edit::SetSeed(value))
                    }
                ))
                .width(Length::Fill),
                button::compact(id::WORLD_RESEED, "Reseed", emit(Edit::Reseed)),
            ]
            .spacing(4)
            .align_y(iced::Alignment::Center),
        ]
        .spacing(3),
    ));

    // The selected class.
    let mut class_card = Column::new().spacing(3);
    match view
        .selected
        .and_then(|i| comp.classes.get(i).map(|c| (i, c)))
    {
        None => {
            class_card = class_card.push(heading("RULES"));
            class_card = class_card.push(
                text("Select a class to edit where it grows.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
            );
        }
        Some((index, class)) => {
            class_card = class_card.push(heading("RULES"));
            class_card = class_card.push(text(class.id.clone()).size(12).color(theme::TEXT));
            for field in ClassField::ALL {
                let (min, max, step) = field.range();
                let emit = emit.clone();
                class_card = class_card.push(slider::property_step(
                    id::world_class_field(field.key()),
                    field.label(),
                    min..=max,
                    field.get(class),
                    step,
                    move |value| emit(Edit::SetClass(index, field, value)),
                ));
            }
            class_card = class_card.push(checkbox::property(
                id::WORLD_UNIQUE,
                "Unique (bake per instance)",
                class.unique,
                {
                    let emit = emit.clone();
                    move |on| emit(Edit::SetUnique(index, on))
                },
            ));
            class_card = class_card.push(heading("WHERE (TERMS OVER MASKS)"));
            let masks: Vec<String> = world::MASKS.iter().map(|m| (*m).to_owned()).collect();
            for (term_index, term) in class.terms.iter().enumerate() {
                let mask_emit = emit.clone();
                let mut options = masks.clone();
                if !options.contains(&term.mask) {
                    options.push(term.mask.clone());
                }
                class_card = class_card.push(
                    row![
                        container(dropdown::property(
                            id::world_term(term_index, "mask"),
                            "Mask",
                            options,
                            Some(term.mask.clone()),
                            move |mask| mask_emit(Edit::SetTermMask(index, term_index, mask)),
                        ))
                        .width(Length::Fill),
                        button::compact(
                            id::world_term(term_index, "remove"),
                            "×",
                            emit(Edit::RemoveTerm(index, term_index))
                        ),
                    ]
                    .spacing(3)
                    .align_y(iced::Alignment::Center),
                );
                for (value_index, label) in ["Rise from", "Full at", "Full until", "Gone at"]
                    .into_iter()
                    .enumerate()
                {
                    let emit = emit.clone();
                    class_card = class_card.push(slider::property_step(
                        id::world_term(
                            term_index,
                            ["rise0", "rise1", "fall1", "fall0"][value_index],
                        ),
                        label,
                        0.0..=1.0,
                        term.values[value_index],
                        0.01,
                        move |value| emit(Edit::SetTerm(index, term_index, value_index, value)),
                    ));
                }
            }
            class_card = class_card.push(button::compact(
                id::WORLD_ADD_TERM,
                "+ Term",
                emit(Edit::AddTerm(index)),
            ));
            class_card = class_card.push(heading("ADAPT (GROUND CONTEXT → INSTANCE)"));
            let sources = world::adapt_sources();
            for (adapt_index, adapt) in class.adapt.iter().enumerate() {
                let mut targets: Vec<String> = world::ADAPT_TARGETS
                    .iter()
                    .map(|t| (*t).to_owned())
                    .collect();
                if !targets.contains(&adapt.target) {
                    targets.push(adapt.target.clone());
                }
                let mut from = sources.clone();
                if !from.contains(&adapt.mask) {
                    from.push(adapt.mask.clone());
                }
                let (target_emit, mask_emit, amount_emit) =
                    (emit.clone(), emit.clone(), emit.clone());
                class_card = class_card.push(
                    row![
                        container(dropdown::property(
                            id::world_adapt(adapt_index, "target"),
                            "Drive",
                            targets,
                            Some(adapt.target.clone()),
                            move |target| {
                                target_emit(Edit::SetAdaptTarget(index, adapt_index, target))
                            },
                        ))
                        .width(Length::Fill),
                        container(dropdown::property(
                            id::world_adapt(adapt_index, "mask"),
                            "by",
                            from,
                            Some(adapt.mask.clone()),
                            move |mask| mask_emit(Edit::SetAdaptMask(index, adapt_index, mask)),
                        ))
                        .width(Length::Fill),
                        button::compact(
                            id::world_adapt(adapt_index, "remove"),
                            "×",
                            emit(Edit::RemoveAdapt(index, adapt_index))
                        ),
                    ]
                    .spacing(3)
                    .align_y(iced::Alignment::Center),
                );
                class_card = class_card.push(slider::property_step(
                    id::world_adapt(adapt_index, "amount"),
                    "Amount",
                    0.0..=2.0,
                    adapt.effective_amount(),
                    0.01,
                    move |value| amount_emit(Edit::SetAdaptAmount(index, adapt_index, value)),
                ));
            }
            class_card = class_card.push(button::compact(
                id::WORLD_ADD_ADAPT,
                "+ Adapt",
                emit(Edit::AddAdapt(index)),
            ));
            class_card = class_card.push(
                row![
                    button::compact(
                        id::WORLD_ADD_PIN,
                        "+ Pin",
                        emit(Edit::AddOverride(index, OverrideKind::Pin))
                    ),
                    button::compact(
                        id::WORLD_ADD_MOVE,
                        "+ Move",
                        emit(Edit::AddOverride(index, OverrideKind::Move))
                    ),
                ]
                .spacing(4),
            );
        }
    }
    column = column.push(card(class_card));

    // The edit layer.
    let mut edits = Column::new().spacing(3);
    edits = edits.push(heading("EDIT LAYER"));
    for (axis, label) in ["Region x0", "Region z0", "Region x1", "Region z1"]
        .into_iter()
        .enumerate()
    {
        let emit = emit.clone();
        let max = view.size[axis % 2].max(1.0);
        edits = edits.push(slider::property_step(
            id::world_region(axis),
            label,
            0.0..=max,
            view.region[axis].clamp(0.0, max),
            0.5,
            move |value| emit(Edit::SetRegion(axis, value)),
        ));
    }
    edits = edits.push(toggle_chip::view(
        id::WORLD_BRUSH,
        if view.brush {
            "Remove brush: on (drag in the Viewport)"
        } else {
            "Remove brush: off"
        },
        view.brush,
        theme::DANGER,
        emit(Edit::SetBrush(!view.brush)),
    ));
    edits = edits.push(slider::property_step(
        id::WORLD_BRUSH_RADIUS,
        "Brush radius (m)",
        0.5..=50.0,
        view.brush_radius,
        0.5,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetBrushRadius(value))
        },
    ));
    edits = edits.push(button::block(
        id::WORLD_REMOVE_IN_REGION,
        "Remove in region",
        emit(Edit::RemoveIn(view.region)),
    ));
    for (index, edit) in comp.overrides.iter().enumerate() {
        edits = edits.push(
            row![
                container(
                    text(format!(
                        "{} {} {} {}",
                        edit.op.name(),
                        edit.class,
                        edit.cell[0],
                        edit.cell[1]
                    ))
                    .size(10)
                    .color(theme::TEXT)
                )
                .id(id::world_override(index, "row"))
                .width(Length::Fill),
                button::compact(
                    id::world_override(index, "remove"),
                    "×",
                    emit(Edit::RemoveOverride(index))
                ),
            ]
            .align_y(iced::Alignment::Center),
        );
        if matches!(edit.op, OverrideOp::Remove) {
            continue;
        }
        let cell = comp.class(&edit.class).map_or(8.0, |c| c.cell.max(0.5));
        let cells = (view.size[0].max(view.size[1]) / cell).ceil().max(1.0) + 4.0;
        for (field, label, axis) in [
            (OverrideField::CellX, "Cell x", 0),
            (OverrideField::CellZ, "Cell z", 1),
        ] {
            let emit = emit.clone();
            edits = edits.push(slider::property_step(
                id::world_override(index, if axis == 0 { "cell_x" } else { "cell_z" }),
                label,
                -4.0..=cells,
                edit.cell[axis] as f32,
                1.0,
                move |value| emit(Edit::SetOverride(index, field, value)),
            ));
        }
        match edit.op {
            OverrideOp::Move { x, z } => {
                for (field, label, value, axis) in [
                    (OverrideField::X, "To x (m)", x, 0),
                    (OverrideField::Z, "To z (m)", z, 1),
                ] {
                    let emit = emit.clone();
                    let max = view.size[axis].max(1.0);
                    edits = edits.push(slider::property_step(
                        id::world_override(index, if axis == 0 { "x" } else { "z" }),
                        label,
                        0.0..=max,
                        value.clamp(0.0, max),
                        0.25,
                        move |v| emit(Edit::SetOverride(index, field, v)),
                    ));
                }
            }
            OverrideOp::Pin { variant } => {
                let variants = comp.class(&edit.class).map_or(1, |c| c.variants.max(1));
                let emit = emit.clone();
                edits = edits.push(slider::property_step(
                    id::world_override(index, "variant"),
                    "Variant",
                    0.0..=(variants - 1).max(1) as f32,
                    variant as f32,
                    1.0,
                    move |v| emit(Edit::SetOverride(index, OverrideField::Variant, v)),
                ));
            }
            OverrideOp::Remove => {}
        }
    }
    edits = edits.push(
        text("Hand edits are kept as `override` lines and applied after generation, so the rest of the world never moves.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );
    column = column.push(card(edits));
    column.into()
}
