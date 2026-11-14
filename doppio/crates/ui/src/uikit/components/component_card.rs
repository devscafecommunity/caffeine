use crate::uikit::components::{button, field_editor};
use doppio_engine::{ComponentData, ComponentType};
use iced::widget::{column, container, row, space, text, Column};
use iced::{Element, Length};
use std::collections::HashMap;
use std::rc::Rc;

pub struct Card<'a> {
    pub index: usize,
    pub component: &'a ComponentData,
    pub meta: Option<&'a ComponentType>,
    pub inputs: &'a HashMap<(usize, usize, usize), String>,
    pub remove_id: iced::widget::Id,
}

/// A component card: header, generic field editors and a remove action.
pub fn view<'a, Message: Clone + 'a>(
    card: Card<'a>,
    id_for_field_axis: impl Fn(usize, usize) -> iced::widget::Id + 'a,
    on_field: impl Fn(usize, field_editor::FieldEdit) -> Message + 'a,
    on_remove: Message,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(usize, field_editor::FieldEdit) -> Message + 'a> = Rc::new(on_field);
    let ids: Rc<dyn Fn(usize, usize) -> iced::widget::Id + 'a> = Rc::new(id_for_field_axis);
    let mut fields = Column::new().spacing(4);

    for (field_index, value) in card.component.values.iter().enumerate() {
        let Some(info) = card.meta.and_then(|meta| meta.fields.get(field_index)) else {
            continue;
        };
        let emit = emit.clone();
        let ids = ids.clone();
        fields = fields.push(field_editor::view(
            field_editor::Field {
                component: card.index,
                index: field_index,
                info,
                value,
                inputs: card.inputs,
            },
            move |axis| ids(field_index, axis),
            move |edit| emit(field_index, edit),
        ));
    }

    container(
        column![
            row![
                text(card.component.info.name.clone()).size(13),
                space::horizontal(),
                button::danger(card.remove_id, "×", on_remove),
            ]
            .align_y(iced::Alignment::Center),
            fields
        ]
        .spacing(4),
    )
    .padding(6)
    .width(Length::Fill)
    .into()
}
