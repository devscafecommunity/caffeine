//! Dryad node editor pane: palette, canvas, properties and file actions.
//!
//! The pane is presentation only. It renders a [`dryad::State`], converts
//! canvas hits into stable graph endpoints and emits [`CanvasEvent`]s plus
//! property messages; the window applies them to the state through
//! [`dryad::State`] methods and then commits an undo step if it wants one.
//!
//! Node ids on the reusable canvas are 1-based indices into
//! `state.graph.nodes`. [`translate_event`] maps them back to graph ids, so
//! messages never leak numeric canvas ids to the window.

use crate::uikit::components::{
    button, checkbox, node_canvas, status, text_field, toggle_chip, tree_view,
};
use crate::uikit::dryad::{self, NodeKind, PinKind};
use crate::uikit::icons::Icon;
use crate::uikit::id::{self, UiId};
use crate::uikit::theme;
use iced::widget::{column, container, row, scrollable, text, Column};
use iced::{Element, Length, Vector};
use std::rc::Rc;

/// A wire, as its source and target endpoints (`"n2.value"` → `"n3.y"`).
pub type LinkEndpoints = (String, String);

/// A canvas edit with graph ids instead of canvas indices.
///
/// `SelectLink`/`Detach` carry the wire the canvas hit; a wire is identified
/// by its source and target endpoints (`"n2.value"` → `"n3.y"`).
#[derive(Debug, Clone, PartialEq)]
pub enum CanvasEvent {
    Select(Option<String>),
    SelectLink(Option<LinkEndpoints>),
    Detach { to: String },
    MoveNode { id: String, delta: Vector },
    Connect { from: String, to: String },
    Viewport { pan: Vector, zoom: f32 },
}

/// Stable widget ids for the pane.
#[derive(Debug, Clone)]
pub struct Ids {
    pub root: UiId,
    pub canvas: UiId,
    pub palette_search: UiId,
    pub save: UiId,
    pub sync: UiId,
    pub delete: UiId,
    pub fit: UiId,
    pub attach: UiId,
    pub run: UiId,
    pub add_motion: UiId,
    pub status: UiId,
    /// Palette row id for a catalog `type_name`.
    pub palette_item: fn(&str) -> UiId,
    /// Property field id for a field key (`"x"`, `"value"`, `"text"`…).
    pub property: fn(&str) -> UiId,
}

impl Default for Ids {
    fn default() -> Self {
        Self {
            root: id::DRYAD_ROOT,
            canvas: id::DRYAD_CANVAS,
            palette_search: id::DRYAD_PALETTE_SEARCH,
            save: id::DRYAD_SAVE,
            sync: id::DRYAD_SYNC,
            delete: id::DRYAD_DELETE,
            fit: id::DRYAD_FIT,
            attach: id::DRYAD_ATTACH,
            run: id::DRYAD_RUN,
            add_motion: id::DRYAD_ADD_MOTION,
            status: id::DRYAD_STATUS,
            palette_item: id::dryad_palette_item,
            property: id::dryad_property,
        }
    }
}

fn slug(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    for character in text.chars() {
        if character.is_ascii_alphanumeric() {
            out.push(character.to_ascii_lowercase());
        } else if !out.ends_with('_') {
            out.push('_');
        }
    }
    out.trim_matches('_').to_owned()
}

/// The pane state a caller renders.
pub struct View<'a> {
    pub state: &'a dryad::State,
}

