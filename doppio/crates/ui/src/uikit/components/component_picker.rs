use doppio_engine::{ComponentData, ComponentType};
use iced::widget::{container, pick_list, Id};
use iced::Element;

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
            present
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
    Some(
        container(pick_list(available, None::<TypeOption>, move |choice| {
            on_add(choice.id)
        }))
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
