//! The transport: Play / Pause and Stop, in the middle of the top bar.
//!
//! Play starts the game's simulation (the scene as the compiled game runs it); Pause freezes it where it is;
//! Stop ends it and puts the scene back as it was before Play.

use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::{button, container, row, text};
use iced::{Element, Length};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Stopped,
    Playing,
    Paused,
}

/// The two buttons. The first reads ▶ unless the game is playing, when it reads ⏸; Stop is only live once a
/// play session exists.
pub fn view<'a, Message: Clone + 'a>(
    state: State,
    on_play_pause: Message,
    on_stop: Message,
) -> Element<'a, Message> {
    let playing = state == State::Playing;
    let play = container(
        button(
            text(if playing { "⏸" } else { "▶" })
                .size(14)
                .width(Length::Fixed(20.0))
                .align_x(iced::alignment::Horizontal::Center),
        )
        .style(theme::transport_button(playing, theme::SUCCESS))
        .padding([2, 8])
        .on_press(on_play_pause),
    )
    .id(id::TRANSPORT_PLAY);
    let stop_button = button(
        text("⏹")
            .size(14)
            .width(Length::Fixed(20.0))
            .align_x(iced::alignment::Horizontal::Center),
    )
    .style(theme::transport_button(
        state == State::Paused,
        theme::DANGER,
    ))
    .padding([2, 8]);
    let stop = container(if state == State::Stopped {
        stop_button
    } else {
        stop_button.on_press(on_stop)
    })
    .id(id::TRANSPORT_STOP);
    row![play, stop]
        .spacing(4)
        .align_y(iced::Alignment::Center)
        .into()
}
