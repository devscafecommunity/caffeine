use crate::uikit::components::{button, gizmo};
use crate::uikit::EntityView;
use iced::widget::{column, container, text, text_input};
use iced::Element;

pub fn view<'a, Message: Clone + 'a>(
    entity: Option<&'a EntityView>,
    name_input: &'a str,
    on_name: impl Fn(String) -> Message + 'a,
    on_nudge_x: Message,
    on_nudge_rotation_y: Message,
    on_scale_up: Message,
    on_delete: Message,
) -> Element<'a, Message> {
    let mut content = column![text("Inspector").size(16)].spacing(8);

    if let Some(entity) = entity {
        content = content.push(text(format!("Entity: {}", entity.name)));
        content = content.push(
            text_input("Name", name_input)
                .on_input(on_name)
                .id("entity-name"),
        );
        content = content.push(text(format!("Entity ID: {}", entity.id)));
        content = content.push(text(format!(
            "Position: {:.2}, {:.2}, {:.2}",
            entity.position[0], entity.position[1], entity.position[2]
        )));
        content = content.push(text(format!(
            "Rotation: {:.2}, {:.2}, {:.2}",
            entity.rotation[0], entity.rotation[1], entity.rotation[2]
        )));
        content = content.push(text(format!(
            "Scale: {:.2}, {:.2}, {:.2}",
            entity.scale[0], entity.scale[1], entity.scale[2]
        )));
        content = content.push(gizmo::axes(
            on_nudge_x.clone(),
            on_nudge_rotation_y.clone(),
            on_scale_up.clone(),
        ));
        content = content.push(button::toolbar("nudge-x", "+X", on_nudge_x));
        content = content.push(button::toolbar(
            "nudge-rot-y",
            "+Rot Y",
            on_nudge_rotation_y,
        ));
        content = content.push(button::toolbar("scale-up", "Scale x1.1", on_scale_up));
        content = content.push(button::danger("delete-entity", "Delete", on_delete));
    } else {
        content = content.push(text("No entity selected"));
    }

    container(content).padding(8).into()
}
