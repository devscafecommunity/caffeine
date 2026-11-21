//! Nature Generator panel: a node graph for geometry, with a live 3D preview.
//!
//! Left: the node palette by category. Middle: the node canvas. Right: the selected node's
//! parameters (sliders and dropdowns generated from each node's table row), the preview (drag to
//! orbit) and the export / placement controls. The panel is a pure view: the app
//! (`windows/main/naturegen.rs`) owns the state and handles the messages.
//!
//! Parts take their material from the library, from the Texture Generator, or from **your own
//! project assets**: the Part node lists the project's `.mat` files and images.

use crate::uikit::components::{
    button, checkbox, dropdown, material_preview, node_canvas, scroll, slider, status, text_field,
    texture_slot, tree_view,
};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::nature::graph::{Kind, NatureGraph, NodeId};
use crate::uikit::nature::{ops, Quality};
use crate::uikit::procedural::ParamKind;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{column, container, row, text, Column};
use iced::{Element, Length, Point, Vector};
use std::collections::HashSet;
use std::rc::Rc;

#[derive(Debug, Clone)]
pub enum Edit {
    Select(Option<NodeId>),
    SelectLink(Option<(NodeId, usize)>),
    DeleteLink(NodeId, usize),
    DetachLink(NodeId, usize),
    Connect {
        from: NodeId,
        output: usize,
        to: NodeId,
        input: usize,
    },
    MoveNode(NodeId, Vector),
    Viewport {
        pan: Vector,
        zoom: f32,
    },
    AddNode(&'static str),
    DeleteNode(NodeId),
    /// Node, parameter index, value.
    SetParam(NodeId, usize, f32),
    /// Node, string index, text.
    SetString(NodeId, usize, String),
    /// Opens the asset picker for a node's string (node, string index, what to pick).
    OpenPicker(NodeId, usize, PickKind),
    PickerSearch(String),
    /// A picked asset's project-relative path.
    PickerPick(String),
    PickerClose,
    /// Drops the asset being dragged from the Assets pane onto a node's slot.
    DropAsset(NodeId, usize, PickKind),
    Undo,
    Redo,
    Fit,
    SetSnap(bool),
    SetMergeParts(bool),
    SetMicromesh(bool),
    SetSearch(String),
    TogglePaletteGroup(String),
    /// Puts an Edit Library Material node after a library material node, wired between it and
    /// whatever used it, so the library asset can be changed.
    EditMaterial(NodeId),
    /// Index into the preset list.
    ApplyPreset(usize),
    SetQuality(usize),
    SetName(String),
    New,
    Export,
    Place,
    /// Writes the model as a class of the default scatter plan.
    AddToScatter,
    Noop,
    PreviewPress,
    PreviewMove(Point),
    PreviewRelease,
    PreviewZoom(f32),
}

/// What a node slot takes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PickKind {
    Image,
    Material,
}

impl PickKind {
    /// The engine's asset kind.
    pub fn asset_kind(self) -> &'static str {
        match self {
            PickKind::Image => "image",
            PickKind::Material => "material",
        }
    }

    pub fn badge(self) -> &'static str {
        match self {
            PickKind::Image => "TEX",
            PickKind::Material => "MAT",
        }
    }
}

pub struct View<'a> {
    pub graph: &'a NatureGraph,
    pub selected: Option<NodeId>,
    pub selected_link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub snap: bool,
    pub merge_parts: bool,
    pub micromesh: bool,
    pub search: &'a str,
    pub collapsed_groups: &'a HashSet<String>,
    pub quality: Quality,
    pub name: &'a str,
    pub presets: &'a [String],
    pub preview: Option<&'a Handle>,
    pub stats: &'a str,
    pub status: Option<&'a str>,
    pub dirty: bool,
    /// Project `.mat` assets and image assets, for the Part node's "your own" materials.
    pub project_materials: Vec<String>,
    pub project_textures: Vec<String>,
    /// Thumbnails of the images the selected node uses, by project path.
    pub thumbnails: std::collections::HashMap<String, Handle>,
}

