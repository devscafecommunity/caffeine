//! Texture Generator panel: a node graph that bakes **one texture** (a leaf, a flame, a packed
//! VFX mask...) with alpha, as opposed to the Material Generator which bakes a PBR material.
//!
//! The two tools share the node canvas, the node-properties editor and the procedural library,
//! but they are separate panes with their own title, accent colour, palette, presets, preview
//! (over a checkerboard) and export (a single PNG or a flipbook sheet).

use crate::uikit::components::{
    button, checkbox, dropdown, node_canvas, text_field, toggle_chip, tree_view,
};
use crate::uikit::generators::{MaterialGraph, NodeId, NodeKind};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::panels::material_generator as nodes_panel;
use crate::uikit::texture_graph;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{column, container, image, row, text, Column};
use iced::{ContentFit, Element, Length, Vector};
use std::collections::{HashMap, HashSet};
use std::rc::Rc;

/// What the preview shows.
pub const PREVIEW_MODES: [&str; 6] = ["Over checker", "Colour", "Alpha", "R", "G", "B"];

#[derive(Debug, Clone)]
pub enum Edit {
    /// Canvas and node edits shared with the Material Generator (select, move, connect,
    /// parameters, add/delete, undo, resolution...).
    Node(nodes_panel::Edit),
    New,
    Save,
    /// Bake and write one PNG.
    Export,
    /// Bake a flipbook sheet.
    ExportFlipbook,
    /// Index into `texture_graph::presets()`.
    ApplyPreset(usize),
    SetBits(u8),
    SetName(String),
    SetFlipbook(u32, u32),
    SetPreviewMode(usize),
}

pub struct View<'a> {
    pub graph: &'a MaterialGraph,
    pub name: &'a str,
    pub selected: Option<NodeId>,
    pub selected_link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub preview: Option<&'a Handle>,
    pub preview_mode: usize,
    pub node_previews: &'a HashMap<NodeId, Handle>,
    pub status: Option<&'a str>,
    pub dirty: bool,
    pub can_undo: bool,
    pub can_redo: bool,
    pub snap: bool,
    pub search: &'a str,
    pub collapsed_groups: &'a HashSet<String>,
    pub bits: u8,
    pub flip_columns: u32,
    pub flip_rows: u32,
}

