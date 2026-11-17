use crate::uikit::components::{gizmo, scene_markers, viewport_shader, viewport_surface};
use crate::uikit::id;
use iced::mouse;
use iced::widget::container;
use iced::{Element, Point, Size};

pub struct ViewportState<'a> {
    pub frame: Option<&'a viewport_shader::Frame>,
    pub status: String,
    pub hint: String,
    pub empty: bool,
    pub dragging: bool,
    pub camera: &'a doppio_engine::Camera,
    pub size: Size,
    pub mode: gizmo::Mode,
    pub target: Option<gizmo::Target>,
    pub markers: &'a [scene_markers::Marker],
    pub show_markers: bool,
    pub selected_entity: Option<u32>,
}

pub struct ViewportCallbacks<'a, Message> {
    pub surface_left_press: Message,
    pub surface_right_press: Message,
    pub surface_left_release: Message,
    pub surface_right_release: Message,
    pub surface_exit: Message,
    pub surface_move: Box<dyn Fn(Point) -> Message + 'a>,
    pub surface_scroll: Box<dyn Fn(mouse::ScrollDelta) -> Message + 'a>,
    pub surface_double_click: Message,
    pub surface_resized: Box<dyn Fn(Size) -> Message + 'a>,
    pub surface_gizmo: Box<dyn Fn(gizmo::Message) -> Message + 'a>,
    pub surface_marker: Box<dyn Fn(scene_markers::Message) -> Message + 'a>,
}

/// The viewport panel: camera toolbar + interactive engine surface.
pub fn view<'a, Message: Clone + 'a>(
    state: ViewportState<'a>,
    callbacks: ViewportCallbacks<'a, Message>,
) -> Element<'a, Message> {
    let gizmo = gizmo::view(gizmo::View {
        mode: state.mode,
        camera: state.camera,
        viewport: state.size,
        target: state.target,
    })
    .map(callbacks.surface_gizmo);
    // Markers sit under the gizmo: its handles win a click, then a marker icon, then the scene.
    let overlay: Element<'a, Message> = if state.show_markers && !state.markers.is_empty() {
        iced::widget::stack![
            scene_markers::view(scene_markers::View {
                camera: state.camera,
                viewport: state.size,
                markers: state.markers,
                selected: state.selected_entity,
            })
            .map(callbacks.surface_marker),
            gizmo,
        ]
        .into()
    } else {
        gizmo
    };

    let surface = viewport_surface::view(viewport_surface::Surface {
        id: id::VIEWPORT_SURFACE,
        frame: state.frame,
        status: state.status,
        hint: state.hint,
        empty: state.empty,
        dragging: state.dragging,
        on_left_press: callbacks.surface_left_press,
        on_right_press: callbacks.surface_right_press,
        on_left_release: callbacks.surface_left_release,
        on_right_release: callbacks.surface_right_release,
        on_exit: callbacks.surface_exit,
        on_move: callbacks.surface_move,
        on_scroll: callbacks.surface_scroll,
        on_double_click: callbacks.surface_double_click,
        on_resize: callbacks.surface_resized,
        gizmo: overlay,
    });

    container(surface)
        .width(iced::Length::Fill)
        .height(iced::Length::Fill)
        .into()
}
