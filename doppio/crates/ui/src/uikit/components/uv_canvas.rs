//! Reusable UV/tile painting canvas for texture sets.

use crate::uikit::theme;
use iced::widget::canvas::{self, Frame, Geometry, Image, Path, Program, Stroke};
use iced::widget::{container, Canvas};
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Event {
    PaintStart([f32; 2]),
    PaintMove([f32; 2]),
    PaintEnd,
    Hover(Option<[f32; 2]>),
}

pub struct View<'a> {
    pub image: Option<&'a iced::widget::image::Handle>,
    pub tile: u32,
    pub tile_size: u32,
    pub brush_radius: f32,
    pub brush_color: Color,
    pub painting: bool,
    pub cursor: Option<[f32; 2]>,
}

pub fn view<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    view: View<'a>,
    on_event: impl Fn(Event) -> Message + 'a,
) -> Element<'a, Message> {
    container(
        Canvas::new(ProgramView {
            view,
            on_event: std::rc::Rc::new(on_event),
        })
        .width(Length::Fill)
        .height(Length::Fill),
    )
    .id(id)
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

struct ProgramView<'a, Message> {
    view: View<'a>,
    on_event: std::rc::Rc<dyn Fn(Event) -> Message + 'a>,
}

impl<'a, Message: Clone + 'a> Program<Message> for ProgramView<'a, Message> {
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
            .map(|position| Point::new(position.x - bounds.x, position.y - bounds.y));
        let uv = local.and_then(|point| to_uv(point, bounds));
        if let canvas::Event::Mouse(iced::mouse::Event::CursorMoved { .. }) = event {
            return Some(canvas::Action::publish((self.on_event)(Event::Hover(uv))));
        }
        match event {
            canvas::Event::Mouse(iced::mouse::Event::ButtonPressed(mouse::Button::Left)) => {
                let uv = uv?;
                Some(canvas::Action::publish((self.on_event)(Event::PaintStart(uv))).and_capture())
            }
            canvas::Event::Mouse(iced::mouse::Event::CursorMoved { .. }) => {
                if self.view.painting {
                    let uv = uv?;
                    Some(
                        canvas::Action::publish((self.on_event)(Event::PaintMove(uv)))
                            .and_capture(),
                    )
                } else {
                    None
                }
            }
            canvas::Event::Mouse(iced::mouse::Event::ButtonReleased(mouse::Button::Left)) => {
                if self.view.painting {
                    Some(canvas::Action::publish((self.on_event)(Event::PaintEnd)).and_capture())
                } else {
                    None
                }
            }
            _ => None,
        }
    }

    fn draw(
        &self,
        _state: &Self::State,
        _renderer: &iced::Renderer,
        _theme: &iced::Theme,
        bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> Vec<Geometry> {
        let mut frame = Frame::new(_renderer, bounds.size());
        let size = (bounds.width.min(bounds.height) - 24.0).max(32.0);
        let origin = Point::new((bounds.width - size) * 0.5, (bounds.height - size) * 0.5);
        let image_bounds = Rectangle::new(origin, Size::new(size, size));
        frame.fill_rectangle(Point::ORIGIN, bounds.size(), theme::SURFACE);
        if let Some(handle) = self.view.image {
            let image = Image::new(handle.clone());
            frame.draw_image(image_bounds, image);
        } else {
            frame.fill_rectangle(
                image_bounds.position(),
                image_bounds.size(),
                theme::PANEL_ALT,
            );
        }
        frame.stroke(
            &Path::rectangle(image_bounds.position(), image_bounds.size()),
            Stroke::default()
                .with_color(theme::BORDER_STRONG)
                .with_width(1.0),
        );
        for step in 1..4 {
            let fraction = step as f32 / 4.0;
            let x = image_bounds.x + image_bounds.width * fraction;
            let y = image_bounds.y + image_bounds.height * fraction;
            let color = Color {
                a: 0.12,
                ..theme::TEXT
            };
            frame.stroke(
                &Path::line(
                    Point::new(x, image_bounds.y),
                    Point::new(x, image_bounds.y + image_bounds.height),
                ),
                Stroke::default().with_color(color),
            );
            frame.stroke(
                &Path::line(
                    Point::new(image_bounds.x, y),
                    Point::new(image_bounds.x + image_bounds.width, y),
                ),
                Stroke::default().with_color(color),
            );
        }
        if let Some(uv) = self.view.cursor {
            let point = Point::new(
                image_bounds.x + uv[0] * image_bounds.width,
                image_bounds.y + (1.0 - uv[1]) * image_bounds.height,
            );
            let radius = self.view.brush_radius.max(1.0) / self.view.tile_size.max(1) as f32
                * image_bounds.width;
            frame.stroke(
                &Path::circle(point, radius.max(2.0)),
                Stroke::default()
                    .with_color(self.view.brush_color)
                    .with_width(1.5),
            );
        }
        vec![frame.into_geometry()]
    }
}

fn to_uv(point: Point, bounds: Rectangle) -> Option<[f32; 2]> {
    let size = (bounds.width.min(bounds.height) - 24.0).max(32.0);
    let origin = Point::new((bounds.width - size) * 0.5, (bounds.height - size) * 0.5);
    let x = (point.x - origin.x) / size;
    let y = (point.y - origin.y) / size;
    if (0.0..=1.0).contains(&x) && (0.0..=1.0).contains(&y) {
        Some([x, 1.0 - y])
    } else {
        None
    }
}
