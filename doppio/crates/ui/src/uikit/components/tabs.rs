use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{button, container, row, space, text};
use iced::Element;

pub struct Tab<'a, Message> {
    pub id: UiId,
    pub label: &'a str,
    pub active: bool,
    pub message: Message,
}

/// A compact tab strip for a pane that hosts several panels.
pub fn view<'a, Message: Clone + 'a>(
    tabs: Vec<Tab<'a, Message>>,
    subtitle: Option<String>,
) -> Element<'a, Message> {
    let mut strip = row![].spacing(2).align_y(iced::Alignment::Center);
    for tab in tabs {
        strip = strip.push(
            container(
                button(text(tab.label).size(11))
                    .style(theme::tab(tab.active))
                    .padding([3, 8])
                    .on_press(tab.message),
            )
            .id(tab.id),
        );
    }
    if let Some(subtitle) = subtitle {
        strip = strip
            .push(space::horizontal())
            .push(text(subtitle).size(9).color(theme::TEXT_MUTED));
    }
    container(strip).padding([1, 4]).into()
}
