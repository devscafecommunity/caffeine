//! Docking guides shown over a pane while a tab is being dragged.
//!
//! The overlay owns hit-testing only: it reports which [`Zone`] the cursor is
//! over and which zone a release landed on. The window decides what docking
//! into that zone means (merge into the pane's tab group, or split it).

use crate::uikit::theme;
use iced::widget::canvas::{self, Frame, Path, Stroke, Text};
use iced::widget::Canvas;
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Zone {
    /// Add the dragged tab to this pane's tab group.
    Center,
    /// Split the pane and dock the tab on that side.
    Left,
    Right,
    Top,
    Bottom,
}

const BUTTON: f32 = 30.0;
const GAP: f32 = 6.0;
/// Fraction of the pane edge that also counts as a split zone.
const EDGE: f32 = 0.22;

/// Centre of each compass button relative to the pane centre.
fn compass(bounds: Rectangle) -> [(Zone, Rectangle); 5] {
    let cx = bounds.width / 2.0;
    let cy = bounds.height / 2.0;
    let step = BUTTON + GAP;
    let rect = |dx: f32, dy: f32| {
        Rectangle::new(
            Point::new(cx + dx - BUTTON / 2.0, cy + dy - BUTTON / 2.0),
            Size::new(BUTTON, BUTTON),
        )
    };
    [
        (Zone::Center, rect(0.0, 0.0)),
        (Zone::Left, rect(-step, 0.0)),
        (Zone::Right, rect(step, 0.0)),
        (Zone::Top, rect(0.0, -step)),
        (Zone::Bottom, rect(0.0, step)),
    ]
}

/// Zone under `point` (pane-local coordinates): compass buttons win, then the
/// outer edge bands, otherwise the whole body means "add as a tab".
pub fn zone_at(size: Size, point: Point) -> Zone {
    let bounds = Rectangle::new(Point::ORIGIN, size);
    for (zone, rect) in compass(bounds) {
        if rect.contains(point) {
            return zone;
        }
    }
    let left = point.x / size.width.max(1.0);
    let right = 1.0 - left;
    let top = point.y / size.height.max(1.0);
    let bottom = 1.0 - top;
    let nearest = left.min(right).min(top).min(bottom);
    if nearest > EDGE {
        return Zone::Center;
    }
    if nearest == left {
        Zone::Left
    } else if nearest == right {
        Zone::Right
    } else if nearest == top {
        Zone::Top
    } else {
        Zone::Bottom
    }
}

/// Where the dragged tab would end up if dropped in `zone`.
fn preview(size: Size, zone: Zone) -> Rectangle {
    let (w, h) = (size.width, size.height);
    match zone {
        Zone::Center => Rectangle::new(Point::ORIGIN, size),
        Zone::Left => Rectangle::new(Point::ORIGIN, Size::new(w / 2.0, h)),
        Zone::Right => Rectangle::new(Point::new(w / 2.0, 0.0), Size::new(w / 2.0, h)),
        Zone::Top => Rectangle::new(Point::ORIGIN, Size::new(w, h / 2.0)),
        Zone::Bottom => Rectangle::new(Point::new(0.0, h / 2.0), Size::new(w, h / 2.0)),
    }
}

struct Program<'a, Message> {
    hovered: Option<Zone>,
    on_hover: Box<dyn Fn(Option<Zone>) -> Message + 'a>,
    on_drop: Box<dyn Fn(Zone) -> Message + 'a>,
}

