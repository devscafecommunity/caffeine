use crate::uikit::components::{color_picker, slider, text_field};
use doppio_engine::{FieldInfo, FieldKind, FieldValue};
use iced::widget::{checkbox, pick_list, row, text, Column};
use iced::{Element, Length};
use std::collections::HashMap;
use std::rc::Rc;

/// One edit produced by a field editor.
#[derive(Debug, Clone)]
pub enum FieldEdit {
    Text { axis: usize, value: String },
    Bool { value: bool },
    Enum { index: i32 },
    Number { axis: usize, value: f32 },
    Color { axis: usize, value: f32 },
}

pub struct Field<'a> {
    pub component: usize,
    pub index: usize,
    pub info: &'a FieldInfo,
    pub value: &'a FieldValue,
    pub inputs: &'a HashMap<(usize, usize, usize), String>,
}

/// Renders one component field (scalar, vector, bool, string or enum).
#[allow(clippy::redundant_closure)]
pub fn view<'a, Message: Clone + 'a>(
    field: Field<'a>,
    id_for_axis: impl Fn(usize) -> iced::widget::Id + 'a,
    on_edit: impl Fn(FieldEdit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(FieldEdit) -> Message + 'a> = Rc::new(on_edit);
    let input = |axis: usize| {
        field
            .inputs
            .get(&(field.component, field.index, axis))
            .cloned()
            .unwrap_or_else(|| display(field.value, axis))
    };

    match (field.info.kind, field.value) {
        (FieldKind::Color, FieldValue::Color(color)) => color_picker::view(
            field.info.name.as_str(),
            *color,
            move |axis| id_for_axis(axis),
            move |axis, value| emit(FieldEdit::Color { axis, value }),
        ),
        (FieldKind::F32, FieldValue::F32(number)) if field.info.max > field.info.min => {
            let emit = emit.clone();
            slider::property_step(
                id_for_axis(0),
                field.info.name.clone(),
                field.info.min..=field.info.max,
                *number,
                field.info.step,
                move |value| emit(FieldEdit::Number { axis: 0, value }),
            )
        }
        (FieldKind::Bool, FieldValue::Bool(flag)) => {
            let emit = emit.clone();
            row![
                checkbox(*flag).on_toggle(move |flag| { emit(FieldEdit::Bool { value: flag }) }),
                iced::widget::space::horizontal(),
                iced::widget::container(
                    text(field.info.name.clone())
                        .size(9)
                        .color(crate::uikit::theme::TEXT_MUTED)
                )
                .width(Length::Fill)
                .align_x(iced::alignment::Horizontal::Right),
            ]
            .spacing(5)
            .align_y(iced::Alignment::Center)
            .into()
        }
        (FieldKind::Enum, FieldValue::Enum(selected)) => {
            let options: Vec<String> = field.info.options.clone();
            let current = options.get(*selected as usize).cloned();
            let emit = emit.clone();
            let lookup = options.clone();
            let picker = pick_list(options, current, move |choice| {
                let index = lookup
                    .iter()
                    .position(|option| *option == choice)
                    .unwrap_or(0) as i32;
                emit(FieldEdit::Enum { index })
            });
            Column::new()
                .push(
                    text(field.info.name.clone())
                        .size(9)
                        .color(crate::uikit::theme::TEXT_MUTED),
                )
                .push(picker.width(Length::Fill))
                .spacing(1)
                .into()
        }
        (FieldKind::Vec2 | FieldKind::Vec3 | FieldKind::Vec4, _) => {
            let axes = match field.info.kind {
                FieldKind::Vec2 => 2,
                FieldKind::Vec3 => 3,
                _ => 4,
            };
            let mut axes_row = row![].spacing(3);
            for axis in 0..axes {
                let emit = emit.clone();
                axes_row = axes_row.push(text_field::axis(
                    id_for_axis(axis),
                    axis_label(axis),
                    axis_color(axis),
                    &input(axis),
                    move |value| emit(FieldEdit::Text { axis, value }),
                ));
            }
            row![
                text(field.info.name.clone())
                    .size(9)
                    .color(crate::uikit::theme::TEXT_MUTED)
                    .width(Length::Fixed(62.0)),
                axes_row,
            ]
            .spacing(4)
            .align_y(iced::Alignment::Center)
            .into()
        }
        _ => {
            let emit = emit.clone();
            text_field::property(
                id_for_axis(0),
                field.info.name.clone(),
                &input(0),
                move |value| emit(FieldEdit::Text { axis: 0, value }),
            )
        }
    }
}

fn axis_label(axis: usize) -> &'static str {
    match axis {
        0 => "X",
        1 => "Y",
        2 => "Z",
        _ => "W",
    }
}