/// Messages the pane can emit. Every closure receives graph ids or raw field
/// text; the window owns the corresponding [`dryad::State`] mutation.
///
/// - `on_canvas`: a canvas edit translated to graph endpoints.
/// - `on_search`: palette search text.
/// - `add_node`: add a catalog kind and select it.
/// - `select_node` / `select_link`: direct selection (also reachable from the
///   canvas; the window may wire them to other surfaces).
/// - `save`, `sync`, `delete`, `fit`, `attach`, `run`, `add_motion`: toolbar
///   actions (`attach`/`run` are runtime actions the engine handles).
/// - `number_text(node, field, text)`: raw numeric text. The window should
///   store it as a draft and call [`dryad::State::set_number`] when it parses.
/// - `set_bool(node, field, value)` / `set_text(node, text)`: checked/typed
///   edits.
/// - `noop`: emitted by inert tree rows and ignored canvas events.
pub struct Callbacks<'a, Message> {
    pub on_canvas: Box<dyn Fn(CanvasEvent) -> Message + 'a>,
    pub on_search: Box<dyn Fn(String) -> Message + 'a>,
    pub add_node: Box<dyn Fn(NodeKind) -> Message + 'a>,
    pub select_node: Box<dyn Fn(Option<String>) -> Message + 'a>,
    pub select_link: Box<dyn Fn(Option<LinkEndpoints>) -> Message + 'a>,
    pub delete: Message,
    pub save: Message,
    pub sync: Message,
    pub attach: Message,
    pub run: Message,
    pub fit: Message,
    pub add_motion: Message,
    pub number_text: Box<dyn Fn(String, String, String) -> Message + 'a>,
    pub set_bool: Box<dyn Fn(String, String, bool) -> Message + 'a>,
    pub set_text: Box<dyn Fn(String, String) -> Message + 'a>,
    pub noop: Message,
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let ids = Ids::default();
    let state = view.state;
    let on_canvas: Rc<dyn Fn(CanvasEvent) -> Message + 'a> = Rc::from(callbacks.on_canvas);
    let on_search: Rc<dyn Fn(String) -> Message + 'a> = Rc::from(callbacks.on_search);
    let add_node: Rc<dyn Fn(NodeKind) -> Message + 'a> = Rc::from(callbacks.add_node);
    let on_number: Rc<dyn Fn(String, String, String) -> Message + 'a> =
        Rc::from(callbacks.number_text);
    let on_bool: Rc<dyn Fn(String, String, bool) -> Message + 'a> = Rc::from(callbacks.set_bool);
    let on_text: Rc<dyn Fn(String, String) -> Message + 'a> = Rc::from(callbacks.set_text);
    let noop = callbacks.noop.clone();

    let script_label = if !state.script_relative.is_empty() {
        state.script_relative.clone()
    } else {
        state
            .script_path
            .as_ref()
            .and_then(|path| path.file_name())
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| "No script open".to_owned())
    };

    let header_actions = row![
        if state.dirty {
            status::chip("Unsaved", theme::ChipTone::Warning)
        } else {
            status::chip("Saved", theme::ChipTone::Success)
        },
        button::toolbar(ids.sync.clone(), "Sync from .dyd", callbacks.sync.clone()),
        button::compact(ids.fit.clone(), "Fit", callbacks.fit.clone()),
        button::danger(ids.delete.clone(), "Delete", callbacks.delete.clone()),
        button::toolbar(ids.attach.clone(), "Attach", callbacks.attach.clone()),
        button::toolbar(ids.run.clone(), "Run once", callbacks.run.clone()),
        button::toolbar(
            ids.add_motion.clone(),
            "Add motion",
            callbacks.add_motion.clone(),
        ),
        button::primary(ids.save.clone(), "Save", callbacks.save.clone()),
    ]
    .spacing(6)
    .align_y(iced::Alignment::Center);

    let header = container(
        row![
            column![
                text("Dryad Nodes").size(13).color(theme::TEXT),
                text(script_label).size(9).color(theme::TEXT_MUTED),
            ]
            .spacing(0)
            .width(Length::Fill),
            header_actions,
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .padding([4, 7])
    .style(theme::panel_title);

    let Some(graph) = state.graph.as_ref() else {
        let empty = container(
            column![
                text("No Dryad script open").size(13).color(theme::TEXT),
                text(
                    "Open a .dyd from the Assets pane: double-click it, or drag it onto an \
                     entity in the Hierarchy. The graph editor then edits the sibling \
                     .node.json and rewrites the .dyd on Save."
                )
                .size(10)
                .color(theme::TEXT_MUTED),
                text("The .dyd is the program; the JSON only records how the canvas drew it.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(6),
        )
        .id(id::DRYAD_EMPTY)
        .width(Length::Fill)
        .height(Length::Fill)
        .padding(18)
        .style(theme::surface);
        let body = container(empty)
            .width(Length::Fill)
            .height(Length::Fill)
            .padding(8);
        return container(column![header, body].spacing(0))
            .id(ids.root.clone())
            .width(Length::Fill)
            .height(Length::Fill)
            .into();
    };

    let palette = palette_panel(
        &ids,
        state,
        noop.clone(),
        on_search.clone(),
        add_node.clone(),
    );

    let nodes = canvas_nodes(graph, state.selected.as_deref());
    let links = canvas_links(graph);
    let selected_link = state
        .selected_link
        .as_ref()
        .and_then(|link| link_key(graph, link));
    let canvas_graph = graph;
    let canvas_emit = on_canvas.clone();
    let canvas_noop = noop.clone();
    let canvas = node_canvas::view_owned(
        ids.canvas.clone(),
        nodes,
        links,
        state.pan,
        state.zoom,
        selected_link,
        move |event| match translate_event(event, canvas_graph) {
            Some(event) => canvas_emit(event),
            None => canvas_noop.clone(),
        },
    );

    let mut canvas_column = Column::new().spacing(4).height(Length::Fill);
    if graph.on_update().is_none() {
        canvas_column = canvas_column.push(container(status::chip(
            "No OnUpdate node: play mode will do nothing.",
            theme::ChipTone::Danger,
        )));
    } else if !exec_has_link(graph) {
        canvas_column = canvas_column.push(container(status::chip(
            "OnUpdate has no outgoing exec link — use Add motion.",
            theme::ChipTone::Warning,
        )));
    }
    canvas_column = canvas_column.push(container(canvas).height(Length::Fill));
    let status_line = container(
        text(state.status.clone().unwrap_or_default())
            .size(9)
            .color(theme::TEXT_MUTED),
    )
    .id(ids.status.clone())
    .width(Length::Fill)
    .padding([2, 5]);

    let properties = properties_panel(
        &ids,
        state,
        graph,
        on_number.clone(),
        on_bool.clone(),
        on_text.clone(),
        callbacks.delete.clone(),
    );

    let body = row![
        container(scrollable(palette)).width(Length::Fixed(172.0)),
        container(canvas_column)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface),
        container(scrollable(properties)).width(Length::Fixed(248.0)),
    ]
    .spacing(6)
    .height(Length::Fill);

    container(column![header, container(body).height(Length::Fill), status_line].spacing(0))
        .id(ids.root.clone())
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
}

fn palette_panel<'a, Message: Clone + 'a>(
    ids: &Ids,
    state: &dryad::State,
    noop: Message,
    on_search: Rc<dyn Fn(String) -> Message + 'a>,
    add_node: Rc<dyn Fn(NodeKind) -> Message + 'a>,
) -> Element<'a, Message> {
    let mut content = Column::new().spacing(4);
    content = content.push(text_field::search(
        ids.palette_search.clone(),
        "Search nodes…",
        &state.search,
        move |value| on_search(value),
    ));
    content = content.push(text("ADD NODE").size(8).color(theme::TEXT_MUTED));

    let needle = state.search.trim().to_lowercase();
    let mut groups: Vec<(&'static str, Vec<&'static dryad::CatalogEntry>)> = Vec::new();
    for entry in dryad::catalog() {
        let matched = needle.is_empty()
            || entry.display_name.to_lowercase().contains(&needle)
            || entry.type_name.to_lowercase().contains(&needle)
            || entry.category.to_lowercase().contains(&needle)
            || entry.description.to_lowercase().contains(&needle);
        if !matched {
            continue;
        }
        if let Some((_, entries)) = groups.iter_mut().find(|(name, _)| *name == entry.category) {
            entries.push(entry);
        } else {
            groups.push((entry.category, vec![entry]));
        }
    }

    let mut items = Vec::new();
    for (category, entries) in groups {
        items.push(tree_view::Item {
            id: UiId::dynamic(format!("dryad.palette.group.{}", slug(category))),
            label: format!("{category} ({})", entries.len()),
            icon: Icon::Folder,
            depth: 0,
            has_children: false,
            expanded: false,
            selected: false,
            hovered: false,
            drop_target: false,
            trailing: None,
            extra: None,
            on_toggle: noop.clone(),
            on_press: noop.clone(),
            on_release: noop.clone(),
            on_enter: noop.clone(),
            on_exit: noop.clone(),
            on_context: noop.clone(),
        });
        for entry in entries {
            items.push(tree_view::Item {
                id: (ids.palette_item)(entry.type_name),
                label: entry.display_name.to_owned(),
                icon: palette_icon(entry.category),
                depth: 1,
                has_children: false,
                expanded: false,
                selected: false,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: noop.clone(),
                on_press: add_node(entry.kind),
                on_release: noop.clone(),
                on_enter: noop.clone(),
                on_exit: noop.clone(),
                on_context: noop.clone(),
            });
        }
    }
    if items.is_empty() {
        content = content.push(
            text("No nodes match that search.")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    } else {
        content = content.push(tree_view::view(items));
    }
    content = content.push(
        text("Drag a pin to link. Drag empty space to pan, wheel to zoom.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );
    content.into()
}

fn palette_icon(category: &str) -> Icon {
    match category {
        "Engine / Transform" => Icon::Mesh,
        "Engine / Time" => Icon::Wind,
        "Engine / Events" => Icon::Scene,
        "Engine / Debug" => Icon::Audio,
        _ => Icon::Script,
    }
}

fn properties_panel<'a, Message: Clone + 'a>(
    ids: &Ids,
    state: &dryad::State,
    graph: &'a dryad::Graph,
    emit_number: Rc<dyn Fn(String, String, String) -> Message + 'a>,
    emit_bool: Rc<dyn Fn(String, String, bool) -> Message + 'a>,
    emit_text: Rc<dyn Fn(String, String) -> Message + 'a>,
    delete: Message,
) -> Element<'a, Message> {
    let Some(node) = state.selected.as_deref().and_then(|id| graph.find_node(id)) else {
        let mut body = column![
            text("NODE").size(8).color(theme::TEXT_MUTED),
            text("Select a node to edit its parameters.")
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4);
        if let Some((from, to)) = &state.selected_link {
            body = body.push(
                text(format!("Link {from} → {to}"))
                    .size(9)
                    .color(theme::TEXT),
            );
            body = body.push(button::danger(
                (ids.property)("delete_link"),
                "Delete link",
                delete,
            ));
        }
        return container(body)
            .id(id::DRYAD_PROPERTIES)
            .padding(6)
            .style(theme::surface)
            .into();
    };

    let node_id = node.id.clone();
    let kind = node.kind;
    let mut body = Column::new().spacing(5);
    body = body.push(
        row![
            column![
                text(kind.display_name()).size(12).color(theme::TEXT),
                text(format!("{} · {}", kind.type_name(), node.id))
                    .size(8)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(0)
            .width(Length::Fill),
            button::danger((ids.property)("delete_node"), "×", delete),
        ]
        .align_y(iced::Alignment::Center),
    );
    body = body.push(text(kind.description()).size(9).color(theme::TEXT_MUTED));

    match kind {
        NodeKind::Constant => {
            body = body.push(
                row![
                    toggle_chip::view(
                        (ids.property)("constant_number"),
                        "Number",
                        !node.constant_is_bool,
                        theme::ACCENT,
                        (emit_bool)(node_id.clone(), "constant_is_bool".to_owned(), false),
                    ),
                    toggle_chip::view(
                        (ids.property)("constant_bool"),
                        "Bool",
                        node.constant_is_bool,
                        theme::ACCENT,
                        (emit_bool)(node_id.clone(), "constant_is_bool".to_owned(), true),
                    ),
                ]
                .spacing(4),
            );
            if node.constant_is_bool {
                let emit = emit_bool.clone();
                let id = node_id.clone();
                body = body.push(checkbox::property(
                    (ids.property)("value"),
                    "Value",
                    node.bool_value,
                    move |value| emit(id.clone(), "value".to_owned(), value),
                ));
            } else {
                body = body.push(number_field(
                    ids,
                    &node_id,
                    "value",
                    "Value",
                    &display_number(state, &node_id, "value", node.number_value),
                    emit_number.clone(),
                ));
            }
        }
        NodeKind::SetPosition | NodeKind::Translate | NodeKind::SetScale => {
            for (field, label, value) in [
                ("x", "X", node.input_x),
                ("y", "Y", node.input_y),
                ("z", "Z", node.input_z),
            ] {
                if let Some(source) = wired_source(graph, &node_id, field) {
                    body = body.push(
                        text(format!("{label} ← {source}"))
                            .size(9)
                            .color(theme::ICON_CYAN),
                    );
                } else {
                    body = body.push(number_field(
                        ids,
                        &node_id,
                        field,
                        label,
                        &display_number(state, &node_id, field, value),
                        emit_number.clone(),
                    ));
                }
            }
        }
        NodeKind::Branch => {
            if let Some(source) = wired_source(graph, &node_id, "condition") {
                body = body.push(
                    text(format!("Condition ← {source}"))
                        .size(9)
                        .color(theme::ICON_CYAN),
                );
            } else {
                let emit = emit_bool.clone();
                let id = node_id.clone();
                body = body.push(checkbox::property(
                    (ids.property)("condition"),
                    "Condition",
                    node.input_condition,
                    move |value| emit(id.clone(), "condition".to_owned(), value),
                ));
            }
            body = body.push(
                text("then / else run exec chains; out continues after the if.")
                    .size(8)
                    .color(theme::TEXT_MUTED),
            );
        }
        NodeKind::Log => {
            let emit = emit_text.clone();
            let id = node_id.clone();
            body = body.push(text_field::property(
                (ids.property)("text"),
                "Message",
                &node.text_value,
                move |value| emit(id.clone(), value),
            ));
        }
        NodeKind::OnUpdate => {
            body = body.push(
                text("Runs once per frame while the entity is in play mode.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
            if !exec_has_link(graph) {
                body = body.push(status::chip("No exec link yet", theme::ChipTone::Warning));
            }
        }
        NodeKind::GetPosition | NodeKind::GetScale | NodeKind::DeltaTime => {
            body = body.push(
                text("Read-only data node: drag one of its pins into an input.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
        }
    }

    container(body)
        .id(id::DRYAD_PROPERTIES)
        .padding(6)
        .style(theme::surface)
        .into()
}

fn number_field<'a, Message: Clone + 'a>(
    ids: &Ids,
    node: &str,
    field: &'static str,
    label: &'static str,
    value: &str,
    emit: Rc<dyn Fn(String, String, String) -> Message + 'a>,
) -> Element<'a, Message> {
    let id = node.to_owned();
    text_field::property((ids.property)(field), label, value, move |text| {
        emit(id.clone(), field.to_owned(), text.clone())
    })
}

/// The text a numeric field shows: the draft while it is being typed, else a
/// short round-tripping form of the stored value.
pub fn display_number(state: &dryad::State, node: &str, field: &str, value: f64) -> String {
    if let Some(draft) = state.draft(node, field) {
        return draft.to_owned();
    }
    if value.is_finite() && value.fract() == 0.0 && value.abs() < 1e15 {
        format!("{value:.0}")
    } else {
        format!("{value}")
    }
}

fn wired_source(graph: &dryad::Graph, node: &str, field: &str) -> Option<String> {
    let endpoint = format!("{node}.{field}");
    let edge = graph.edge_into(&endpoint)?;
    let (source_node, source_pin) = dryad::parse_pin_ref(&edge.from)?;
    Some(format!("{source_node}.{source_pin}"))
}

fn exec_has_link(graph: &dryad::Graph) -> bool {
    let Some(on_update) = graph.on_update() else {
        return false;
    };
    let source = format!("{}.out", on_update.id);
    graph.edges.iter().any(|edge| edge.from == source)
}

// ── Canvas projection ───────────────────────────────────────────────────

fn canvas_pin_kind(kind: PinKind) -> node_canvas::PortKind {
    match kind {
        PinKind::Exec => node_canvas::PortKind::Exec,
        PinKind::Number | PinKind::Bool => node_canvas::PortKind::Scalar,
    }
}

fn accent(kind: NodeKind) -> iced::Color {
    match kind {
        NodeKind::OnUpdate => theme::SUCCESS,
        NodeKind::SetPosition => theme::ACCENT,
        NodeKind::Translate => theme::ICON_CYAN,
        NodeKind::SetScale => theme::ICON_TEAL,
        NodeKind::GetPosition | NodeKind::GetScale => theme::WARNING,
        NodeKind::DeltaTime => theme::ICON_PURPLE,
        NodeKind::Log => theme::TEXT_MUTED,
        NodeKind::Constant => theme::BORDER_STRONG,
        NodeKind::Branch => theme::WARNING,
    }
}

fn canvas_index(graph: &dryad::Graph, id: &str) -> Option<u64> {
    graph
        .nodes
        .iter()
        .position(|node| node.id == id)
        .map(|index| index as u64 + 1)
}

/// Graph nodes projected onto the reusable canvas. Canvas ids are stable
/// 1-based indices in `graph.nodes`.
pub fn canvas_nodes(graph: &dryad::Graph, selected: Option<&str>) -> Vec<node_canvas::Node> {
    graph
        .nodes
        .iter()
        .enumerate()
        .map(|(index, node)| node_canvas::Node {
            id: index as u64 + 1,
            title: node.kind.display_name().to_owned(),
            subtitle: node.kind.subtitle().to_owned(),
            position: iced::Point::new(node.x, node.y),
            inputs: dryad::inputs(node.kind)
                .iter()
                .map(|pin| node_canvas::Port {
                    label: pin.name.to_owned(),
                    kind: canvas_pin_kind(pin.kind),
                })
                .collect(),
            outputs: dryad::outputs_for(node)
                .iter()
                .map(|pin| node_canvas::Port {
                    label: pin.name.to_owned(),
                    kind: canvas_pin_kind(pin.kind),
                })
                .collect(),
            accent: accent(node.kind),
            selected: selected == Some(node.id.as_str()),
            output: false,
        })
        .collect()
}

/// Edges projected onto canvas links. Invalid endpoints are skipped, matching
/// the C++ panel.
pub fn canvas_links(graph: &dryad::Graph) -> Vec<node_canvas::Link> {
    graph
        .edges
        .iter()
        .filter_map(|edge| {
            let (from_id, from_pin) = dryad::parse_pin_ref(&edge.from)?;
            let (to_id, to_pin) = dryad::parse_pin_ref(&edge.to)?;
            let from = graph.find_node(from_id)?;
            let to = graph.find_node(to_id)?;
            let output = dryad::outputs_for(from)
                .iter()
                .position(|pin| pin.name == from_pin)?;
            let input = dryad::inputs(to.kind)
                .iter()
                .position(|pin| pin.name == to_pin)?;
            Some(node_canvas::Link {
                from: canvas_index(graph, from_id)?,
                output,
                to: canvas_index(graph, to_id)?,
                input,
            })
        })
        .collect()
}

fn link_key(graph: &dryad::Graph, link: &LinkEndpoints) -> Option<node_canvas::LinkKey> {
    let (to_id, to_pin) = dryad::parse_pin_ref(&link.1)?;
    let to = graph.find_node(to_id)?;
    let input = dryad::inputs(to.kind)
        .iter()
        .position(|pin| pin.name == to_pin)?;
    Some((canvas_index(graph, to_id)?, input))
}

fn canvas_node(graph: &dryad::Graph, id: u64) -> Option<&dryad::GraphNode> {
    graph.nodes.get((id as usize).checked_sub(1)?)
}

fn canvas_endpoint(graph: &dryad::Graph, node: u64, index: usize, output: bool) -> Option<String> {
    let node = canvas_node(graph, node)?;
    let pin = if output {
        dryad::outputs_for(node).get(index)?.name
    } else {
        dryad::inputs(node.kind).get(index)?.name
    };
    Some(format!("{}.{}", node.id, pin))
}

fn link_at_key(graph: &dryad::Graph, node: u64, input: usize) -> Option<LinkEndpoints> {
    let to = canvas_endpoint(graph, node, input, false)?;
    let edge = graph.edge_into(&to)?;
    Some((edge.from.clone(), edge.to.clone()))
}

/// Translate a reusable-canvas event into graph endpoints. `None` for events
/// the Dryad canvas does not use (reroute dots).
pub fn translate_event(event: node_canvas::Event, graph: &dryad::Graph) -> Option<CanvasEvent> {
    match event {
        node_canvas::Event::Select(id) => {
            Some(CanvasEvent::Select(id.and_then(|id| {
                canvas_node(graph, id).map(|node| node.id.clone())
            })))
        }
        node_canvas::Event::SelectLink(key) => Some(CanvasEvent::SelectLink(
            key.and_then(|(node, input)| link_at_key(graph, node, input)),
        )),
        node_canvas::Event::Detach((node, input)) => {
            let to = canvas_endpoint(graph, node, input, false)?;
            Some(CanvasEvent::Detach { to })
        }
        node_canvas::Event::InsertDot(..) => None,
        node_canvas::Event::MoveNode(id, delta) => Some(CanvasEvent::MoveNode {
            id: canvas_node(graph, id)?.id.clone(),
            delta,
        }),
        node_canvas::Event::Connect {
            from,
            output,
            to,
            input,
        } => Some(CanvasEvent::Connect {
            from: canvas_endpoint(graph, from, output, true)?,
            to: canvas_endpoint(graph, to, input, false)?,
        }),
        node_canvas::Event::Viewport { pan, zoom } => Some(CanvasEvent::Viewport { pan, zoom }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::uikit::dryad::Graph;

    fn callbacks() -> Callbacks<'static, ()> {
        Callbacks {
            on_canvas: Box::new(|_| ()),
            on_search: Box::new(|_| ()),
            add_node: Box::new(|_| ()),
            select_node: Box::new(|_| ()),
            select_link: Box::new(|_| ()),
            delete: (),
            save: (),
            sync: (),
            attach: (),
            run: (),
            fit: (),
            add_motion: (),
            number_text: Box::new(|_, _, _| ()),
            set_bool: Box::new(|_, _, _| ()),
            set_text: Box::new(|_, _| ()),
            noop: (),
        }
    }

    fn graph() -> (Graph, String, String) {
        let mut graph = Graph::new("test.dyd");
        let root = graph.add_node(NodeKind::OnUpdate, 0.0, 0.0);
        let log = graph.add_node(NodeKind::Log, 200.0, 0.0);
        graph
            .connect(&format!("{root}.out"), &format!("{log}.in"))
            .expect("exec link");
        (graph, root, log)
    }

    #[test]
    fn empty_state_explains_the_asset_flow() {
        let state = dryad::State::new();
        let _element: Element<'_, ()> = view(View { state: &state }, callbacks());
    }

    #[test]
    fn open_graph_builds_the_three_columns() {
        let mut state = dryad::State::new();
        state.graph = Some(graph().0);
        state.selected = state.graph.as_ref().map(|graph| graph.nodes[1].id.clone());
        let _element: Element<'_, ()> = view(View { state: &state }, callbacks());
    }

    #[test]
    fn canvas_ids_are_stable_and_map_back_to_graph_endpoints() {
        let (graph, root, log) = graph();
        let nodes = canvas_nodes(&graph, None);
        assert_eq!(nodes.len(), 2);
        assert_eq!(nodes[0].id, 1);
        assert_eq!(nodes[1].id, 2);
        assert_eq!(nodes[0].outputs[0].kind, node_canvas::PortKind::Exec);
        assert_eq!(nodes[1].inputs[0].kind, node_canvas::PortKind::Exec);
        let links = canvas_links(&graph);
        assert_eq!(
            links,
            vec![node_canvas::Link {
                from: 1,
                output: 0,
                to: 2,
                input: 0,
            }]
        );

        let selected = translate_event(node_canvas::Event::Select(Some(2)), &graph);
        assert_eq!(selected, Some(CanvasEvent::Select(Some(log.clone()))));

        let connected = translate_event(
            node_canvas::Event::Connect {
                from: 1,
                output: 0,
                to: 2,
                input: 0,
            },
            &graph,
        );
        assert_eq!(
            connected,
            Some(CanvasEvent::Connect {
                from: format!("{root}.out"),
                to: format!("{log}.in"),
            })
        );

        let link = translate_event(node_canvas::Event::SelectLink(Some((2, 0))), &graph);
        assert_eq!(
            link,
            Some(CanvasEvent::SelectLink(Some((
                format!("{root}.out"),
                format!("{log}.in"),
            ))))
        );

        let moved = translate_event(
            node_canvas::Event::MoveNode(2, Vector::new(4.0, 6.0)),
            &graph,
        );
        assert_eq!(
            moved,
            Some(CanvasEvent::MoveNode {
                id: log,
                delta: Vector::new(4.0, 6.0),
            })
        );
        assert!(translate_event(
            node_canvas::Event::InsertDot((2, 0), iced::Point::ORIGIN),
            &graph
        )
        .is_none());
    }

    #[test]
    fn number_fields_show_the_draft_first() {
        let mut state = dryad::State::new();
        state.set_draft("n2", "y", "0.".to_owned());
        assert_eq!(display_number(&state, "n2", "y", 1.5), "0.");
        assert_eq!(display_number(&state, "n2", "x", 2.0), "2");
        assert_eq!(display_number(&state, "n2", "x", 2.5), "2.5");
    }
}
