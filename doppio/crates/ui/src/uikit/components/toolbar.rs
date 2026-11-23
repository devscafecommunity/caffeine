use crate::uikit::theme;
use iced::widget::{container, row, space, Row};
use iced::{Element, Length};

pub fn group<'a, Message: 'a>(items: Vec<Element<'a, Message>>) -> Element<'a, Message> {
    Row::with_children(items).spacing(4).into()
}

pub fn divider<'a, Message: 'a>() -> Element<'a, Message> {
    container(space().width(1).height(18))
        .style(theme::divider)
        .into()
}

pub fn view<'a, Message: 'a>(
    brand: Element<'a, Message>,
    groups: Vec<Element<'a, Message>>,
    center: Option<Element<'a, Message>>,
    status: Element<'a, Message>,
) -> Element<'a, Message> {
    let mut bar = row![brand].spacing(10).align_y(iced::Alignment::Center);
    for group in groups {
        bar = bar.push(divider()).push(group);
    }
    bar = bar.push(space::horizontal());
    if let Some(center) = center {
        bar = bar.push(center).push(space::horizontal());
    }
    bar = bar.push(status);
    container(bar.padding([3, 10]).width(Length::Fill))
        .style(theme::toolbar)
        .into()
}
