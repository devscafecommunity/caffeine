//! A layer of a terrain's palette: a swatch of its tint, its name and how much of the terrain it covers.

use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{container, mouse_area, row, space, text};
use iced::{Color, Element, Length};

/// One row. `tint` is the layer's colour (0..1 per channel), `detail` the right-hand note (`12 %`, `locked`).
pub fn view<'a, Message: Clone + 'a>(
    id: UiId,
    index: usize,
    name: &str,
    tint: [f32; 3],
    detail: &str,
    selected: bool,
    on_select: Message,
) -> Element<'a, Message> {
    let swatch = container(space::horizontal())
        .width(Length::Fixed(16.0))
        .height(Length::Fixed(16.0))
        .style(theme::swatch(Color {
            r: tint[0],
            g: tint[1],
            b: tint[2],
            a: 1.0,
        }));
    let content = container(
        row![
            text(format!("{index}"))
                .size(9)
                .color(theme::TEXT_MUTED)
                .width(Length::Fixed(16.0)),
            swatch,
            text(name.to_owned()).size(12),
            space::horizontal(),
            text(detail.to_owned()).size(9).color(theme::TEXT_MUTED),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .id(id)
    .padding([4, 8])
    .width(Length::Fill)
    .style(theme::list_row(selected));
    mouse_area(content)
        .on_press(on_select)
        .interaction(iced::mouse::Interaction::Pointer)
        .into()
}
