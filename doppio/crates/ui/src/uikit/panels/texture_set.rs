use crate::uikit::components::{
    button, checkbox, color_picker, dropdown, list_row, slider, text_field, toggle_chip, uv_canvas,
};
use crate::uikit::generators::BlendMode;
use crate::uikit::id;
use crate::uikit::texture_set::{ExportPreset, TextureSet};
use crate::uikit::theme;
use iced::widget::{column, container, row, scrollable, text, Column};
use iced::{Element, Length};
use std::rc::Rc;

#[derive(Debug, Clone)]
pub enum Edit {
    SelectTile(u32),
    AddTile,
    SelectLayer(usize),
    AddPaintLayer,
    AddFillLayer,
    DeleteLayer,
    MoveLayer(i32),
    SetVisible(usize, bool),
    SetOpacity(usize, f32),
    SetBlend(usize, usize),
    SetChannel(usize),
    SetBrushRadius(f32),
    SetBrushHardness(f32),
    SetBrushOpacity(f32),
    SetBrushColor(usize, f32),
    SetPaintMask(bool),
    TogglePaintMode,
    SetName(String),
    Save,
    New,
    Undo,
    Redo,
    SetExportPreset(usize),
    Export,
    ExportTo(String),
    BakeHeightAo,
    BakeCurvature,
    BakeSceneAo,
    BakeMeshNormal,
    BakeMeshPosition,
    BakeMeshObjectId,
    PaintStart([f32; 2]),
    PaintMove([f32; 2]),
    PaintEnd,
    PaintDab(u32, [f32; 2]),
    Hover(Option<[f32; 2]>),
}

