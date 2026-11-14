use crate::uikit::components::button;
use crate::uikit::EntityView;
use iced::widget::{column, container, scrollable, text};
use iced::Element;

pub fn view<'a, Message: Clone + 'a>(
    entities: &'a [EntityView],
    selected: usize,
    on_select: impl Fn(usize) -> Message + 'a,
) -> Element<'a, Message> {
    let mut list = column![text("Hierarchy").size(16)].spacing(4);
    if entities.is_empty() {
        list = list.push(text("No entities"));
    }
    for (index, entity) in entities.iter().enumerate() {
        let label = format!("{}  #{}", entity.name, entity.id);
        list = list.push(button::selectable(
            format!("entity-{index}"),
            label,
            index == selected,
            on_select(index),
        ));
    }
    container(scrollable(list)).padding(8).into()
}
