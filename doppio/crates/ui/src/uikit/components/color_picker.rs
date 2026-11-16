use crate::uikit::components::slider;
use crate::uikit::theme;
use iced::widget::{column, container, row, space, text};
use iced::Element;

/// RGBA colour editor: a swatch plus one slider per channel.
pub fn view<'a, Message: Clone + 'a>(
    label: &'a str,
    value: [f32; 4],
    id_for_axis: impl Fn(usize) -> iced::widget::Id + 'a,
    on_change: impl Fn(usize, f32) -> Message + 'a,
) -> Element<'a, Message> {
    let swatch = container(space())
        .width(34.0)
        .height(34.0)
        .style(theme::swatch(theme::rgba(value)));

    let on_change = std::rc::Rc::new(on_change);
    let id_for_axis = std::rc::Rc::new(id_for_axis);
    let header = row![text(label).size(11).width(iced::Length::Fill), swatch]
        .align_y(iced::Alignment::Center);

    let mut channels = column![].spacing(1);
    let labels = ["R", "G", "B", "A"];
    for axis in 0..4 {
        let change = on_change.clone();
        let ids = id_for_axis.clone();
        channels = channels.push(slider::compact(
            ids(axis),
            labels[axis],
            0.0..=1.0,
            value[axis],
            move |value| change(axis, value),
        ));
    }

    column![header, channels].spacing(3).into()
}
