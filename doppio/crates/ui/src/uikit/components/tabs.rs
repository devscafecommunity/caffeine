use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{button, container, mouse_area, row, text};
use iced::Element;

pub struct Tab<'a, Message> {
    pub id: UiId,
    pub label: &'a str,
    pub active: bool,
    pub hovered: bool,
    pub drop_target: bool,
    pub on_press: Message,
    pub on_release: Message,
    pub on_enter: Message,
    pub on_exit: Message,
}

/// A compact tab strip for a pane that hosts several panels. Each tab is a
/// mouse area so it can be selected with a click and moved independently.
pub fn view<'a, Message: Clone + 'a>(
    tabs: Vec<Tab<'a, Message>>,
    subtitle: Option<String>,
    split: Option<(UiId, Message)>,
) -> Element<'a, Message> {
    let mut strip = row![].spacing(2).align_y(iced::Alignment::Center);
    for tab in tabs {
        strip = strip.push(
            mouse_area(
                container(text(tab.label).size(11))
                    .id(tab.id)
                    .padding([3, 8])
                    .style(theme::tab_item(tab.active, tab.hovered, tab.drop_target)),
            )
            .on_press(tab.on_press)
            .on_release(tab.on_release)
            .on_enter(tab.on_enter)
            .on_exit(tab.on_exit)
            .interaction(iced::mouse::Interaction::Pointer),
        );
    }
    if let Some(subtitle) = subtitle {
        // Keep the strip narrow: the remaining title-bar space is PaneGrid's
        // pick area and must stay free so panels can be dragged.
        strip = strip
            .push(text("·").size(9).color(theme::TEXT_MUTED))
            .push(text(subtitle).size(9).color(theme::TEXT_MUTED));
    }
    if let Some((id, message)) = split {
        strip = strip.push(
            container(
                button(text("◫").size(11))
                    .style(theme::button_text)
                    .padding([2, 5])
                    .on_press(message),
            )
            .id(id),
        );
    }
    container(strip).padding([1, 4]).into()
}
