use crate::uikit::components::scroll;
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
    _subtitle: Option<String>,
    _split: Option<(UiId, Message)>,
) -> Element<'a, Message> {
    let mut strip = row![].spacing(1).align_y(iced::Alignment::Center);
    for tab in tabs {
        strip = strip.push(
            mouse_area(
                // The outer container's 2 px bottom padding is the indicator.
                container(
                    container(text(tab.label).size(12))
                        .id(tab.id)
                        .padding([4, 10])
                        .style(theme::tab_item(tab.active, tab.hovered, tab.drop_target)),
                )
                .padding(iced::Padding {
                    bottom: 2.0,
                    ..iced::Padding::ZERO
                })
                .style(theme::tab_indicator(tab.active)),
            )
            .on_press(tab.on_press)
            .on_release(tab.on_release)
            .on_enter(tab.on_enter)
            .on_exit(tab.on_exit)
            .interaction(iced::mouse::Interaction::Pointer),
        );
    }
    container(scroll::tab_strip(strip)).padding([1, 4]).into()
}

/// Right-aligned pane controls: detach the active page into its own window, split it out,
/// maximise/restore.
pub fn controls<'a, Message: Clone + 'a>(
    detach: Option<(UiId, Message)>,
    split: Option<(UiId, Message)>,
    maximize: (UiId, bool, Message),
) -> Element<'a, Message> {
    let mut bar = row![].spacing(2).align_y(iced::Alignment::Center);
    if let Some((id, message)) = detach {
        bar = bar.push(
            container(
                button(text("⧉").size(12))
                    .style(theme::button_text)
                    .padding([2, 6])
                    .on_press(message),
            )
            .id(id),
        );
    }
    if let Some((id, message)) = split {
        bar = bar.push(
            container(
                button(text("◫").size(12))
                    .style(theme::button_text)
                    .padding([2, 6])
                    .on_press(message),
            )
            .id(id),
        );
    }
    let (id, maximized, message) = maximize;
    bar = bar.push(
        container(
            button(text(if maximized { "❐" } else { "▢" }).size(12))
                .style(theme::button_text)
                .padding([2, 6])
                .on_press(message),
        )
        .id(id),
    );
    container(bar).padding([1, 4]).into()
}

/// One open document tab (title + close), styled like the pane tabs.
pub fn document<'a, Message: Clone + 'a>(
    id: UiId,
    close_id: UiId,
    label: String,
    active: bool,
    on_select: Message,
    on_close: Message,
) -> Element<'a, Message> {
    let title = mouse_area(container(text(label).size(12)).id(id).padding([4, 4]))
        .on_press(on_select)
        .interaction(iced::mouse::Interaction::Pointer);
    let close = container(
        button(text("×").size(12))
            .style(theme::button_text)
            .padding([0, 4])
            .on_press(on_close),
    )
    .id(close_id);
    container(
        container(
            row![title, close]
                .spacing(2)
                .align_y(iced::Alignment::Center),
        )
        .padding([0, 6])
        .style(theme::tab_item(active, false, false)),
    )
    .padding(iced::Padding {
        bottom: 2.0,
        ..iced::Padding::ZERO
    })
    .style(theme::tab_indicator(active))
    .into()
}