fn axis_color(axis: usize) -> iced::Color {
    match axis {
        0 => crate::uikit::theme::DANGER,
        1 => crate::uikit::theme::SUCCESS,
        2 => crate::uikit::theme::ACCENT,
        _ => crate::uikit::theme::TEXT_MUTED,
    }
}

fn display(value: &FieldValue, axis: usize) -> String {
    match value {
        FieldValue::F32(number) => format_number(*number),
        FieldValue::I32(number) => number.to_string(),
        FieldValue::U32(number) => number.to_string(),
        FieldValue::Vec2(vector) => format_number(vector[axis.min(1)]),
        FieldValue::Vec3(vector) => format_number(vector[axis.min(2)]),
        FieldValue::Vec4(vector) => format_number(vector[axis.min(3)]),
        FieldValue::String(text) => text.clone(),
        FieldValue::Bool(flag) => flag.to_string(),
        FieldValue::Enum(index) => index.to_string(),
        FieldValue::Color(color) => format_number(color[axis.min(3)]),
    }
}

fn format_number(value: f32) -> String {
    if value.fract().abs() < f32::EPSILON {
        format!("{value:.0}")
    } else {
        format!("{value:.3}")
    }
}

/// Parses one text edit against the current value, keeping other axes intact.
pub fn parse_value(current: &FieldValue, axis: usize, text: &str) -> Option<FieldValue> {
    let text = text.trim();
    match current {
        FieldValue::F32(_) => text.parse::<f32>().ok().map(FieldValue::F32),
        FieldValue::I32(_) => text.parse::<i32>().ok().map(FieldValue::I32),
        FieldValue::U32(_) => text.parse::<u32>().ok().map(FieldValue::U32),
        FieldValue::Vec2(vector) => {
            let mut vector = *vector;
            vector[axis.min(1)] = text.parse::<f32>().ok()?;
            Some(FieldValue::Vec2(vector))
        }
        FieldValue::Vec3(vector) => {
            let mut vector = *vector;
            vector[axis.min(2)] = text.parse::<f32>().ok()?;
            Some(FieldValue::Vec3(vector))
        }
        FieldValue::Vec4(vector) => {
            let mut vector = *vector;
            vector[axis.min(3)] = text.parse::<f32>().ok()?;
            Some(FieldValue::Vec4(vector))
        }
        FieldValue::String(_) => Some(FieldValue::String(text.to_owned())),
        FieldValue::Enum(_) => text.parse::<i32>().ok().map(FieldValue::Enum),
        FieldValue::Bool(flag) => Some(FieldValue::Bool(*flag)),
        FieldValue::Color(color) => {
            let mut color = *color;
            color[axis.min(3)] = text.parse::<f32>().ok()?;
            Some(FieldValue::Color(color))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_keeps_other_vector_axes() {
        let current = FieldValue::Vec3([1.0, 2.0, 3.0]);
        assert_eq!(
            parse_value(&current, 1, "7.5"),
            Some(FieldValue::Vec3([1.0, 7.5, 3.0]))
        );
        assert_eq!(parse_value(&current, 0, "nope"), None);
    }

    #[test]
    fn parse_scalars_and_strings() {
        assert_eq!(
            parse_value(&FieldValue::F32(1.0), 0, "2.5"),
            Some(FieldValue::F32(2.5))
        );
        assert_eq!(
            parse_value(&FieldValue::I32(1), 0, "9"),
            Some(FieldValue::I32(9))
        );
        assert_eq!(
            parse_value(&FieldValue::String("a".into()), 0, "b"),
            Some(FieldValue::String("b".into()))
        );
    }

    #[test]
    fn display_formats_numbers_and_axes() {
        assert_eq!(display(&FieldValue::F32(2.0), 0), "2");
        assert_eq!(display(&FieldValue::Vec3([1.0, 2.5, 3.0]), 1), "2.500");
        assert_eq!(display(&FieldValue::U32(42), 0), "42");
    }
}
