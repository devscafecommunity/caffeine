//! Reusable node canvas: pan, zoom, draggable cards, typed ports and links.
//!
//! The canvas owns interaction only. Callers keep the graph model, apply the
//! emitted edits and pass the graph back next frame. This keeps it usable for
//! material generators and for any other node-based tool added later.

use crate::uikit::theme;
use iced::widget::canvas::{self, Frame, LineCap, Path, Stroke, Text};
use iced::widget::Canvas;
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size, Vector};
use std::time::Instant;

pub type NodeId = u64;
pub type LinkKey = (NodeId, usize);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PortKind {
    Color,
    Scalar,
}

impl PortKind {
    pub fn color(self) -> Color {
        match self {
            PortKind::Color => theme::ICON_CYAN,
            PortKind::Scalar => theme::TEXT_MUTED,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            PortKind::Color => "color",
            PortKind::Scalar => "scalar",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Port {
    pub label: String,
    pub kind: PortKind,
}

#[derive(Debug, Clone)]
pub struct Node {
    pub id: NodeId,
    pub title: String,
    pub subtitle: String,
    pub position: Point,
    pub inputs: Vec<Port>,
    pub outputs: Vec<Port>,
    pub accent: Color,
    pub selected: bool,
    pub output: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Link {
    pub from: NodeId,
    pub output: usize,
    pub to: NodeId,
    pub input: usize,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Event {
    Select(Option<NodeId>),
    SelectLink(Option<LinkKey>),
    Detach(LinkKey),
    InsertDot(LinkKey, Point),
    MoveNode(NodeId, Vector),
    Connect {
        from: NodeId,
        output: usize,
        to: NodeId,
        input: usize,
    },
    Viewport {
        pan: Vector,
        zoom: f32,
    },
}

pub struct View<'a> {
    pub nodes: Vec<Node>,
    pub links: &'a [Link],
    pub pan: Vector,
    pub zoom: f32,
    pub selected_link: Option<LinkKey>,
}

const NODE_WIDTH: f32 = 158.0;
const HEADER_HEIGHT: f32 = 24.0;
const PORT_SPACING: f32 = 18.0;
const BODY_PADDING: f32 = 9.0;
const PORT_RADIUS: f32 = 4.5;
const MIN_ZOOM: f32 = 0.35;
const MAX_ZOOM: f32 = 2.2;

fn node_height(node: &Node) -> f32 {
    let ports = node.inputs.len().max(node.outputs.len()) as f32;
    HEADER_HEIGHT + BODY_PADDING * 2.0 + ports * PORT_SPACING
}

fn input_position(node: &Node, index: usize) -> Point {
    Point::new(
        node.position.x,
        node.position.y + HEADER_HEIGHT + BODY_PADDING + index as f32 * PORT_SPACING,
    )
}

fn output_position(node: &Node, index: usize) -> Point {
    Point::new(
        node.position.x + NODE_WIDTH,
        node.position.y + HEADER_HEIGHT + BODY_PADDING + index as f32 * PORT_SPACING,
    )
}

fn to_screen(point: Point, bounds: Rectangle, pan: Vector, zoom: f32) -> Point {
    let _ = bounds;
    Point::new(pan.x + point.x * zoom, pan.y + point.y * zoom)
}

fn to_graph(point: Point, bounds: Rectangle, pan: Vector, zoom: f32) -> Point {
    let _ = bounds;
    Point::new((point.x - pan.x) / zoom, (point.y - pan.y) / zoom)
}

fn node_at(graph: Point, nodes: &[Node]) -> Option<NodeId> {
    nodes.iter().rev().find_map(|node| {
        let size = Size::new(NODE_WIDTH, node_height(node));
        (graph.x >= node.position.x
            && graph.x <= node.position.x + size.width
            && graph.y >= node.position.y
            && graph.y <= node.position.y + size.height)
            .then_some(node.id)
    })
}

fn output_at(
    screen: Point,
    nodes: &[Node],
    bounds: Rectangle,
    pan: Vector,
    zoom: f32,
) -> Option<(NodeId, usize)> {
    for node in nodes.iter().rev() {
        for (index, _) in node.outputs.iter().enumerate() {
            let position = to_screen(output_position(node, index), bounds, pan, zoom);
            if ((position.x - screen.x).powi(2) + (position.y - screen.y).powi(2)).sqrt()
                <= PORT_RADIUS + 5.0
            {
                return Some((node.id, index));
            }
        }
    }
    None
}

fn input_at(
    screen: Point,
    nodes: &[Node],
    bounds: Rectangle,
    pan: Vector,
    zoom: f32,
) -> Option<(NodeId, usize)> {
    for node in nodes.iter().rev() {
        for (index, _) in node.inputs.iter().enumerate() {
            let position = to_screen(input_position(node, index), bounds, pan, zoom);
            if ((position.x - screen.x).powi(2) + (position.y - screen.y).powi(2)).sqrt()
                <= PORT_RADIUS + 5.0
            {
                return Some((node.id, index));
            }
        }
    }
    None
}

fn bezier_point(from: Point, to: Point, t: f32) -> Point {
    let offset = ((to.x - from.x).abs() * 0.5).max(48.0);
    let c1 = Point::new(from.x + offset, from.y);
    let c2 = Point::new(to.x - offset, to.y);
    let u = 1.0 - t;
    Point::new(
        u * u * u * from.x + 3.0 * u * u * t * c1.x + 3.0 * u * t * t * c2.x + t * t * t * to.x,
        u * u * u * from.y + 3.0 * u * u * t * c1.y + 3.0 * u * t * t * c2.y + t * t * t * to.y,
    )
}

fn point_segment_distance(point: Point, a: Point, b: Point) -> f32 {
    let ab = b - a;
    let length_sq = ab.x * ab.x + ab.y * ab.y;
    if length_sq <= 1e-6 {
        return ((point.x - a.x).powi(2) + (point.y - a.y).powi(2)).sqrt();
    }
    let ap = point - a;
    let t = ((ap.x * ab.x + ap.y * ab.y) / length_sq).clamp(0.0, 1.0);
    {
        let closest = Point::new(a.x + ab.x * t, a.y + ab.y * t);
        ((point.x - closest.x).powi(2) + (point.y - closest.y).powi(2)).sqrt()
    }
}

fn link_at(
    screen: Point,
    nodes: &[Node],
    links: &[Link],
    bounds: Rectangle,
    pan: Vector,
    zoom: f32,
) -> Option<LinkKey> {
    let mut best: Option<(f32, LinkKey)> = None;
    for link in links {
        let from = nodes.iter().find(|node| node.id == link.from)?;
        let to = nodes.iter().find(|node| node.id == link.to)?;
        if link.input >= to.inputs.len() || link.output >= from.outputs.len() {
            continue;
        }
        let start = to_screen(output_position(from, link.output), bounds, pan, zoom);
        let end = to_screen(input_position(to, link.input), bounds, pan, zoom);
        let mut previous = start;
        for step in 1..=28 {
            let point = bezier_point(start, end, step as f32 / 28.0);
            let distance = point_segment_distance(screen, previous, point);
            if distance <= 6.0 && best.is_none_or(|(best, _)| distance < best) {
                best = Some((distance, (link.to, link.input)));
            }
            previous = point;
        }
    }
    best.map(|(_, key)| key)
}

fn bezier(from: Point, to: Point) -> Path {
    let offset = ((to.x - from.x).abs() * 0.5).max(48.0);
    Path::new(|builder| {
        builder.move_to(from);
        builder.bezier_curve_to(
            Point::new(from.x + offset, from.y),
            Point::new(to.x - offset, to.y),
            to,
        );
    })
}

fn draw_grid(frame: &mut Frame, bounds: Rectangle, pan: Vector, zoom: f32) {
    let spacing = 32.0 * zoom;
    if spacing < 9.0 {
        return;
    }
    let color = Color {
        a: 0.12,
        ..theme::BORDER
    };
    let start_x = (pan.x % spacing + bounds.width) % spacing;
    let start_y = (pan.y % spacing + bounds.height) % spacing;
    let mut x = start_x;
    while x < bounds.width {
        frame.fill(
            &Path::line(Point::new(x, 0.0), Point::new(x, bounds.height)),
            color,
        );
        x += spacing;
    }
    let mut y = start_y;
    while y < bounds.height {
        frame.fill(
            &Path::line(Point::new(0.0, y), Point::new(bounds.width, y)),
            color,
        );
        y += spacing;
    }
}

fn draw_link(frame: &mut Frame, from: Point, to: Point, color: Color, width: f32) {
    frame.stroke(
        &bezier(from, to),
        Stroke::default()
            .with_color(color)
            .with_width(width)
            .with_line_cap(LineCap::Round),
    );
}

struct Program<'a, Message> {
    view: View<'a>,
    on_event: std::rc::Rc<dyn Fn(Event) -> Message + 'a>,
}

#[derive(Default)]
struct State {
    drag: Option<Drag>,
    link_cursor: Option<Point>,
    last_wire_click: Option<(Instant, LinkKey)>,
}

enum Drag {
    Pan { last: Point },
    Node { id: NodeId, last: Point },
    Link { from: NodeId, output: usize },
}

impl<'a, Message: Clone + 'a> canvas::Program<Message> for Program<'a, Message> {
    type State = State;

    fn update(
        &self,
        state: &mut Self::State,
        event: &canvas::Event,
        bounds: Rectangle,
        cursor: mouse::Cursor,
    ) -> Option<canvas::Action<Message>> {
        let emit = |event| canvas::Action::publish((self.on_event)(event)).and_capture();
        let local = cursor
            .position()
            .map(|position| Point::new(position.x - bounds.x, position.y - bounds.y));
        let inside = cursor
            .position()
            .is_some_and(|position| bounds.contains(position));
        // Canvas widgets receive mouse events even when the cursor is over a
        // sibling widget in the same pane. Ignore presses outside the canvas
        // unless a drag is already in progress so property sliders/buttons
        // never start an empty-canvas pan or clear the node selection.
        if !inside && state.drag.is_none() {
            return None;
        }
        let graph = |point: Point| to_graph(point, bounds, self.view.pan, self.view.zoom);

        if let canvas::Event::Keyboard(iced::keyboard::Event::KeyPressed { key, .. }) = event {
            if matches!(
                key.as_ref(),
                iced::keyboard::Key::Named(
                    iced::keyboard::key::Named::Delete | iced::keyboard::key::Named::Backspace
                )
            ) {
                if let Some(link) = self.view.selected_link {
                    return Some(emit(Event::Detach(link)));
                }
            }
            return None;
        }

        match event {
            canvas::Event::Mouse(mouse::Event::ButtonPressed(mouse::Button::Left)) => {
                let cursor = local?;
                if let Some(Drag::Link { from, output }) = state.drag {
                    if let Some((to, input)) = input_at(
                        cursor,
                        &self.view.nodes,
                        bounds,
                        self.view.pan,
                        self.view.zoom,
                    ) {
                        state.drag = None;
                        state.link_cursor = None;
                        return Some(emit(Event::Connect {
                            from,
                            output,
                            to,
                            input,
                        }));
                    }
                    state.drag = None;
                    state.link_cursor = None;
                }
                if let Some((id, input)) = input_at(
                    cursor,
                    &self.view.nodes,
                    bounds,
                    self.view.pan,
                    self.view.zoom,
                ) {
                    let key = (id, input);
                    if let Some(link) = self
                        .view
                        .links
                        .iter()
                        .find(|link| link.to == id && link.input == input)
                        .copied()
                    {
                        state.drag = Some(Drag::Link {
                            from: link.from,
                            output: link.output,
                        });
                        state.link_cursor = Some(cursor);
                        return Some(emit(Event::Detach(key)));
                    }
                }
                if let Some((id, output)) = output_at(
                    cursor,
                    &self.view.nodes,
                    bounds,
                    self.view.pan,
                    self.view.zoom,
                ) {
                    state.drag = Some(Drag::Link { from: id, output });
                    state.link_cursor = Some(cursor);
                    return Some(emit(Event::Select(Some(id))));
                }
                let point = graph(cursor);
                if let Some(id) = node_at(point, &self.view.nodes) {
                    state.drag = Some(Drag::Node { id, last: point });
                    return Some(emit(Event::Select(Some(id))));
                }
                if let Some(key) = link_at(
                    cursor,
                    &self.view.nodes,
                    self.view.links,
                    bounds,
                    self.view.pan,
                    self.view.zoom,
                ) {
                    let now = Instant::now();
                    let double_click = state.last_wire_click.is_some_and(|(last, last_key)| {
                        last_key == key
                            && now.duration_since(last) < std::time::Duration::from_millis(350)
                    });
                    state.last_wire_click = Some((now, key));
                    if double_click {
                        return Some(emit(Event::InsertDot(key, point)));
                    }
                    return Some(emit(Event::SelectLink(Some(key))));
                }
                state.last_wire_click = None;
                state.drag = Some(Drag::Pan { last: cursor });
                return Some(emit(Event::Select(None)));
            }
            canvas::Event::Mouse(mouse::Event::ButtonPressed(mouse::Button::Right)) => {
                let cursor = local?;
                let point = graph(cursor);
                if let Some(id) = node_at(point, &self.view.nodes) {
                    return Some(emit(Event::Select(Some(id))));
                }
                if let Some(key) = link_at(
                    cursor,
                    &self.view.nodes,
                    self.view.links,
                    bounds,
                    self.view.pan,
                    self.view.zoom,
                ) {
                    return Some(emit(Event::Detach(key)));
                }
            }
            canvas::Event::Mouse(mouse::Event::CursorMoved { position }) => {
                let cursor = Point::new(position.x - bounds.x, position.y - bounds.y);
                match &mut state.drag {
                    Some(Drag::Pan { last }) => {
                        let delta = cursor - *last;
                        *last = cursor;
                        return Some(emit(Event::Viewport {
                            pan: self.view.pan + delta,
                            zoom: self.view.zoom,
                        }));
                    }
                    Some(Drag::Node { id, last }) => {
                        let point = graph(cursor);
                        let delta = point - *last;
                        *last = point;
                        return Some(emit(Event::MoveNode(*id, delta)));
                    }
                    Some(Drag::Link { .. }) => {
                        state.link_cursor = Some(cursor);
                        return Some(canvas::Action::request_redraw().and_capture());
                    }
                    None => {}
                }
            }
            canvas::Event::Mouse(mouse::Event::ButtonReleased(mouse::Button::Left)) => {
                if let Some(Drag::Link { from, output }) = state.drag {
                    if let Some(cursor) = local {
                        if let Some((to, input)) = input_at(
                            cursor,
                            &self.view.nodes,
                            bounds,
                            self.view.pan,
                            self.view.zoom,
                        ) {
                            state.drag = None;
                            state.link_cursor = None;
                            return Some(emit(Event::Connect {
                                from,
                                output,
                                to,
                                input,
                            }));
                        }
                    }
                }
                state.drag = None;
                state.link_cursor = None;
            }
            canvas::Event::Mouse(mouse::Event::WheelScrolled { delta }) => {
                let cursor = local?;
                let amount = match delta {
                    mouse::ScrollDelta::Lines { y, .. } => *y,
                    mouse::ScrollDelta::Pixels { y, .. } => *y * 0.05,
                };
                if amount.abs() < f32::EPSILON {
                    return None;
                }
                let factor = (amount * 0.12).exp();
                let zoom = (self.view.zoom * factor).clamp(MIN_ZOOM, MAX_ZOOM);
                if (zoom - self.view.zoom).abs() < f32::EPSILON {
                    return None;
                }
                let before = to_graph(cursor, bounds, self.view.pan, self.view.zoom);
                let pan = Vector::new(cursor.x - before.x * zoom, cursor.y - before.y * zoom);
                return Some(emit(Event::Viewport { pan, zoom }));
            }
            _ => {}
        }
        None
    }

    fn draw(
        &self,
        state: &Self::State,
        renderer: &iced::Renderer,
        _theme: &iced::Theme,
        bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> Vec<canvas::Geometry> {
        let mut frame = Frame::new(renderer, bounds.size());
        frame.fill_rectangle(Point::ORIGIN, bounds.size(), theme::SURFACE);
        draw_grid(&mut frame, bounds, self.view.pan, self.view.zoom);

        for link in self.view.links {
            let Some(from) = self.view.nodes.iter().find(|node| node.id == link.from) else {
                continue;
            };
            let Some(to) = self.view.nodes.iter().find(|node| node.id == link.to) else {
                continue;
            };
            let Some(output) = from.outputs.get(link.output) else {
                continue;
            };
            if link.input >= to.inputs.len() {
                continue;
            }
            let start = to_screen(
                output_position(from, link.output),
                bounds,
                self.view.pan,
                self.view.zoom,
            );
            let end = to_screen(
                input_position(to, link.input),
                bounds,
                self.view.pan,
                self.view.zoom,
            );
            let selected = self.view.selected_link == Some((link.to, link.input));
            draw_link(
                &mut frame,
                start,
                end,
                if selected {
                    theme::TEXT
                } else {
                    output.kind.color()
                },
                if selected { 3.5 } else { 2.0 },
            );
        }

        if let Some(Drag::Link { from, output }) = state.drag {
            if let (Some(node), Some(cursor)) = (
                self.view.nodes.iter().find(|node| node.id == from),
                state.link_cursor,
            ) {
                if let Some(port) = node.outputs.get(output) {
                    let start = to_screen(
                        output_position(node, output),
                        bounds,
                        self.view.pan,
                        self.view.zoom,
                    );
                    draw_link(&mut frame, start, cursor, port.kind.color(), 2.0);
                }
            }
        }

        for node in &self.view.nodes {
            let top_left = to_screen(node.position, bounds, self.view.pan, self.view.zoom);
            let size = Size::new(
                NODE_WIDTH * self.view.zoom,
                node_height(node) * self.view.zoom,
            );
            let card = Path::rounded_rectangle(top_left, size, (7.0 * self.view.zoom).into());
            frame.fill(&card, theme::PANEL_ALT);
            let border = if node.selected {
                theme::ACCENT
            } else if node.output {
                theme::SUCCESS
            } else {
                theme::BORDER_STRONG
            };
            frame.stroke(
                &card,
                Stroke::default()
                    .with_color(border)
                    .with_width(if node.selected { 2.0 } else { 1.0 }),
            );
            let header_height = HEADER_HEIGHT * self.view.zoom;
            let header = Path::rectangle(top_left, Size::new(size.width, header_height));
            frame.fill(&header, node.accent);

            let title_size = 11.0 * self.view.zoom;
            frame.fill_text(Text {
                content: node.title.clone(),
                position: Point::new(
                    top_left.x + 7.0 * self.view.zoom,
                    top_left.y + header_height * 0.5,
                ),
                color: theme::TEXT,
                size: title_size.into(),
                align_y: iced::alignment::Vertical::Center,
                ..Text::default()
            });
            if !node.subtitle.is_empty() {
                frame.fill_text(Text {
                    content: node.subtitle.clone(),
                    position: Point::new(
                        top_left.x + size.width - 7.0 * self.view.zoom,
                        top_left.y + header_height * 0.5,
                    ),
                    color: Color {
                        a: 0.85,
                        ..theme::TEXT
                    },
                    size: (8.5 * self.view.zoom).into(),
                    align_x: iced::alignment::Horizontal::Right.into(),
                    align_y: iced::alignment::Vertical::Center,
                    ..Text::default()
                });
            }

            for (index, port) in node.inputs.iter().enumerate() {
                let position = to_screen(
                    input_position(node, index),
                    bounds,
                    self.view.pan,
                    self.view.zoom,
                );
                frame.fill(
                    &Path::circle(position, PORT_RADIUS * self.view.zoom),
                    port.kind.color(),
                );
                frame.fill_text(Text {
                    content: port.label.clone(),
                    position: Point::new(position.x + 8.0 * self.view.zoom, position.y),
                    color: theme::TEXT_MUTED,
                    size: (8.5 * self.view.zoom).into(),
                    align_y: iced::alignment::Vertical::Center,
                    ..Text::default()
                });
            }
            for (index, port) in node.outputs.iter().enumerate() {
                let position = to_screen(
                    output_position(node, index),
                    bounds,
                    self.view.pan,
                    self.view.zoom,
                );
                frame.fill(
                    &Path::circle(position, PORT_RADIUS * self.view.zoom),
                    port.kind.color(),
                );
                frame.fill_text(Text {
                    content: port.label.clone(),
                    position: Point::new(position.x - 8.0 * self.view.zoom, position.y),
                    color: theme::TEXT_MUTED,
                    size: (8.5 * self.view.zoom).into(),
                    align_x: iced::alignment::Horizontal::Right.into(),
                    align_y: iced::alignment::Vertical::Center,
                    ..Text::default()
                });
            }
        }

        vec![frame.into_geometry()]
    }
}

pub fn view<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    view: View<'a>,
    on_event: impl Fn(Event) -> Message + 'a,
) -> Element<'a, Message> {
    iced::widget::container(
        Canvas::new(Program {
            view,
            on_event: std::rc::Rc::new(on_event),
        })
        .width(Length::Fill)
        .height(Length::Fill),
    )
    .id(id.into())
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn graph_and_screen_round_trip() {
        let bounds = Rectangle::new(Point::new(10.0, 20.0), Size::new(400.0, 300.0));
        let pan = Vector::new(-40.0, 25.0);
        let zoom = 1.35;
        let point = Point::new(120.0, -60.0);
        let screen = to_screen(point, bounds, pan, zoom);
        let back = to_graph(screen, bounds, pan, zoom);
        assert!((back.x - point.x).abs() < 0.001);
        assert!((back.y - point.y).abs() < 0.001);
    }

    #[test]
    fn wires_are_hit_near_the_curve() {
        let source = Node {
            id: 1,
            title: "Noise".to_owned(),
            subtitle: String::new(),
            position: Point::new(0.0, 0.0),
            inputs: Vec::new(),
            outputs: vec![Port {
                label: "Color".to_owned(),
                kind: PortKind::Color,
            }],
            accent: theme::ACCENT,
            selected: false,
            output: false,
        };
        let target = Node {
            id: 2,
            title: "Output".to_owned(),
            subtitle: String::new(),
            position: Point::new(260.0, 0.0),
            inputs: vec![Port {
                label: "Color".to_owned(),
                kind: PortKind::Color,
            }],
            outputs: Vec::new(),
            accent: theme::SUCCESS,
            selected: false,
            output: true,
        };
        let nodes = [source, target];
        let links = [Link {
            from: 1,
            output: 0,
            to: 2,
            input: 0,
        }];
        let bounds = Rectangle::new(Point::ORIGIN, Size::new(600.0, 400.0));
        let start = to_screen(
            output_position(&nodes[0], 0),
            bounds,
            Vector::new(0.0, 0.0),
            1.0,
        );
        let end = to_screen(
            input_position(&nodes[1], 0),
            bounds,
            Vector::new(0.0, 0.0),
            1.0,
        );
        let middle = bezier_point(start, end, 0.5);
        assert_eq!(
            link_at(middle, &nodes, &links, bounds, Vector::new(0.0, 0.0), 1.0),
            Some((2, 0))
        );
        assert_eq!(
            link_at(
                Point::new(100.0, 300.0),
                &nodes,
                &links,
                bounds,
                Vector::new(0.0, 0.0),
                1.0
            ),
            None
        );
    }

    #[test]
    fn presses_outside_the_canvas_do_not_start_a_pan_or_clear_selection() {
        let links = [];
        let program = Program {
            view: View {
                nodes: Vec::new(),
                links: &links,
                pan: Vector::new(0.0, 0.0),
                zoom: 1.0,
                selected_link: None,
            },
            on_event: std::rc::Rc::new(|event| event),
        };
        let bounds = Rectangle::new(Point::ORIGIN, Size::new(500.0, 400.0));
        let mut state = State::default();
        let event = canvas::Event::Mouse(mouse::Event::ButtonPressed(mouse::Button::Left));
        assert!(canvas::Program::update(
            &program,
            &mut state,
            &event,
            bounds,
            mouse::Cursor::Available(Point::new(900.0, 200.0)),
        )
        .is_none());
        assert!(state.drag.is_none());
    }

    #[test]
    fn output_ports_are_hit_at_their_position() {
        let node = Node {
            id: 7,
            title: "Noise".to_owned(),
            subtitle: String::new(),
            position: Point::new(20.0, 30.0),
            inputs: Vec::new(),
            outputs: vec![Port {
                label: "Color".to_owned(),
                kind: PortKind::Color,
            }],
            accent: theme::ACCENT,
            selected: false,
            output: false,
        };
        let nodes = [node];
        let bounds = Rectangle::new(Point::ORIGIN, Size::new(500.0, 400.0));
        let position = to_screen(
            output_position(&nodes[0], 0),
            bounds,
            Vector::new(0.0, 0.0),
            1.0,
        );
        assert_eq!(
            output_at(position, &nodes, bounds, Vector::new(0.0, 0.0), 1.0),
            Some((7, 0))
        );
    }
}