impl<'a, Message: Clone + 'a> canvas::Program<Message> for Program<'a, Message> {
    type State = ();

    fn update(
        &self,
        _state: &mut Self::State,
        event: &canvas::Event,
        bounds: Rectangle,
        cursor: mouse::Cursor,
    ) -> Option<canvas::Action<Message>> {
        let local = cursor
            .position()
            .filter(|position| bounds.contains(*position))
            .map(|position| Point::new(position.x - bounds.x, position.y - bounds.y));
        match event {
            canvas::Event::Mouse(mouse::Event::CursorMoved { .. }) => {
                let zone = local.map(|point| zone_at(bounds.size(), point));
                if zone != self.hovered {
                    return Some(canvas::Action::publish((self.on_hover)(zone)));
                }
                None
            }
            canvas::Event::Mouse(mouse::Event::ButtonReleased(mouse::Button::Left)) => {
                let point = local?;
                let zone = zone_at(bounds.size(), point);
                Some(canvas::Action::publish((self.on_drop)(zone)).and_capture())
            }
            _ => None,
        }
    }

    fn draw(
        &self,
        _state: &Self::State,
        renderer: &iced::Renderer,
        _theme: &iced::Theme,
        bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> Vec<canvas::Geometry> {
        let mut frame = Frame::new(renderer, bounds.size());
        frame.fill_rectangle(
            Point::ORIGIN,
            bounds.size(),
            Color {
                a: 0.28,
                ..theme::BACKGROUND
            },
        );
        if let Some(zone) = self.hovered {
            let rect = preview(bounds.size(), zone);
            let path = Path::rectangle(rect.position(), rect.size());
            frame.fill(
                &path,
                Color {
                    a: 0.30,
                    ..theme::ACCENT
                },
            );
            frame.stroke(
                &path,
                Stroke::default().with_color(theme::ACCENT).with_width(2.0),
            );
        }
        for (zone, rect) in compass(Rectangle::new(Point::ORIGIN, bounds.size())) {
            let active = self.hovered == Some(zone);
            let card = Path::rounded_rectangle(rect.position(), rect.size(), 4.0.into());
            frame.fill(
                &card,
                if active {
                    theme::ACCENT_PRESSED
                } else {
                    theme::PANEL_ALT
                },
            );
            frame.stroke(
                &card,
                Stroke::default()
                    .with_color(if active {
                        theme::ACCENT
                    } else {
                        theme::BORDER_STRONG
                    })
                    .with_width(1.0),
            );
            // Mini diagram of where the tab lands inside a pane.
            let inner = Rectangle::new(
                Point::new(rect.x + 6.0, rect.y + 6.0),
                Size::new(BUTTON - 12.0, BUTTON - 12.0),
            );
            frame.stroke(
                &Path::rectangle(inner.position(), inner.size()),
                Stroke::default()
                    .with_color(theme::TEXT_MUTED)
                    .with_width(1.0),
            );
            let fill = preview(inner.size(), zone);
            frame.fill_rectangle(
                Point::new(inner.x + fill.x, inner.y + fill.y),
                fill.size(),
                if active { theme::TEXT } else { theme::ACCENT },
            );
        }
        let caption = match self.hovered {
            Some(Zone::Center) => "Add as tab",
            Some(Zone::Left) => "Dock left",
            Some(Zone::Right) => "Dock right",
            Some(Zone::Top) => "Dock above",
            Some(Zone::Bottom) => "Dock below",
            None => "Drop on a guide to dock",
        };
        frame.fill_text(Text {
            content: caption.to_owned(),
            position: Point::new(
                bounds.width / 2.0,
                bounds.height / 2.0 + BUTTON * 2.0 + 10.0,
            ),
            color: theme::TEXT,
            size: 12.0.into(),
            align_x: iced::alignment::Horizontal::Center.into(),
            ..Text::default()
        });
        vec![frame.into_geometry()]
    }

    fn mouse_interaction(
        &self,
        _state: &Self::State,
        _bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> mouse::Interaction {
        mouse::Interaction::Grabbing
    }
}

pub fn view<'a, Message: Clone + 'a>(
    hovered: Option<Zone>,
    on_hover: impl Fn(Option<Zone>) -> Message + 'a,
    on_drop: impl Fn(Zone) -> Message + 'a,
) -> Element<'a, Message> {
    Canvas::new(Program {
        hovered,
        on_hover: Box::new(on_hover),
        on_drop: Box::new(on_drop),
    })
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compass_buttons_and_edges_pick_the_expected_zone() {
        let size = Size::new(400.0, 300.0);
        assert_eq!(zone_at(size, Point::new(200.0, 150.0)), Zone::Center);
        assert_eq!(zone_at(size, Point::new(200.0 - 36.0, 150.0)), Zone::Left);
        assert_eq!(zone_at(size, Point::new(200.0 + 36.0, 150.0)), Zone::Right);
        assert_eq!(zone_at(size, Point::new(200.0, 150.0 - 36.0)), Zone::Top);
        assert_eq!(zone_at(size, Point::new(200.0, 150.0 + 36.0)), Zone::Bottom);
        // Outer bands dock without aiming at the compass.
        assert_eq!(zone_at(size, Point::new(10.0, 150.0)), Zone::Left);
        assert_eq!(zone_at(size, Point::new(395.0, 150.0)), Zone::Right);
        assert_eq!(zone_at(size, Point::new(120.0, 5.0)), Zone::Top);
        assert_eq!(zone_at(size, Point::new(120.0, 295.0)), Zone::Bottom);
        // Everything else is "add as tab".
        assert_eq!(zone_at(size, Point::new(120.0, 100.0)), Zone::Center);
    }
}
