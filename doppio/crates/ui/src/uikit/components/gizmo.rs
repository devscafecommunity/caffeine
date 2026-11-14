use iced::widget::{button, row};
use iced::Element;

/// Axis actions for transform editing. The host maps these to nudge/set
/// commands; the component stays independent from engine state.
pub fn axes<'a, Message: Clone + 'a>(x: Message, y: Message, z: Message) -> Element<'a, Message> {
    row![
        button("X").style(iced::widget::button::danger).on_press(x),
        button("Y").style(iced::widget::button::success).on_press(y),
        button("Z").style(iced::widget::button::primary).on_press(z),
    ]
    .spacing(4)
    .into()
}