fn accent(category: &str) -> iced::Color {
    match category {
        "Shapes" => theme::ICON_CYAN,
        "Modifiers" => theme::ICON_PURPLE,
        "Trees" => theme::SUCCESS,
        "Plants" => theme::ICON_TEAL,
        "Scatter" => theme::ICON_ORANGE,
        "Materials" => theme::ICON_PINK,
        "Combine" => theme::TEXT_MUTED,
        _ => theme::WARNING,
    }
}

fn port_kind(kind: Kind) -> node_canvas::PortKind {
    match kind {
        Kind::Mesh => node_canvas::PortKind::Mesh,
        Kind::Skeleton => node_canvas::PortKind::Skeleton,
        Kind::Points => node_canvas::PortKind::Points,
        Kind::Material => node_canvas::PortKind::Material,
    }
}

/// The canvas nodes of a graph.
pub fn canvas_nodes(graph: &NatureGraph, selected: Option<NodeId>) -> Vec<node_canvas::Node> {
    graph
        .nodes
        .iter()
        .map(|node| {
            let def = node.def();
            let subtitle = def
                .params
                .iter()
                .enumerate()
                .find(|(_, p)| {
                    matches!(p.kind, ParamKind::Float | ParamKind::Int) && p.key != "seed"
                })
                .map(|(i, p)| {
                    format!(
                        "{} {:.2}",
                        p.label.split(' ').next().unwrap_or(""),
                        node.params[i]
                    )
                })
                .unwrap_or_default();
            node_canvas::Node {
                id: node.id,
                title: def.title.to_owned(),
                subtitle,
                position: node.position,
                inputs: def
                    .inputs
                    .iter()
                    .map(|p| node_canvas::Port {
                        label: p.label.to_owned(),
                        kind: port_kind(p.kind),
                    })
                    .collect(),
                outputs: def
                    .outputs
                    .iter()
                    .map(|p| node_canvas::Port {
                        label: p.label.to_owned(),
                        kind: port_kind(p.kind),
                    })
                    .collect(),
                accent: accent(def.category),
                selected: selected == Some(node.id),
                output: node.op == "output",
                preview: None,
            }
        })
        .collect()
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);

    let title = container(
        row![
            container(text(" ").size(2))
                .width(Length::Fixed(3.0))
                .height(Length::Fixed(24.0))
                .style(theme::nature_bar),
            column![
                text("Nature Generator").size(13).color(theme::TEXT),
                text(format!(
                    "{}{} · {}",
                    view.name,
                    if view.dirty { " •" } else { "" },
                    view.stats
                ))
                .size(9)
                .color(theme::TEXT_MUTED),
            ]
            .spacing(0),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    );
    let preset_lookup = view.presets.to_vec();
    let preset_emit = emit.clone();
    let preset = dropdown::property(
        id::NATUREGEN_PRESET,
        "Preset",
        view.presets.to_vec(),
        dropdown::current_option(view.presets, view.name),
        move |choice| {
            preset_emit(Edit::ApplyPreset(
                preset_lookup.iter().position(|p| *p == choice).unwrap_or(0),
            ))
        },
    );
    let quality_labels: Vec<String> = Quality::ALL.iter().map(|q| q.label().to_owned()).collect();
    let quality_lookup = quality_labels.clone();
    let quality_emit = emit.clone();
    let quality = dropdown::property(
        id::NATUREGEN_QUALITY,
        "Quality",
        quality_labels,
        Some(view.quality.label().to_owned()),
        move |choice| {
            quality_emit(Edit::SetQuality(
                quality_lookup
                    .iter()
                    .position(|l| *l == choice)
                    .unwrap_or(1),
            ))
        },
    );
    let header = container(
        row![
            title,
            iced::widget::space::horizontal(),
            container(preset).width(Length::Fixed(240.0)),
            container(quality).width(Length::Fixed(220.0)),
            button::compact(id::NATUREGEN_UNDO, "↶", emit(Edit::Undo)),
            button::compact(id::NATUREGEN_REDO, "↷", emit(Edit::Redo)),
            button::compact(id::NATUREGEN_FIT, "Fit", emit(Edit::Fit)),
            button::compact(id::NATUREGEN_NEW, "New", emit(Edit::New)),
            button::compact(id::NATUREGEN_EXPORT, "Export", emit(Edit::Export)),
            checkbox::inline(
                id::NATUREGEN_MERGE_PARTS,
                "Merge parts",
                view.merge_parts,
                {
                    let emit = emit.clone();
                    move |value| emit(Edit::SetMergeParts(value))
                }
            ),
            checkbox::inline(id::NATUREGEN_MICROMESH, "Micromesh", view.micromesh, {
                let emit = emit.clone();
                move |value| emit(Edit::SetMicromesh(value))
            }),
            button::compact(
                id::NATUREGEN_SCATTER,
                "Add to scatter plan",
                emit(Edit::AddToScatter)
            ),
            button::primary(id::NATUREGEN_PLACE, "Place in scene", emit(Edit::Place)),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center)
        .wrap(),
    )
    .padding([4, 7])
    .style(theme::panel_title);

    // Palette.
    let mut toolbox = Column::new().spacing(3);
    toolbox = toolbox.push(text_field::search(
        id::NATUREGEN_SEARCH,
        "Search nodes…",
        view.search,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetSearch(value))
        },
    ));
    toolbox = toolbox.push(text("ADD NODE").size(8).color(theme::TEXT_MUTED));
    let needle = view.search.trim().to_lowercase();
    let mut items = Vec::new();
    for category in ops::CATEGORIES {
        let nodes: Vec<&'static crate::uikit::nature::graph::NodeDef> = ops::NODES
            .iter()
            .filter(|n| n.category == category)
            .filter(|n| {
                needle.is_empty()
                    || n.title.to_lowercase().contains(&needle)
                    || category.to_lowercase().contains(&needle)
            })
            .collect();
        if nodes.is_empty() {
            continue;
        }
        let collapsed = needle.is_empty() && view.collapsed_groups.contains(category);
        items.push(tree_view::Item {
            id: id::naturegen_group(category),
            label: format!("{category} ({})", nodes.len()),
            icon: Icon::Folder,
            depth: 0,
            has_children: true,
            expanded: !collapsed,
            selected: false,
            hovered: false,
            drop_target: false,
            trailing: None,
            extra: None,
            on_toggle: emit(Edit::TogglePaletteGroup(category.to_owned())),
            on_press: emit(Edit::TogglePaletteGroup(category.to_owned())),
            on_release: emit(Edit::Noop),
            on_enter: emit(Edit::Noop),
            on_exit: emit(Edit::Noop),
            on_context: emit(Edit::TogglePaletteGroup(category.to_owned())),
        });
        if collapsed {
            continue;
        }
        for def in nodes {
            items.push(tree_view::Item {
                id: id::naturegen_add(def.id),
                label: def.title.to_owned(),
                icon: match category {
                    "Trees" | "Plants" => Icon::Terrain,
                    "Output" => Icon::Material,
                    _ => Icon::Mesh,
                },
                depth: 1,
                has_children: false,
                expanded: false,
                selected: false,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: emit(Edit::Noop),
                on_press: emit(Edit::AddNode(def.id)),
                on_release: emit(Edit::Noop),
                on_enter: emit(Edit::Noop),
                on_exit: emit(Edit::Noop),
                on_context: emit(Edit::Noop),
            });
        }
    }
    toolbox = toolbox.push(tree_view::view(items));
    toolbox = toolbox.push(checkbox::property(
        id::NATUREGEN_SNAP,
        "Snap 16",
        view.snap,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetSnap(value))
        },
    ));
    toolbox = toolbox.push(
        text("Wire Mesh, Skeleton and Points ports of the same colour. Every mesh reaching Output goes through a Part node, which names it and gives it a material.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );

    // Canvas.
    let canvas_emit = emit.clone();
    let canvas = node_canvas::view(
        id::NATUREGEN_CANVAS,
        node_canvas::View {
            nodes: canvas_nodes(view.graph, view.selected),
            links: &view.graph.links,
            pan: view.pan,
            zoom: view.zoom,
            selected_link: view.selected_link,
            snap: view.snap.then_some(16.0),
        },
        move |event| match event {
            node_canvas::Event::Select(selected) => canvas_emit(Edit::Select(selected)),
            node_canvas::Event::SelectLink(link) => canvas_emit(Edit::SelectLink(link)),
            node_canvas::Event::Detach(link) => canvas_emit(Edit::DetachLink(link.0, link.1)),
            node_canvas::Event::InsertDot(..) => canvas_emit(Edit::Noop),
            node_canvas::Event::MoveNode(node, delta) => canvas_emit(Edit::MoveNode(node, delta)),
            node_canvas::Event::Connect {
                from,
                output,
                to,
                input,
            } => canvas_emit(Edit::Connect {
                from,
                output,
                to,
                input,
            }),
            node_canvas::Event::Viewport { pan, zoom } => canvas_emit(Edit::Viewport { pan, zoom }),
        },
    );

    // Right column: properties, preview, naming.
    let mut right = Column::new().spacing(6);
    right = right.push(properties(&view, emit.clone()));
    let press = emit(Edit::PreviewPress);
    let release = emit(Edit::PreviewRelease);
    let move_emit = emit.clone();
    right = right.push(
        container(
            column![
                text("3D PREVIEW").size(8).color(theme::TEXT_MUTED),
                material_preview::view(
                    id::NATUREGEN_PREVIEW,
                    view.preview,
                    250.0,
                    press,
                    move |p| move_emit(Edit::PreviewMove(p)),
                    release
                ),
                text(
                    "Drag to orbit. A quick shape render; Place in scene shows the real materials."
                )
                .size(8)
                .color(theme::TEXT_MUTED),
            ]
            .spacing(3),
        )
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface),
    );
    right = right.push(
        container(
            column![
                text_field::property(id::NATUREGEN_NAME, "Name", view.name, {
                    let emit = emit.clone();
                    move |value| emit(Edit::SetName(value))
                }),
                status::chip(view.stats.to_owned(), theme::ChipTone::Neutral),
            ]
            .spacing(3),
        )
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface),
    );
    if let Some(note) = view.status {
        right = right.push(status::note(note));
    }

    let body = row![
        container(scroll::vertical(toolbox)).width(Length::Fixed(150.0)),
        container(canvas)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface),
        container(scroll::vertical(right)).width(Length::Fixed(270.0)),
    ]
    .spacing(6)
    .height(Length::Fill);

    column![header, container(body).height(Length::Fill)]
        .spacing(0)
        .into()
}