fn choices(values: &[u32]) -> Vec<String> {
    values.iter().map(|value| value.to_string()).collect()
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let node_emit: Rc<dyn Fn(nodes_panel::Edit) -> Message + 'a> = {
        let emit = emit.clone();
        Rc::new(move |edit| emit(Edit::Node(edit)))
    };
    let graph = view.graph;

    // ---- header
    let sizes = [128u32, 256, 512, 1024, 2048];
    let size_lookup = choices(&sizes);
    let size_emit = node_emit.clone();
    let size = dropdown::property(
        id::TEXTUREGEN_SIZE,
        "Size",
        size_lookup.clone(),
        Some(graph.resolution.to_string()),
        move |choice| {
            let value = size_lookup
                .iter()
                .position(|label| *label == choice)
                .map(|index| sizes[index])
                .unwrap_or(512);
            size_emit(nodes_panel::Edit::SetResolution(value))
        },
    );
    let library_options: Vec<String> = texture_graph::presets()
        .iter()
        .map(|preset| format!("{} · {}", preset.category, preset.name))
        .collect();
    let library_lookup = library_options.clone();
    let library_emit = emit.clone();
    let library = dropdown::property(
        id::TEXTUREGEN_LIBRARY,
        "Library",
        library_options,
        None,
        move |choice| {
            let index = library_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            library_emit(Edit::ApplyPreset(index))
        },
    );
    let title = container(
        row![
            container(text(" ").size(2))
                .width(Length::Fixed(3.0))
                .height(Length::Fixed(24.0))
                .style(theme::accent_bar),
            column![
                text("Texture Generator").size(13).color(theme::TEXT),
                text(format!(
                    "{}{} · {}×{}",
                    view.name,
                    if view.dirty { " •" } else { "" },
                    graph.resolution,
                    graph.resolution
                ))
                .size(9)
                .color(theme::TEXT_MUTED),
            ]
            .spacing(0),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    );
    let header = container(
        row![
            title,
            iced::widget::space::horizontal(),
            container(library).width(Length::Fixed(230.0)),
            button::compact(id::TEXTUREGEN_NEW, "New", emit(Edit::New)),
            button::compact(
                id::TEXTUREGEN_UNDO,
                "↶",
                emit(Edit::Node(nodes_panel::Edit::Undo))
            ),
            button::compact(
                id::TEXTUREGEN_REDO,
                "↷",
                emit(Edit::Node(nodes_panel::Edit::Redo))
            ),
            button::compact(
                id::TEXTUREGEN_FIT,
                "Fit",
                emit(Edit::Node(nodes_panel::Edit::Fit))
            ),
            button::compact(
                id::TEXTUREGEN_RESEED,
                "Reseed",
                emit(Edit::Node(nodes_panel::Edit::Reseed))
            ),
            container(size).width(Length::Fixed(138.0)),
            button::compact(id::TEXTUREGEN_SAVE, "Save", emit(Edit::Save)),
            button::primary(id::TEXTUREGEN_EXPORT, "Export PNG", emit(Edit::Export)),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center)
        .wrap(),
    )
    .padding([4, 7])
    .style(theme::panel_title);

    // ---- palette
    let mut toolbox = Column::new().spacing(3);
    toolbox = toolbox.push(text_field::search(
        id::TEXTUREGEN_SEARCH,
        "Search nodes…",
        view.search,
        {
            let node_emit = node_emit.clone();
            move |value| node_emit(nodes_panel::Edit::SetSearch(value))
        },
    ));
    toolbox = toolbox.push(text("ADD NODE").size(8).color(theme::TEXT_MUTED));
    let needle = view.search.trim().to_lowercase();
    let mut grouped: Vec<(&str, Vec<NodeKind>)> = Vec::new();
    for kind in texture_graph::palette() {
        let title = kind.title().to_owned();
        let category = kind.category();
        if !needle.is_empty()
            && !title.to_lowercase().contains(&needle)
            && !category.to_lowercase().contains(&needle)
        {
            continue;
        }
        if let Some((_, kinds)) = grouped.iter_mut().find(|(name, _)| *name == category) {
            kinds.push(kind);
        } else {
            grouped.push((category, vec![kind]));
        }
    }
    let mut tree_items = Vec::new();
    for (category, kinds) in grouped {
        let collapsed = needle.is_empty() && view.collapsed_groups.contains(category);
        let toggle = node_emit.clone();
        let name = category.to_owned();
        tree_items.push(tree_view::Item {
            id: id::texturegen_group(category),
            label: format!("{category} ({})", kinds.len()),
            icon: Icon::Folder,
            depth: 0,
            has_children: true,
            expanded: !collapsed,
            selected: false,
            hovered: false,
            drop_target: false,
            trailing: None,
            extra: None,
            on_toggle: toggle(nodes_panel::Edit::TogglePaletteGroup(name.clone())),
            on_press: toggle(nodes_panel::Edit::TogglePaletteGroup(name.clone())),
            on_release: toggle(nodes_panel::Edit::Noop),
            on_enter: toggle(nodes_panel::Edit::Noop),
            on_exit: toggle(nodes_panel::Edit::Noop),
            on_context: toggle(nodes_panel::Edit::TogglePaletteGroup(name)),
        });
        if collapsed {
            continue;
        }
        for kind in kinds {
            let title = kind.title().to_owned();
            let slug = title.to_lowercase().replace(' ', "_");
            let icon = match category {
                texture_graph::VFX_CATEGORY => Icon::Light,
                "Noise" | "Natural" | "Realistic" => Icon::Terrain,
                "Patterns" | "Shapes" => Icon::Texture,
                _ => Icon::Script,
            };
            tree_items.push(tree_view::Item {
                id: id::texturegen_add(&slug),
                label: title,
                icon,
                depth: 1,
                has_children: false,
                expanded: false,
                selected: false,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: node_emit(nodes_panel::Edit::Noop),
                on_press: node_emit(nodes_panel::Edit::AddNode(kind)),
                on_release: node_emit(nodes_panel::Edit::Noop),
                on_enter: node_emit(nodes_panel::Edit::Noop),
                on_exit: node_emit(nodes_panel::Edit::Noop),
                on_context: node_emit(nodes_panel::Edit::Noop),
            });
        }
    }
    if !tree_items.is_empty() {
        toolbox = toolbox.push(tree_view::view(tree_items));
    }
    toolbox = toolbox.push(checkbox::property(
        id::TEXTUREGEN_SNAP,
        "Snap 16",
        view.snap,
        {
            let node_emit = node_emit.clone();
            move |value| node_emit(nodes_panel::Edit::SetSnap(value))
        },
    ));
    toolbox = toolbox.push(
        text("Objects sit inside the image with a transparent margin. Wire a mask into the Alpha port.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );

    // ---- canvas
    let canvas_emit = node_emit.clone();
    let canvas = node_canvas::view(
        id::TEXTUREGEN_CANVAS,
        node_canvas::View {
            nodes: graph.canvas_nodes(view.selected, view.node_previews),
            links: &graph.links,
            pan: view.pan,
            zoom: view.zoom,
            selected_link: view.selected_link,
            snap: view.snap.then_some(16.0),
        },
        move |event| match event {
            node_canvas::Event::Select(selected) => {
                canvas_emit(nodes_panel::Edit::Select(selected))
            }
            node_canvas::Event::SelectLink(link) => {
                canvas_emit(nodes_panel::Edit::SelectLink(link))
            }
            node_canvas::Event::Detach(link) => {
                canvas_emit(nodes_panel::Edit::DetachLink(link.0, link.1))
            }
            node_canvas::Event::InsertDot(link, position) => {
                canvas_emit(nodes_panel::Edit::InsertDot(link.0, link.1, position))
            }
            node_canvas::Event::MoveNode(id, delta) => {
                canvas_emit(nodes_panel::Edit::MoveNode(id, delta))
            }
            node_canvas::Event::Connect {
                from,
                output,
                to,
                input,
            } => canvas_emit(nodes_panel::Edit::Connect {
                from,
                output,
                to,
                input,
            }),
            node_canvas::Event::Viewport { pan, zoom } => {
                canvas_emit(nodes_panel::Edit::Viewport { pan, zoom })
            }
        },
    );

    // ---- right column: properties, preview, export
    let mut right = Column::new().spacing(6);
    right = right.push(nodes_panel::properties_panel(
        graph,
        view.selected,
        view.selected_link,
        node_emit.clone(),
    ));

    let preview_body: Element<'a, Message> = if let Some(handle) = view.preview {
        image(handle.clone())
            .width(Length::Fill)
            .height(Length::Fixed(190.0))
            .content_fit(ContentFit::Contain)
            .into()
    } else {
        container(text("Nothing to preview").size(9).color(theme::TEXT_MUTED))
            .width(Length::Fill)
            .height(Length::Fixed(72.0))
            .center_x(Length::Fill)
            .center_y(Length::Fixed(72.0))
            .into()
    };
    let mut chips = Vec::new();
    for (index, label) in PREVIEW_MODES.iter().enumerate() {
        chips.push(toggle_chip::view(
            id::texturegen_mode(label),
            *label,
            view.preview_mode == index,
            theme::ICON_ORANGE,
            emit(Edit::SetPreviewMode(index)),
        ));
    }
    right = right.push(
        container(
            column![
                text("TEXTURE PREVIEW").size(8).color(theme::TEXT_MUTED),
                iced::widget::Row::with_children(chips)
                    .spacing(3)
                    .wrap()
                    .vertical_spacing(3),
                container(preview_body).id(id::TEXTUREGEN_PREVIEW),
            ]
            .spacing(3),
        )
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface),
    );

    let mut export = Column::new().spacing(3);
    export = export.push(text("EXPORT").size(8).color(theme::TEXT_MUTED));
    export = export.push(text_field::property(
        id::TEXTUREGEN_NAME,
        "Name",
        view.name,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetName(value))
        },
    ));
    let bits_options = vec!["8-bit".to_owned(), "16-bit".to_owned()];
    let bits_lookup = bits_options.clone();
    let bits_emit = emit.clone();
    export = export.push(dropdown::property(
        id::TEXTUREGEN_BITS,
        "Bit depth",
        bits_options,
        Some(bits_lookup[usize::from(view.bits == 16)].clone()),
        move |choice| bits_emit(Edit::SetBits(if choice == bits_lookup[1] { 16 } else { 8 })),
    ));
    export = export.push(text("FLIPBOOK").size(8).color(theme::TEXT_MUTED));
    let grid = [1u32, 2, 4, 6, 8];
    let grid_labels = choices(&grid);
    let (cols, rows) = (view.flip_columns, view.flip_rows);
    let cols_emit = emit.clone();
    let cols_lookup = grid_labels.clone();
    export = export.push(dropdown::property(
        id::TEXTUREGEN_FLIP_COLS,
        "Columns",
        grid_labels.clone(),
        Some(cols.to_string()),
        move |choice| {
            let value = cols_lookup
                .iter()
                .position(|label| *label == choice)
                .map(|index| grid[index])
                .unwrap_or(4);
            cols_emit(Edit::SetFlipbook(value, rows))
        },
    ));
    let rows_emit = emit.clone();
    let rows_lookup = grid_labels.clone();
    export = export.push(dropdown::property(
        id::TEXTUREGEN_FLIP_ROWS,
        "Rows",
        grid_labels,
        Some(rows.to_string()),
        move |choice| {
            let value = rows_lookup
                .iter()
                .position(|label| *label == choice)
                .map(|index| grid[index])
                .unwrap_or(4);
            rows_emit(Edit::SetFlipbook(cols, value))
        },
    ));
    export = export.push(button::compact(
        id::TEXTUREGEN_EXPORT_FLIPBOOK,
        "Export flipbook",
        emit(Edit::ExportFlipbook),
    ));
    right = right.push(
        container(export)
            .width(Length::Fill)
            .padding(4)
            .style(theme::surface),
    );
    if let Some(status) = view.status {
        right = right.push(
            container(text(status).size(9).color(theme::WARNING))
                .width(Length::Fill)
                .padding(3),
        );
    }

    let body = row![
        container(crate::uikit::components::scroll::vertical(toolbox)).width(Length::Fixed(150.0)),
        container(canvas)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface),
        container(crate::uikit::components::scroll::vertical(right)).width(Length::Fixed(250.0)),
    ]
    .spacing(6)
    .height(Length::Fill);

    column![header, container(body).height(Length::Fill)]
        .spacing(0)
        .into()
}
