use crate::uikit::components::{
    button, component_card, component_picker, field_editor, text_field, texture_slot,
    transform_card,
};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::{theme, EntityView};
use doppio_engine::{ComponentData, ComponentType};
use iced::widget::{column, container, row, scrollable, text, Column};
use iced::Element;
use std::collections::{HashMap, HashSet};
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
    ToggleComponent(u32),
    TransformText {
        kind: usize,
        axis: usize,
        value: String,
    },
    OpenMaterial(String),
    OpenMaterialPicker,
    DropMaterialAsset,
    ClearMaterial,
    Delete,
}

pub struct Inspector<'a> {
    pub entity: Option<&'a EntityView>,
    pub name_input: &'a str,
    pub components: &'a [ComponentData],
    pub component_types: &'a [ComponentType],
    pub field_inputs: &'a HashMap<(usize, usize, usize), String>,
    pub transform_inputs: &'a HashMap<(usize, usize), String>,
    pub collapsed_components: &'a HashSet<u32>,
    pub material: Option<&'a str>,
    pub material_thumbnail: Option<&'a iced::widget::image::Handle>,
}

/// Composes the inspector from reusable uikit components.
pub fn view<'a, Message: Clone + 'a>(
    inspector: Inspector<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let mut content = Column::new().spacing(5);

    let Some(entity) = inspector.entity else {
        return container(content.push(text("No entity selected")))
            .padding(8)
            .into();
    };

    let entity_icon = text(entity.icon.glyph())
        .size(16)
        .color(entity.icon.color());
    content = content.push(
        container(
            row![
                entity_icon,
                column![
                    text(entity.name.clone()).size(12).color(theme::TEXT),
                    text(format!("Entity ID {}", entity.id))
                        .size(10)
                        .color(theme::TEXT_MUTED),
                ]
                .spacing(0),
            ]
            .spacing(7)
            .align_y(iced::Alignment::Center),
        )
        .padding([3, 6])
        .style(theme::inspector_header),
    );
    let on_name = emit.clone();
    content = content.push(text_field::property(
        id::ENTITY_NAME,
        "Name",
        inspector.name_input,
        move |value| on_name(Edit::Name(value)),
    ));

    let emit_add = emit.clone();
    if let Some(picker) = component_picker::view(
        inspector.component_types,
        inspector.components,
        id::ADD_COMPONENT.into(),
        move |type_id| emit_add(Edit::AddComponent(type_id)),
    ) {
        content = content.push(picker);
    }

    let material_path = inspector.material;
    if let Some(path) = material_path {
        let emit_open = emit.clone();
        let emit_clear = emit.clone();
        content = content.push(texture_slot::view(
            texture_slot::Slot {
                id: id::INSPECTOR_MATERIAL,
                label: "Material",
                path,
                thumbnail: inspector.material_thumbnail,
            },
            emit_open(Edit::OpenMaterial(path.to_owned())),
            Some(emit_clear(Edit::ClearMaterial)),
            Some(emit.clone()(Edit::DropMaterialAsset)),
        ));
    }
    let emit_picker = emit.clone();
    content = content.push(button::compact(
        id::INSPECTOR_MATERIAL_PICKER,
        if material_path.is_some() {
            "Choose another material…"
        } else {
            "Choose material…"
        },
        emit_picker(Edit::OpenMaterialPicker),
    ));

    let emit_transform = emit.clone();
    content = content.push(transform_card::view(
        transform_card::Transform {
            inputs: inspector.transform_inputs,
            position: entity.position,
            rotation: entity.rotation,
            scale: entity.scale,
            collapsed: inspector.collapsed_components.contains(&u32::MAX),
            toggle_id: id::component_toggle(usize::MAX).into(),
        },
        move |kind, axis, value| emit_transform(Edit::TransformText { kind, axis, value }),
        emit(Edit::ToggleComponent(u32::MAX)),
    ));

    for (component_index, component) in inspector.components.iter().enumerate() {
        if matches!(
            component.info.name.as_str(),
            "Transform" | "Position 3D" | "Rotation 3D" | "Scale 3D"
        ) {
            continue;
        }
        let meta = inspector
            .component_types
            .iter()
            .find(|kind| kind.id == component.info.type_id);
        let emit_field = emit.clone();
        let emit_toggle = emit.clone();
        content = content.push(component_card::view(
            component_card::Card {
                index: component_index,
                component,
                meta,
                inputs: inspector.field_inputs,
                collapsed: inspector
                    .collapsed_components
                    .contains(&component.info.type_id),
                toggle_id: id::component_toggle(component_index).into(),
                icon: component_icon(&component.info.name),
                remove_id: id::component_remove(component_index).into(),
            },
            move |field, axis| id::component_field(component_index, field, axis).into(),
            move |field, edit| map_field(component_index, field, edit, emit_field.as_ref()),
            emit_toggle(Edit::ToggleComponent(component.info.type_id)),
            emit(Edit::RemoveComponent(component_index)),
        ));
    }

    container(scrollable(container(content).padding([0, 9])))
        .padding(6)
        .into()
}

fn component_icon(name: &str) -> Icon {
    let lower = name.to_lowercase();
    if lower.contains("camera") {
        Icon::Camera
    } else if lower.contains("light") {
        Icon::Light
    } else if lower.contains("mesh") {
        Icon::Mesh
    } else if lower.contains("script") {
        Icon::Script
    } else if lower.contains("rigid") || lower.contains("collider") {
        Icon::Physics
    } else if lower.contains("terrain") {
        Icon::Terrain
    } else {
        Icon::Generic
    }
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