/// Parameters of the selected node: sliders, dropdowns and, for Part, your own assets.
fn properties<'a, Message: Clone + 'a>(
    view: &View<'a>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let graph: &'a NatureGraph = view.graph;
    let Some(node) = view.selected.and_then(|id| graph.node(id)) else {
        return container(
            column![
                text("NODE").size(8).color(theme::TEXT_MUTED),
                text("Select a node to edit its parameters.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(4),
        )
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface)
        .into();
    };
    let def = node.def();
    let node_id = node.id;
    let mut body = Column::new().spacing(3);
    let delete_emit = emit.clone();
    body = body.push(
        row![
            text(def.title).size(12).color(theme::TEXT),
            iced::widget::space::horizontal(),
            // The Output node is the graph's result and stays.
            if node.op == "output" {
                Element::from(text(""))
            } else {
                button::compact(
                    id::NATUREGEN_DELETE_NODE,
                    "×",
                    delete_emit(Edit::DeleteNode(node_id)),
                )
            },
        ]
        .align_y(iced::Alignment::Center),
    );
    body = body.push(
        text(def.category.to_uppercase())
            .size(8)
            .color(theme::TEXT_MUTED),
    );
    for (index, spec) in def.params.iter().enumerate() {
        let value = node.params[index];
        let param_emit = emit.clone();
        match spec.kind {
            ParamKind::Enum(options) => {
                let labels: Vec<String> = options.iter().map(|o| (*o).to_owned()).collect();
                let lookup = labels.clone();
                let selected = labels.get(value.round().max(0.0) as usize).cloned();
                body = body.push(dropdown::property(
                    id::naturegen_param(spec.key),
                    spec.label,
                    labels,
                    selected,
                    move |choice| {
                        param_emit(Edit::SetParam(
                            node_id,
                            index,
                            lookup.iter().position(|l| *l == choice).unwrap_or(0) as f32,
                        ))
                    },
                ));
            }
            ParamKind::Bool => {
                body = body.push(checkbox::property(
                    id::naturegen_param(spec.key),
                    spec.label,
                    value >= 0.5,
                    move |on| {
                        param_emit(Edit::SetParam(node_id, index, if on { 1.0 } else { 0.0 }))
                    },
                ));
            }
            _ => {
                body = body.push(slider::property_step(
                    id::naturegen_param(spec.key),
                    spec.label,
                    spec.min..=spec.max,
                    value,
                    spec.step,
                    move |v| param_emit(Edit::SetParam(node_id, index, v)),
                ));
            }
        }
    }
    // A library material is a fixed asset: this adds the node that edits it.
    if matches!(node.op, "mat_library" | "mat_cards")
        || node.op == "mat_project" && crate::uikit::nature::graph::is_library_node(node)
    {
        let edit_emit = emit.clone();
        body = body.push(
            text("The library material is read-only. Add an edit node to change its colours, surface and wind; it keeps the library asset and writes a copy.")
                .size(8)
                .color(theme::TEXT_MUTED),
        );
        body = body.push(button::compact(
            id::NATUREGEN_EDIT_MATERIAL,
            "Edit this material",
            edit_emit(Edit::EditMaterial(node_id)),
        ));
    }
    // Material nodes that point at your own project assets list the matching files.
    {
        let (options, label): (&[String], &str) = match node.op {
            "mat_project" => (&view.project_materials, "Your .mat"),
            "mat_texture" => (&view.project_textures, "Your image"),
            _ => (&[], ""),
        };
        if !label.is_empty() {
            let current = node.strings.first().cloned().unwrap_or_default();
            let pick_emit = emit.clone();
            body = body.push(dropdown::property(
                id::naturegen_param("asset"),
                label,
                options.to_vec(),
                Some(current.clone()).filter(|c| !c.is_empty()),
                move |choice| pick_emit(Edit::SetString(node_id, 0, choice)),
            ));
            let text_emit = emit.clone();
            body = body.push(text_field::property(
                id::naturegen_param("asset_path"),
                "Path",
                &current,
                move |value| text_emit(Edit::SetString(node_id, 0, value)),
            ));
            if options.is_empty() {
                body = body.push(text("No matching assets found in the project: type a path relative to the assets folder.").size(8).color(theme::TEXT_MUTED));
            }
        }
    }
    let mut slots: Vec<(usize, &'static str, PickKind)> = Vec::new();
    match node.op {
        "texture" => slots.push((0, "Image", PickKind::Image)),
        "material" => slots.push((0, "Base material (.mat)", PickKind::Material)),
        _ => {}
    }
    for (index, label, kind) in slots {
        let current: &'a str = node.strings.get(index).map(String::as_str).unwrap_or("");
        let pick = emit.clone();
        let clear = emit.clone();
        let drop = emit.clone();
        body = body.push(texture_slot::view(
            texture_slot::Slot {
                id: id::naturegen_param(&format!("slot_{index}_{}", kind.asset_kind())),
                label,
                path: current,
                thumbnail: view.thumbnails.get(current).cloned(),
                badge: kind.badge(),
            },
            pick(Edit::OpenPicker(node_id, index, kind)),
            (!current.is_empty()).then(|| clear(Edit::SetString(node_id, index, String::new()))),
            Some(drop(Edit::DropAsset(node_id, index, kind))),
        ));
        let text_emit = emit.clone();
        body = body.push(text_field::property(
            id::naturegen_param(&format!("path_{index}_{}", kind.asset_kind())),
            "Path",
            current,
            move |value| text_emit(Edit::SetString(node_id, index, value)),
        ));
    }
    let _ = (&view.project_materials, &view.project_textures);
    container(body)
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface)
        .into()
}
