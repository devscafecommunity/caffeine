use iced::widget::{column, container, scrollable, text};
use iced::Element;

pub fn view<'a, Message: 'a>(lines: &'a [String]) -> Element<'a, Message> {
    let mut content = column![].spacing(2);
    for line in lines {
        content = content.push(text(line.as_str()));
    }
    container(scrollable(content)).into()
}
