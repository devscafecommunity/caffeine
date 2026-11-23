use iced::widget::{container, pick_list, row, text};
use iced::{Element, Length};
use std::fmt::Display;

/// Labelled dropdown used by property panels.
pub fn property<'a, Message, T>(
    id: impl Into<iced::widget::Id>,
    label: impl Into<String>,
    options: Vec<T>,
    selected: Option<T>,
    on_select: impl Fn(T) -> Message + 'a,
) -> Element<'a, Message>
where
    Message: Clone + 'a,
    T: Clone + PartialEq + Display + 'a,
{
    container(
        row![
            text(label.into())
                .size(10)
                .color(crate::uikit::theme::TEXT_MUTED),
            pick_list(options, selected, on_select)
                .width(Length::Fill)
                .text_size(11)
                .padding([3, 6])
                .style(crate::uikit::theme::pick_list)
                .menu_style(crate::uikit::theme::pick_menu)
                .placeholder("Choose…"),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .into()
}

/// The `Category · Name` option that names `name`, so a dropdown of presets shows the one in use instead of
/// "Choose…" (the generators set the model's name to the preset's name when one is applied).
pub fn current_option(options: &[String], name: &str) -> Option<String> {
    let suffix = format!("· {name}");
    options
        .iter()
        .find(|option| option.ends_with(&suffix))
        .cloned()
}

#[cfg(test)]
mod current_option_tests {
    use super::current_option;

    #[test]
    fn the_option_that_ends_with_the_name_is_selected() {
        let options = vec![
            "Trees · Oak".to_owned(),
            "Trees · Ancient Oak".to_owned(),
            "Rocks · Boulder".to_owned(),
        ];
        assert_eq!(
            current_option(&options, "Oak").as_deref(),
            Some("Trees · Oak")
        );
        assert_eq!(
            current_option(&options, "Ancient Oak").as_deref(),
            Some("Trees · Ancient Oak")
        );
        assert_eq!(current_option(&options, "My own"), None);
    }
}