pub struct View<'a> {
    pub set: &'a TextureSet,
    pub preview: Option<&'a iced::widget::image::Handle>,
    pub path: Option<String>,
    pub dirty: bool,
    pub status: Option<&'a str>,
    pub can_undo: bool,
    pub can_redo: bool,
    pub paint_mode: bool,
    pub painting: bool,
    pub cursor: Option<[f32; 2]>,
    pub export_preset: usize,
    pub export_presets: &'a [ExportPreset],
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let set = view.set;

    let mut header = row![
        text("TEXTURE SET").size(9).color(theme::TEXT_MUTED),
        text(set.name.clone()).size(11).color(theme::TEXT),
        iced::widget::space::horizontal(),
    ]
    .spacing(6)
    .align_y(iced::Alignment::Center);
    if let Some(path) = view.path.as_deref() {
        header = header.push(text(path.to_owned()).size(8).color(theme::TEXT_MUTED));
    }
    if view.dirty {
        header = header.push(text("● unsaved").size(8).color(theme::WARNING));
    }
    let new = emit.clone();
    let save = emit.clone();
    let undo = emit.clone();
    let redo = emit.clone();
    header = header
        .push(button::compact(id::TEXTURE_SET_NEW, "New", new(Edit::New)))
        .push(button::compact(
            id::TEXTURE_SET_SAVE,
            "Save",
            save(Edit::Save),
        ))
        .push(button::compact(id::TEXTURE_SET_UNDO, "↶", undo(Edit::Undo)))
        .push(button::compact(id::TEXTURE_SET_REDO, "↷", redo(Edit::Redo)));
    if view.can_undo {
        header = header.push(text("undo").size(8).color(theme::TEXT_MUTED));
    }

    let mut left = Column::new().spacing(4);
    left = left.push(text("UDIM TILES").size(8).color(theme::TEXT_MUTED));
    let mut tiles = row![].spacing(3);
    for tile in set.tiles() {
        let tile_emit = emit.clone();
        tiles = tiles.push(button::compact(
            id::texture_set_tile(tile),
            tile.to_string(),
            tile_emit(Edit::SelectTile(tile)),
        ));
    }
    let add_tile = emit.clone();
    tiles = tiles.push(button::compact(
        id::TEXTURE_SET_TILE_ADD,
        "+",
        add_tile(Edit::AddTile),
    ));
    left = left.push(tiles);
    left = left.push(text("LAYERS").size(8).color(theme::TEXT_MUTED));
    let mut layers = Column::new().spacing(2);
    for (index, layer) in set.layers.iter().enumerate().rev() {
        let selected = index == set.active_layer;
        let select = emit.clone();
        let visibility = emit.clone();
        layers = layers.push(
            row![
                toggle_chip::view(
                    id::texture_set_layer_visible(index),
                    if layer.visible { "●" } else { "○" },
                    layer.visible,
                    theme::SUCCESS,
                    visibility(Edit::SetVisible(index, !layer.visible)),
                ),
                list_row::view(
                    id::texture_set_layer(index),
                    layer.name.clone(),
                    selected,
                    select(Edit::SelectLayer(index)),
                    select(Edit::SelectLayer(index)),
                    select(Edit::SelectLayer(index)),
                ),
            ]
            .spacing(2)
            .align_y(iced::Alignment::Center),
        );
    }
    left = left.push(layers);
    let add_paint = emit.clone();
    let add_fill = emit.clone();
    let remove = emit.clone();
    let up = emit.clone();
    let down = emit.clone();
    left = left.push(
        row![
            button::compact(
                id::TEXTURE_SET_LAYER_ADD_PAINT,
                "Paint",
                add_paint(Edit::AddPaintLayer)
            ),
            button::compact(
                id::TEXTURE_SET_LAYER_ADD_FILL,
                "Fill",
                add_fill(Edit::AddFillLayer)
            ),
            button::compact(id::TEXTURE_SET_LAYER_DELETE, "×", remove(Edit::DeleteLayer)),
            button::compact(id::TEXTURE_SET_LAYER_UP, "↑", up(Edit::MoveLayer(1))),
            button::compact(id::TEXTURE_SET_LAYER_DOWN, "↓", down(Edit::MoveLayer(-1))),
        ]
        .spacing(3),
    );
    if let Some(layer) = set.layer() {
        let index = set.active_layer;
        left = left.push(slider::property(
            id::texture_set_brush("layer_opacity"),
            "Opacity",
            0.0..=1.0,
            layer.opacity,
            {
                let emit = emit.clone();
                move |value| emit(Edit::SetOpacity(index, value))
            },
        ));
        let blend_options: Vec<String> = BlendMode::ALL
            .iter()
            .map(|mode| mode.label().to_owned())
            .collect();
        let blend_lookup = blend_options.clone();
        let blend_emit = emit.clone();
        left = left.push(dropdown::property(
            id::texture_set_brush("layer_blend"),
            "Blend",
            blend_options,
            Some(layer.blend.label().to_owned()),
            move |choice| {
                let index = blend_lookup
                    .iter()
                    .position(|label| *label == choice)
                    .unwrap_or(0);
                blend_emit(Edit::SetBlend(set.active_layer, index))
            },
        ));
    }

    let canvas = uv_canvas::view(
        id::TEXTURE_SET_UV_CANVAS,
        uv_canvas::View {
            image: view.preview,
            tile: set.active_tile,
            tile_size: set.tile_size,
            brush_radius: set.brush.radius,
            brush_color: theme::rgba(set.brush.color),
            painting: view.painting,
            cursor: view.cursor,
        },
        {
            let emit = emit.clone();
            move |event| match event {
                uv_canvas::Event::PaintStart(uv) => emit(Edit::PaintStart(uv)),
                uv_canvas::Event::PaintMove(uv) => emit(Edit::PaintMove(uv)),
                uv_canvas::Event::PaintEnd => emit(Edit::PaintEnd),
                uv_canvas::Event::Hover(uv) => emit(Edit::Hover(uv)),
            }
        },
    );

    let mut right = Column::new().spacing(4);
    right = right.push(text_field::property(
        id::TEXTURE_SET_NAME,
        "Name",
        &set.name,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetName(value))
        },
    ));
    let channel_options: Vec<String> = set
        .channels
        .iter()
        .map(|channel| channel.name.clone())
        .collect();
    let channel_selected = set
        .channels
        .get(set.active_channel)
        .map(|channel| channel.name.clone());
    let channel_lookup = channel_options.clone();
    let channel_emit = emit.clone();
    right = right.push(dropdown::property(
        id::TEXTURE_SET_CHANNEL,
        "Channel",
        channel_options,
        channel_selected,
        move |choice| {
            let index = channel_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            channel_emit(Edit::SetChannel(index))
        },
    ));
    right = right.push(checkbox::property(
        id::TEXTURE_SET_MASK,
        "Paint layer mask",
        set.paint_mask,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetPaintMask(value))
        },
    ));
    let paint_toggle = emit.clone();
    right = right.push(toggle_chip::view(
        id::TEXTURE_SET_PAINT_3D,
        if view.paint_mode {
            "3D paint on"
        } else {
            "3D paint"
        },
        view.paint_mode,
        theme::ACCENT,
        paint_toggle(Edit::TogglePaintMode),
    ));
    right = right.push(text("BRUSH").size(8).color(theme::TEXT_MUTED));
    right = right.push(slider::property(
        id::texture_set_brush("radius"),
        "Radius",
        1.0..=128.0,
        set.brush.radius,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetBrushRadius(value))
        },
    ));
    right = right.push(slider::property(
        id::texture_set_brush("hardness"),
        "Hardness",
        0.0..=0.99,
        set.brush.hardness,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetBrushHardness(value))
        },
    ));
    right = right.push(slider::property(
        id::texture_set_brush("opacity"),
        "Opacity",
        0.0..=1.0,
        set.brush.opacity,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetBrushOpacity(value))
        },
    ));
    right = right.push(color_picker::view(
        "Brush colour",
        set.brush.color,
        |axis| id::texture_set_brush_color(axis).into(),
        {
            let emit = emit.clone();
            move |axis, value| emit(Edit::SetBrushColor(axis, value))
        },
    ));
    right = right.push(text("EXPORT").size(8).color(theme::TEXT_MUTED));
    let preset_options: Vec<String> = view
        .export_presets
        .iter()
        .map(|preset| preset.name.to_owned())
        .collect();
    let preset_lookup = preset_options.clone();
    let preset_emit = emit.clone();
    right = right.push(dropdown::property(
        id::TEXTURE_SET_EXPORT_PRESET,
        "Preset",
        preset_options,
        view.export_presets
            .get(view.export_preset)
            .map(|preset| preset.name.to_owned()),
        move |choice| {
            let index = preset_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            preset_emit(Edit::SetExportPreset(index))
        },
    ));
    let export = emit.clone();
    right = right.push(button::compact(
        id::TEXTURE_SET_EXPORT,
        "Export maps",
        export(Edit::Export),
    ));
    let bake_ao = emit.clone();
    let bake_curvature = emit.clone();
    let bake_scene = emit.clone();
    right = right.push(
        row![
            button::compact(
                id::TEXTURE_SET_BAKE_AO,
                "AO from height",
                bake_ao(Edit::BakeHeightAo)
            ),
            button::compact(
                id::TEXTURE_SET_BAKE_CURVATURE,
                "Curvature",
                bake_curvature(Edit::BakeCurvature)
            ),
        ]
        .spacing(3),
    );
    right = right.push(button::compact(
        id::TEXTURE_SET_BAKE_SCENE_AO,
        "AO from scene",
        bake_scene(Edit::BakeSceneAo),
    ));
    let bake_normal = emit.clone();
    let bake_position = emit.clone();
    let bake_id = emit.clone();
    right = right.push(
        row![
            button::compact(
                id::TEXTURE_SET_BAKE_NORMAL,
                "Normal",
                bake_normal(Edit::BakeMeshNormal)
            ),
            button::compact(
                id::TEXTURE_SET_BAKE_POSITION,
                "Position",
                bake_position(Edit::BakeMeshPosition)
            ),
            button::compact(
                id::TEXTURE_SET_BAKE_OBJECT_ID,
                "ID",
                bake_id(Edit::BakeMeshObjectId)
            ),
        ]
        .spacing(3),
    );
    if let Some(status) = view.status {
        right = right.push(text(status).size(8).color(theme::WARNING));
    }

    let body = row![
        container(scrollable(left)).width(Length::Fixed(220.0)),
        container(canvas)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface),
        container(scrollable(right)).width(Length::Fixed(230.0)),
    ]
    .spacing(6)
    .height(Length::Fill);

    column![header, container(body).height(Length::Fill)]
        .spacing(4)
        .into()
}
