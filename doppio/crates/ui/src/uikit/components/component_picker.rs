use doppio_engine::{ComponentData, ComponentType};
use iced::widget::{container, pick_list, Id};
use iced::{Element, Length};

/// "Add Component" picker for the types the entity does not have.
pub fn view<'a, Message: Clone + 'a>(
    types: &'a [ComponentType],
    present: &'a [ComponentData],
    id: Id,
    on_add: impl Fn(u32) -> Message + 'a,
) -> Option<Element<'a, Message>> {
    let available: Vec<TypeOption> = types
        .iter()
        .filter(|kind| {
            !matches!(
                kind.name.as_str(),
                "Transform" | "Position 3D" | "Rotation 3D" | "Scale 3D"
            ) && present
                .iter()
                .all(|component| component.info.type_id != kind.id)
        })
        .map(|kind| TypeOption {
            id: kind.id,
            name: kind.name.clone(),
        })
        .collect();
    if available.is_empty() {
        return None;
    }
    let placeholder = TypeOption {
        id: u32::MAX,
        name: "Add Component…".to_owned(),
    };
    Some(
        container(
            pick_list(available, Some(placeholder), move |choice| {
                on_add(choice.id)
            })
            .width(Length::Fill),
        )
        .id(id)
        .into(),
    )
}

#[derive(Debug, Clone, PartialEq)]
struct TypeOption {
    id: u32,
    name: String,
}

impl std::fmt::Display for TypeOption {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.name)
    }
}
