use crate::uikit::components::text_field;
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::{button as raw_button, column, container, row, text};
use iced::{Element, Length};
use std::collections::HashMap;

pub struct Transform<'a> {
    pub inputs: &'a HashMap<(usize, usize), String>,
    pub position: [f32; 3],
    pub rotation: [f32; 3],
    pub scale: [f32; 3],
    pub collapsed: bool,
    pub toggle_id: iced::widget::Id,
}

/// The combined Transform card shown above the generic components.
pub fn view<'a, Message: Clone + 'a>(
    transform: Transform<'a>,
    on_edit: impl Fn(usize, usize, String) -> Message + 'a,
    on_toggle: Message,
) -> Element<'a, Message> {
    let on_edit = std::rc::Rc::new(on_edit);
    let chevron = container(
        raw_button(text(if transform.collapsed { "▸" } else { "▾" }).size(9))
            .style(theme::button_text)
            .padding([0, 2])
            .on_press(on_toggle),
    )
    .id(transform.toggle_id);

    let header = container(
        row![
            chevron,
            text(Icon::Scene.glyph())
                .size(12)
                .color(Icon::Scene.color()),
            text("Transform").size(11),
        ]
        .spacing(5)
        .align_y(iced::Alignment::Center),
    )
    .padding([1, 4])
    .style(theme::component_header);

    let mut body = column![].spacing(0);
    if !transform.collapsed {
        for kind in 0..3 {
            let vector = match kind {
                0 => transform.position,
                1 => transform.rotation,
                _ => transform.scale,
            };
            let mut axes = row![].spacing(3);
            for (axis, axis_value) in vector.iter().enumerate() {
                let emit = on_edit.clone();
                let value = transform
                    .inputs
                    .get(&(kind, axis))
                    .cloned()
                    .unwrap_or_else(|| format_number(*axis_value));
                axes = axes.push(text_field::axis(
                    id::transform_axis(kind, axis),
                    axis_label(axis),
                    axis_color(axis),
                    &value,
                    move |value| emit(kind, axis, value),
                ));
            }
            body = body.push(
                container(
                    row![
                        text(["Position", "Rotation", "Scale"][kind])
                            .size(10)
                            .color(theme::TEXT_MUTED)
                            .width(Length::Fixed(56.0)),
                        axes,
                    ]
                    .spacing(4)
                    .align_y(iced::Alignment::Center),
                )
                .padding([1, 4])
                .style(theme::property_row(kind % 2 == 1)),
            );
        }
    }

    container(column![header, body].spacing(0))
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

fn axis_label(axis: usize) -> &'static str {
    ["X", "Y", "Z"][axis.min(2)]
}

fn axis_color(axis: usize) -> iced::Color {
    match axis {
        0 => theme::DANGER,
        1 => theme::SUCCESS,
        _ => theme::ACCENT,
    }
}

fn format_number(value: f32) -> String {
    if value.fract().abs() < f32::EPSILON {
        format!("{value:.0}")
    } else {
        format!("{value:.3}")
    }
}
