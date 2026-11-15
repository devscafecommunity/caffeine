use crate::uikit::components::field_editor;
use crate::uikit::icons::Icon;
use crate::uikit::theme as ui_theme;
use crate::uikit::theme;
use doppio_engine::{ComponentData, ComponentType};
use iced::widget::{button as raw_button, column, container, row, space, text, Column};
use iced::{Element, Length};
use std::collections::HashMap;
use std::rc::Rc;

pub struct Card<'a> {
    pub index: usize,
    pub component: &'a ComponentData,
    pub meta: Option<&'a ComponentType>,
    pub inputs: &'a HashMap<(usize, usize, usize), String>,
    pub collapsed: bool,
    pub toggle_id: iced::widget::Id,
    pub icon: Icon,
    pub remove_id: iced::widget::Id,
}

/// A component card: collapsible header, generic property rows and remove.
pub fn view<'a, Message: Clone + 'a>(
    card: Card<'a>,
    id_for_field_axis: impl Fn(usize, usize) -> iced::widget::Id + 'a,
    on_field: impl Fn(usize, field_editor::FieldEdit) -> Message + 'a,
    on_toggle: Message,
    on_remove: Message,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(usize, field_editor::FieldEdit) -> Message + 'a> = Rc::new(on_field);
    let ids: Rc<dyn Fn(usize, usize) -> iced::widget::Id + 'a> = Rc::new(id_for_field_axis);
    let mut fields = Column::new().spacing(0);

    for (field_index, value) in card.component.values.iter().enumerate() {
        let Some(info) = card.meta.and_then(|meta| meta.fields.get(field_index)) else {
            continue;
        };
        let emit = emit.clone();
        let ids = ids.clone();
        fields = fields.push(
            container(field_editor::view(
                field_editor::Field {
                    component: card.index,
                    index: field_index,
                    info,
                    value,
                    inputs: card.inputs,
                },
                move |axis| ids(field_index, axis),
                move |edit| emit(field_index, edit),
            ))
            .padding([1, 4])
            .style(theme::property_row(field_index % 2 == 1)),
        );
    }

    let chevron = container(
        raw_button(text(if card.collapsed { "▸" } else { "▾" }).size(9))
            .style(theme::button_text)
            .padding([0, 2])
            .on_press(on_toggle),
    )
    .id(card.toggle_id);

    let header = container(
        row![
            chevron,
            text(card.icon.glyph()).size(11).color(card.icon.color()),
            text(card.component.info.name.clone()).size(11),
            space::horizontal(),
            container(
                raw_button(text("×").size(10))
                    .style(ui_theme::button_danger)
                    .padding([0, 4])
                    .on_press(on_remove),
            )
            .id(card.remove_id),
        ]
        .spacing(5)
        .align_y(iced::Alignment::Center),
    )
    .padding([1, 4])
    .style(theme::component_header);

    let body: Element<'a, Message> = if card.collapsed {
        space().height(0).into()
    } else {
        fields.into()
    };

    container(column![header, body].spacing(0))
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}
