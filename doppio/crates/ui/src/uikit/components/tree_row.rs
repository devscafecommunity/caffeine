use crate::uikit::id::{self, UiId};
use crate::uikit::theme;
use iced::widget::{button, container, mouse_area, row, space, text};
use iced::{Element, Length};

pub struct Row<Message> {
    pub id: UiId,
    pub label: String,
    pub depth: usize,
    pub has_children: bool,
    pub expanded: bool,
    pub selected: bool,
    pub on_toggle: Message,
    pub on_select: Message,
    pub on_context: Message,
    pub on_double_click: Message,
}

/// One hierarchy row with indentation and an expand/collapse chevron.
pub fn view<'a, Message: Clone + 'a>(row: Row<Message>) -> Element<'a, Message> {
    let chevron: Element<'a, Message> = if row.has_children {
        container(
            button(text(if row.expanded { "▾" } else { "▸" }).size(11))
                .style(theme::button_text)
                .padding([0, 4])
                .on_press(row.on_toggle),
        )
        .id(id::tree_toggle(row.id.key()))
        .into()
    } else {
        space().width(Length::Fixed(16.0)).into()
    };

    let content = container(
        row![
            space().width(Length::Fixed((row.depth as f32) * 14.0)),
            chevron,
            text(row.label).size(12),
        ]
        .spacing(4)
        .align_y(iced::Alignment::Center),
    )
    .id(row.id)
    .padding([4, 6])
    .width(Length::Fill)
    .style(theme::list_row(row.selected));

    mouse_area(content)
        .on_press(row.on_select)
        .on_right_press(row.on_context)
        .on_double_click(row.on_double_click)
        .interaction(iced::mouse::Interaction::Pointer)
        .into()
}
