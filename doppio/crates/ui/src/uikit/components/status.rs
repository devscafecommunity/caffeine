use crate::uikit::theme::{self, ChipTone};
use iced::widget::{container, text};
use iced::Element;

pub fn chip<'a, Message: 'a>(label: impl Into<String>, tone: ChipTone) -> Element<'a, Message> {
    let label = label.into();
    container(text(label).size(11))
        .padding([3, 8])
        .style(theme::chip(tone))
        .into()
}
