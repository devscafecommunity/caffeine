//! Terrain Generator panel: a node graph for heightfields, with a live 3D preview.
//!
//! Left: the node palette by category. Middle: the node canvas. Right: the selected node's
//! parameters (sliders and dropdowns generated from each node's table row), the preview (drag to
//! orbit) and the export / placement controls. The panel is a pure view: the app
//! (`windows/main/naturegen.rs`) owns the state and handles the messages.
//!
//! The preview is a software 3D render (drag to orbit, scroll to zoom); **Place in scene** makes the
//! real engine terrain with its splat layers.

use crate::uikit::components::{
    button, checkbox, dropdown, material_preview, node_canvas, scroll, slider, status, text_field,
    tree_view,
};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::procedural::ParamKind;
use crate::uikit::terrain::{self, NodeId, TerrainGraph};
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
    Undo,
    Redo,
    Fit,
    SetSnap(bool),
    SetSearch(String),
    TogglePaletteGroup(String),
    /// Index into the preset list.
    ApplyPreset(usize),
    SetName(String),
    New,
    Export,
    Place,
    Noop,
    PreviewPress,
    PreviewMove(Point),
    PreviewRelease,
    PreviewZoom(f32),
}

pub struct View<'a> {
    pub graph: &'a TerrainGraph,
    pub selected: Option<NodeId>,
    pub selected_link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub snap: bool,
    pub search: &'a str,
    pub collapsed_groups: &'a HashSet<String>,
    pub name: &'a str,
    pub presets: &'a [String],
    pub preview: Option<&'a Handle>,
    pub stats: &'a str,
    pub status: Option<&'a str>,
    pub dirty: bool,
}

fn accent(category: &str) -> iced::Color {
    match category {
        "Sources" => theme::ICON_ORANGE,
        "Modifiers" => theme::ICON_PURPLE,
        "Derive" => theme::ICON_CYAN,
        "Combine" => theme::TEXT_MUTED,
        "Simulate" => theme::SUCCESS,
        _ => theme::WARNING,
    }
}

/// The canvas nodes of a graph.
pub fn canvas_nodes(graph: &TerrainGraph, selected: Option<NodeId>) -> Vec<node_canvas::Node> {
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
                        kind: node_canvas::PortKind::Height,
                    })
                    .collect(),
                outputs: def
                    .outputs
                    .iter()
                    .map(|p| node_canvas::Port {
                        label: p.label.to_owned(),
                        kind: node_canvas::PortKind::Height,
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
                .style(theme::terrain_bar),
            column![
                text("Terrain Generator").size(13).color(theme::TEXT),
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
        id::TERRAINGEN_PRESET,
        "Preset",
        view.presets.to_vec(),
        None,
        move |choice| {
            preset_emit(Edit::ApplyPreset(
                preset_lookup.iter().position(|p| *p == choice).unwrap_or(0),
            ))
        },
    );
    let header = container(
        row![
            title,
            iced::widget::space::horizontal(),
            container(preset).width(Length::Fixed(240.0)),
            button::compact(id::TERRAINGEN_UNDO, "↶", emit(Edit::Undo)),
            button::compact(id::TERRAINGEN_REDO, "↷", emit(Edit::Redo)),
            button::compact(id::TERRAINGEN_FIT, "Fit", emit(Edit::Fit)),
            button::compact(id::TERRAINGEN_NEW, "New", emit(Edit::New)),
            button::compact(id::TERRAINGEN_EXPORT, "Export", emit(Edit::Export)),
            button::primary(id::TERRAINGEN_PLACE, "Place in scene", emit(Edit::Place)),
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
        id::TERRAINGEN_SEARCH,
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
    for category in terrain::CATEGORIES {
        let nodes: Vec<&'static terrain::NodeDef> = terrain::NODES
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
            id: id::terraingen_group(category),
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
                id: id::terraingen_add(def.id),
                label: def.title.to_owned(),
                icon: match category {
                    "Output" => Icon::Material,
                    _ => Icon::Terrain,
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
        id::TERRAINGEN_SNAP,
        "Snap 16",
        view.snap,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetSnap(value))
        },
    ));
    toolbox = toolbox.push(
        text("Wire a height into the Output node. Every node passes a field (a grid of 0-1 values): heights and masks use the same ports. Combine takes an optional mask; the Hydraulic node also outputs Wear and Deposit masks.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );

    // Canvas.
    let canvas_emit = emit.clone();
    let canvas = node_canvas::view(
        id::TERRAINGEN_CANVAS,
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
    right = right.push(
        container(
            column![
                text("3D PREVIEW").size(8).color(theme::TEXT_MUTED),
                material_preview::view(
                    id::TERRAINGEN_PREVIEW,
                    view.preview,
                    250.0,
                    emit(Edit::PreviewPress),
                    {
                        let emit = emit.clone();
                        move |p| emit(Edit::PreviewMove(p))
                    },
                    emit(Edit::PreviewRelease)
                ),
                text("Drag to orbit. A quick shape render with the splat layers; Place in scene shows the engine terrain.")
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
                text_field::property(id::TERRAINGEN_NAME, "Name", view.name, {
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
        right = right.push(
            container(text(note).size(9).color(theme::WARNING))
                .width(Length::Fill)
                .padding(3),
        );
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
    let Some(node) = view.selected.and_then(|id| view.graph.node(id)) else {
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
                    id::TERRAINGEN_DELETE_NODE,
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
                    id::terraingen_param(spec.key),
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
                    id::terraingen_param(spec.key),
                    spec.label,
                    value >= 0.5,
                    move |on| {
                        param_emit(Edit::SetParam(node_id, index, if on { 1.0 } else { 0.0 }))
                    },
                ));
            }
            _ => {
                body = body.push(slider::property_step(
                    id::terraingen_param(spec.key),
                    spec.label,
                    spec.min..=spec.max,
                    value,
                    spec.step,
                    move |v| param_emit(Edit::SetParam(node_id, index, v)),
                ));
            }
        }
    }
    container(body)
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface)
        .into()
}
