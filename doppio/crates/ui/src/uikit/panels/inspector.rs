use crate::uikit::components::{component_card, component_picker, field_editor, text_field};
use crate::uikit::id;
use crate::uikit::EntityView;
use doppio_engine::{ComponentData, ComponentType};
use iced::widget::{container, scrollable, text, Column};
use iced::Element;
use std::collections::HashMap;
use std::rc::Rc;

/// One edit produced by the generic inspector.
#[derive(Debug, Clone)]
pub enum Edit {
    Name(String),
    FieldText {
        component: usize,
        field: usize,
        axis: usize,
        value: String,
    },
    FieldBool {
        component: usize,
        field: usize,
        value: bool,
    },
    FieldEnum {
        component: usize,
        field: usize,
        index: i32,
    },
    FieldNumber {
        component: usize,
        field: usize,
        value: f32,
    },
    FieldColor {
        component: usize,
        field: usize,
        axis: usize,
        value: f32,
    },
    AddComponent(u32),
    RemoveComponent(usize),
    Delete,
}

pub struct Inspector<'a> {
    pub entity: Option<&'a EntityView>,
    pub name_input: &'a str,
    pub components: &'a [ComponentData],
    pub component_types: &'a [ComponentType],
    pub field_inputs: &'a HashMap<(usize, usize, usize), String>,
}

/// Composes the inspector from reusable uikit components.
pub fn view<'a, Message: Clone + 'a>(
    inspector: Inspector<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let mut content = Column::new().spacing(8);

    let Some(entity) = inspector.entity else {
        return container(content.push(text("No entity selected")))
            .padding(8)
            .into();
    };

    content = content.push(text(format!("Entity: {}", entity.name)));
    let on_name = emit.clone();
    content = content.push(text_field::stacked(
        id::ENTITY_NAME,
        "Name",
        inspector.name_input,
        move |value| on_name(Edit::Name(value)),
    ));
    content = content.push(text(format!("Entity ID: {}", entity.id)));

    for (component_index, component) in inspector.components.iter().enumerate() {
        let meta = inspector
            .component_types
            .iter()
            .find(|kind| kind.id == component.info.type_id);
        let emit_field = emit.clone();
        content = content.push(component_card::view(
            component_card::Card {
                index: component_index,
                component,
                meta,
                inputs: inspector.field_inputs,
                remove_id: id::component_remove(component_index).into(),
            },
            move |field, axis| id::component_field(component_index, field, axis).into(),
            move |field, edit| map_field(component_index, field, edit, emit_field.as_ref()),
            emit(Edit::RemoveComponent(component_index)),
        ));
    }

    let emit_add = emit.clone();
    if let Some(picker) = component_picker::view(
        inspector.component_types,
        inspector.components,
        id::ADD_COMPONENT.into(),
        move |type_id| emit_add(Edit::AddComponent(type_id)),
    ) {
        content = content.push(picker);
    }

    container(scrollable(content)).into()
}

fn map_field<Message: Clone>(
    component: usize,
    field: usize,
    edit: field_editor::FieldEdit,
    emit: &dyn Fn(Edit) -> Message,
) -> Message {
    match edit {
        field_editor::FieldEdit::Text { axis, value } => emit(Edit::FieldText {
            component,
            field,
            axis,
            value,
        }),
        field_editor::FieldEdit::Bool { value } => emit(Edit::FieldBool {
            component,
            field,
            value,
        }),
        field_editor::FieldEdit::Enum { index } => emit(Edit::FieldEnum {
            component,
            field,
            index,
        }),
        field_editor::FieldEdit::Number { value, .. } => emit(Edit::FieldNumber {
            component,
            field,
            value,
        }),
        field_editor::FieldEdit::Color { axis, value } => emit(Edit::FieldColor {
            component,
            field,
            axis,
            value,
        }),
    }
}
