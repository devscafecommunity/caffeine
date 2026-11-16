use crate::uikit::icons::Icon;
use crate::uikit::id::{self, UiId};
use crate::uikit::theme;
use iced::widget::{button, container, mouse_area, row, space, text, Column};
use iced::{Element, Length};

/// One flattened tree row. Panels build these from their own data; the tree
/// view itself only knows about depth, expansion and selection.
pub struct Item<'a, Message> {
    pub id: UiId,
    pub label: String,
    pub icon: Icon,
    pub depth: usize,
    pub has_children: bool,
    pub expanded: bool,
    pub selected: bool,
    pub hovered: bool,
    pub drop_target: bool,
    pub trailing: Option<Element<'a, Message>>,
    /// Rendered immediately below the row (context menus, inline editors).
    pub extra: Option<Element<'a, Message>>,
    pub on_toggle: Message,
    pub on_press: Message,
    pub on_release: Message,
    pub on_enter: Message,
    pub on_exit: Message,
    pub on_context: Message,
}

/// A compact, flat tree view. Indentation and chevrons carry the hierarchy;
/// rows share the pane width and highlight like a native tree, without the
/// card-per-row look.
pub fn view<'a, Message: Clone + 'a>(items: Vec<Item<'a, Message>>) -> Element<'a, Message> {
    let mut content = Column::new().spacing(0);
    for (index, item) in items.into_iter().enumerate() {
        let striped = index % 2 == 1;
        content = content.push(row_view(item, striped));
    }
    container(content).width(Length::Fill).into()
}

fn row_view<'a, Message: Clone + 'a>(
    item: Item<'a, Message>,
    striped: bool,
) -> Element<'a, Message> {
    let indent = space().width(Length::Fixed(item.depth as f32 * 12.0));
    let chevron_id = toggle_id(&item.id);
    let chevron: Element<'a, Message> = if item.has_children {
        container(
            button(text(if item.expanded { "▾" } else { "▸" }).size(9))
                .style(theme::button_text)
                .padding([0, 1])
                .on_press(item.on_toggle),
        )
        .id(chevron_id)
        .width(Length::Fixed(14.0))
        .center_x(Length::Fixed(14.0))
        .into()
    } else {
        space().width(Length::Fixed(14.0)).into()
    };
    let icon = container(text(item.icon.glyph()).size(12).color(item.icon.color()))
        .width(Length::Fixed(16.0))
        .center_x(Length::Fixed(16.0));

    let mut content = row![indent, chevron, icon, text(item.label).size(12)]
        .spacing(3)
        .align_y(iced::Alignment::Center);
    if let Some(trailing) = item.trailing {
        content = content.push(space::horizontal()).push(trailing);
    }

    let row = mouse_area(
        container(content)
            .id(item.id)
            .width(Length::Fill)
            .padding([2, 4])
            .style(theme::tree_row(
                item.selected,
                striped,
                item.hovered,
                item.drop_target,
            )),
    )
    .on_press(item.on_press)
    .on_release(item.on_release)
    .on_enter(item.on_enter)
    .on_exit(item.on_exit)
    .on_right_press(item.on_context)
    .interaction(iced::mouse::Interaction::Pointer);

    if let Some(extra) = item.extra {
        Column::with_children(vec![row.into(), extra]).into()
    } else {
        row.into()
    }
}

/// The stable id of a row's expand/collapse chevron.
pub fn toggle_id(row: &UiId) -> UiId {
    id::tree_toggle(row.key())
}
