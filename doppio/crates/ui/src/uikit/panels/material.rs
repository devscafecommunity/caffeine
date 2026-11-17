use crate::uikit::components::{
    button, card, checkbox, color_picker, dropdown, material_preview, slider, status, text_field,
    texture_slot,
};
use crate::uikit::id;
use crate::uikit::theme;
use doppio_engine::{Material, MaterialAlphaMode, MaterialPreset};
use iced::widget::{column, container, row, text, Column};
use iced::{Element, Length, Point};
use std::collections::HashSet;
use std::fmt;
use std::rc::Rc;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Section {
    Surface,
    Maps,
    Uv,
    Advanced,
    Baker,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TextureSlot {
    Albedo,
    Normal,
    Orm,
    Emission,
    Source,
}

impl TextureSlot {
    pub fn key(self) -> &'static str {
        match self {
            TextureSlot::Albedo => "albedo",
            TextureSlot::Normal => "normal",
            TextureSlot::Orm => "orm",
            TextureSlot::Emission => "emission",
            TextureSlot::Source => "source",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            TextureSlot::Albedo => "Albedo",
            TextureSlot::Normal => "Normal",
            TextureSlot::Orm => "ORM (AO/Rough/Metal)",
            TextureSlot::Emission => "Emission",
            TextureSlot::Source => "Baker source",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MapOperation {
    Normal,
    AmbientOcclusion,
    Roughness,
    Metallic,
    Orm,
    All,
}

#[derive(Debug, Clone)]
pub enum Edit {
    Name(String),
    Albedo(usize, f32),
    Metallic(f32),
    Roughness(f32),
    Reflectance(f32),
    Emission(usize, f32),
    EmissionStrength(f32),
    NormalStrength(f32),
    /// Parallax depth in UV units (0 = off). Needs a normal map with height in its alpha.
    HeightScale(f32),
    ParallaxSteps(f32),
    Geometric(bool),
    DisplacementScale(f32),
    Subdivisions(f32),
    /// Opens the picker of an external height image to merge into the normal's alpha.
    OpenHeightPicker,
    NormalDirectX(bool),
    AoStrength(f32),
    UvTiling(usize, f32),
    /// UVs follow the object's scale: a scaled mesh keeps the texture density.
    UvObjectScale(bool),
    UvOffset(usize, f32),
    AlphaMode(usize),
    AlphaCutoff(f32),
    Transmission(f32),
    Ior(f32),
    Clearcoat(f32),
    ClearcoatRoughness(f32),
    Sheen(usize, f32),
    SheenRoughness(f32),
    Iridescence(f32),
    IridescenceThickness(f32),
    IridescenceIor(f32),
    Reflection(f32),
    OpenTexturePicker(TextureSlot),
    DropTextureAsset(TextureSlot),
    ClearTexture(TextureSlot),
    ToggleSection(Section),
    ApplyPreset(usize),
    Save,
    Apply,
    New,
    OpenGenerator,
    OpenTextureSet,
    SetDirectX(bool),
    SetWrap(bool),
    SetBakeStrength(f32),
    SetBakeRadius(f32),
    SetPacking(usize),
    Generate(MapOperation),
    BakeSceneAo {
        resolution: u32,
        rays: u32,
        max_distance: f32,
    },
    SetPreviewShape(usize),
    OpenPreviewEnvironment,
    ClearPreviewEnvironment,
    SetPreviewExposure(f32),
    SetSunYaw(f32),
    SetSunElevation(f32),
    SetSunIntensity(f32),
    SetPreviewShadows(bool),
    PreviewPressed,
    PreviewMoved(Point),
    PreviewReleased,
}

pub struct MaterialView<'a> {
    pub material: &'a Material,
    pub asset_path: Option<&'a str>,
    pub dirty: bool,
    pub preview: Option<&'a iced::widget::image::Handle>,
    pub thumbnails: [Option<&'a iced::widget::image::Handle>; 4],
    pub source: &'a str,
    pub source_thumbnail: Option<&'a iced::widget::image::Handle>,
    pub presets: &'a [MaterialPreset],
    pub collapsed: &'a HashSet<Section>,
    pub baking: Option<&'a str>,
    pub directx: bool,
    pub wrap: bool,
    pub bake_strength: f32,
    pub bake_radius: f32,
    pub packing: usize,
    pub engine_ready: bool,
    pub preview_yaw: f32,
    pub preview_pitch: f32,
    pub preview_shape: usize,
    pub preview_environment: &'a str,
    pub preview_exposure: f32,
    pub sun_yaw: f32,
    pub sun_elevation: f32,
    pub sun_intensity: f32,
    pub preview_shadows: bool,
    pub map_details: [&'a str; 4],
}

#[derive(Clone, PartialEq)]
struct PresetOption {
    index: usize,
    label: String,
}

impl fmt::Display for PresetOption {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.label)
    }
}

pub fn view<'a, Message: Clone + 'a>(
    view: MaterialView<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let material = view.material;

    let status = if view.dirty { " • unsaved" } else { "" };
    let path = view.asset_path.unwrap_or("New material (not saved yet)");
    let toolbar = row![
        button::compact(id::MATERIAL_NEW, "New", emit(Edit::New)),
        button::compact(id::MATERIAL_SAVE, "Save", emit(Edit::Save)),
        button::primary(id::MATERIAL_APPLY, "Apply", emit(Edit::Apply)),
        button::compact(id::MATERIAL_GENERATOR, "Nodes", emit(Edit::OpenGenerator)),
        button::compact(
            id::MATERIAL_TEXTURE_SET,
            "Texture Set",
            emit(Edit::OpenTextureSet),
        ),
        iced::widget::space::horizontal(),
    ]
    .spacing(4)
    .align_y(iced::Alignment::Center);

    let preview = material_preview::view(
        id::MATERIAL_PREVIEW,
        view.preview,
        230.0,
        emit(Edit::PreviewPressed),
        {
            let emit = emit.clone();
            move |point| emit(Edit::PreviewMoved(point))
        },
        emit(Edit::PreviewReleased),
    );
    let preview_hint = row![
        text(format!(
            "{:.0}° / {:.0}°",
            view.preview_yaw, view.preview_pitch
        ))
        .size(9)
        .color(theme::TEXT_MUTED),
        iced::widget::space::horizontal(),
        text(if view.engine_ready {
            "Drag orbit"
        } else {
            "Engine off"
        })
        .size(9)
        .color(theme::TEXT_MUTED),
    ];
    let shape_emit = emit.clone();
    let shape_options = vec!["Sphere".to_owned(), "Cube".to_owned(), "Plane".to_owned()];
    let shape_lookup = shape_options.clone();
    let preview_shape = dropdown::property(
        id::MATERIAL_PREVIEW_SHAPE,
        "Shape",
        shape_options,
        shape_lookup.get(view.preview_shape.min(2)).cloned(),
        move |choice| {
            let index = shape_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            shape_emit(Edit::SetPreviewShape(index))
        },
    );
    let environment_emit = emit.clone();
    let environment_clear = emit.clone();
    let environment_ready = !view.preview_environment.is_empty();
    let preview_environment = row![
        text(if environment_ready {
            view.preview_environment
        } else {
            "Studio environment"
        })
        .size(9)
        .color(theme::TEXT_MUTED)
        .width(Length::Fill),
        button::compact(
            id::MATERIAL_PREVIEW_ENVIRONMENT,
            "HDRI",
            environment_emit(Edit::OpenPreviewEnvironment)
        ),
    ]
    .spacing(3)
    .align_y(iced::Alignment::Center);
    let clear_environment = environment_ready.then(|| {
        button::compact(
            id::MATERIAL_PREVIEW_ENVIRONMENT_CLEAR,
            "Clear",
            environment_clear(Edit::ClearPreviewEnvironment),
        )
    });
    let preview_environment: Element<'a, Message> = if let Some(clear) = clear_environment {
        row![preview_environment, clear].spacing(3).into()
    } else {
        preview_environment.into()
    };
    let exposure_emit = emit.clone();
    let preview_exposure = slider::property(
        id::MATERIAL_PREVIEW_EXPOSURE,
        "Exposure",
        0.1..=4.0,
        view.preview_exposure,
        move |value| exposure_emit(Edit::SetPreviewExposure(value)),
    );
    let sun_yaw = {
        let emit = emit.clone();
        slider::property_step(
            id::MATERIAL_SUN_YAW,
            "Light direction",
            -180.0..=180.0,
            view.sun_yaw,
            1.0,
            move |value| emit(Edit::SetSunYaw(value)),
        )
    };
    let sun_elevation = {
        let emit = emit.clone();
        slider::property_step(
            id::MATERIAL_SUN_ELEVATION,
            "Light height",
            2.0..=89.0,
            view.sun_elevation,
            1.0,
            move |value| emit(Edit::SetSunElevation(value)),
        )
    };
    let sun_intensity = {
        let emit = emit.clone();
        slider::property(
            id::MATERIAL_SUN_INTENSITY,
            "Light intensity",
            0.0..=6.0,
            view.sun_intensity,
            move |value| emit(Edit::SetSunIntensity(value)),
        )
    };
    let preview_shadows = {
        let emit = emit.clone();
        checkbox::property(
            id::MATERIAL_PREVIEW_SHADOWS,
            "Shadows",
            view.preview_shadows,
            move |value| emit(Edit::SetPreviewShadows(value)),
        )
    };
    let map_access = |label: &str, filled: bool, detail: &str| {
        if detail == "missing" {
            status::chip(format!("{label} missing"), theme::ChipTone::Danger)
        } else if !detail.is_empty() {
            status::chip(format!("{label} {detail}"), theme::ChipTone::Success)
        } else {
            status::chip(
                label,
                if filled {
                    theme::ChipTone::Success
                } else {
                    theme::ChipTone::Neutral
                },
            )
        }
    };
    let mut map_summary = row![
        map_access(
            "Albedo",
            !material.albedo_map.is_empty(),
            view.map_details[0]
        ),
        map_access(
            "Normal",
            !material.normal_map.is_empty(),
            view.map_details[1]
        ),
        map_access("ORM", !material.orm_map.is_empty(), view.map_details[2]),
        map_access(
            "Emission",
            !material.emission_map.is_empty(),
            view.map_details[3]
        ),
    ]
    .spacing(3)
    .align_y(iced::Alignment::Center);
    map_summary = map_summary.push(status::chip(
        if material.normal_directx {
            "Normal: DirectX"
        } else {
            "Normal: OpenGL"
        },
        if material.normal_directx {
            theme::ChipTone::Accent
        } else {
            theme::ChipTone::Neutral
        },
    ));
    if material.alpha_mode != MaterialAlphaMode::Opaque {
        map_summary = map_summary.push(status::chip(
            if material.alpha_mode == MaterialAlphaMode::Cutout {
                "Alpha: cutout"
            } else {
                "Alpha: blend"
            },
            theme::ChipTone::Warning,
        ));
    }

    let preset_options: Vec<PresetOption> = view
        .presets
        .iter()
        .enumerate()
        .map(|(index, preset)| PresetOption {
            index,
            label: format!("{} · {}", preset.category, preset.name),
        })
        .collect();
    let emit_preset = emit.clone();
    let preset = dropdown::property(
        id::MATERIAL_PRESET,
        "Preset",
        preset_options,
        None,
        move |choice| emit_preset(Edit::ApplyPreset(choice.index)),
    );

    let mut content = Column::new().spacing(6);
    content = content.push(toolbar);
    content = content.push(preview);
    content = content.push(preview_hint);
    content = content.push(preview_shape);
    content = content.push(preview_environment);
    content = content.push(preview_exposure);
    content = content.push(sun_yaw);
    content = content.push(sun_elevation);
    content = content.push(sun_intensity);
    content = content.push(preview_shadows);
    content = content.push(map_summary);
    content = content.push(
        container(
            column![
                text(material.name.clone()).size(13).color(theme::TEXT),
                text(format!("{path}{status}"))
                    .size(9)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(0),
        )
        .padding([2, 4]),
    );
    content = content.push(text_field::property(
        id::MATERIAL_NAME,
        "Name",
        &material.name,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Name(value))
        },
    ));
    content = content.push(preset);

    content = content.push(section(
        view.collapsed,
        Section::Surface,
        "Surface",
        "◆",
        surface_body(material, emit.clone()),
        emit(Edit::ToggleSection(Section::Surface)),
    ));
    content = content.push(section(
        view.collapsed,
        Section::Maps,
        "Maps",
        "▦",
        maps_body(&view, emit.clone()),
        emit(Edit::ToggleSection(Section::Maps)),
    ));
    content = content.push(section(
        view.collapsed,
        Section::Uv,
        "UV & detail",
        "⌗",
        uv_body(material, emit.clone()),
        emit(Edit::ToggleSection(Section::Uv)),
    ));
    content = content.push(section(
        view.collapsed,
        Section::Advanced,
        "Advanced surface",
        "✦",
        advanced_body(material, emit.clone()),
        emit(Edit::ToggleSection(Section::Advanced)),
    ));
    content = content.push(section(
        view.collapsed,
        Section::Baker,
        "Map baker",
        "⚙",
        baker_body(&view, emit.clone()),
        emit(Edit::ToggleSection(Section::Baker)),
    ));

    container(
        crate::uikit::components::scroll::vertical(container(content).padding([0, 8]))
            .id(id::MATERIAL_SCROLL),
    )
    .padding(6)
    .into()
}

fn section<'a, Message: Clone + 'a>(
    collapsed: &HashSet<Section>,
    section: Section,
    title: &'a str,
    icon: &'a str,
    body: Element<'a, Message>,
    toggle: Message,
) -> Element<'a, Message> {
    card::view(
        title,
        icon,
        collapsed.contains(&section),
        id::material_section(match section {
            Section::Surface => "surface",
            Section::Maps => "maps",
            Section::Uv => "uv",
            Section::Advanced => "advanced",
            Section::Baker => "baker",
        })
        .into(),
        body,
        toggle,
    )
}

fn surface_body<'a, Message: Clone + 'a>(
    material: &'a Material,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let albedo_emit = emit.clone();
    let emission_emit = emit.clone();
    let alpha_emit = emit.clone();
    let cutoff_emit = emit.clone();
    let alpha_options = vec!["Opaque".to_owned(), "Cutout".to_owned(), "Blend".to_owned()];
    let alpha_index = match material.alpha_mode {
        MaterialAlphaMode::Opaque => 0,
        MaterialAlphaMode::Cutout => 1,
        MaterialAlphaMode::Blend => 2,
    };
    let alpha_lookup = alpha_options.clone();
    let alpha = dropdown::property(
        id::material_field("alpha_mode"),
        "Alpha",
        alpha_options,
        Some(alpha_lookup[alpha_index].clone()),
        move |choice| {
            let index = alpha_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            alpha_emit(Edit::AlphaMode(index))
        },
    );

    let mut body = Column::new().spacing(4);
    body = body.push(color_picker::view(
        "Albedo",
        material.albedo,
        |axis| id::material_axis("albedo", axis).into(),
        move |axis, value| albedo_emit(Edit::Albedo(axis, value)),
    ));
    body = body.push(slider::property(
        id::material_field("metallic"),
        "Metallic",
        0.0..=1.0,
        material.metallic,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Metallic(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("roughness"),
        "Roughness",
        0.0..=1.0,
        material.roughness,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Roughness(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("reflectance"),
        "Reflectance",
        0.0..=1.0,
        material.reflectance,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Reflectance(value))
        },
    ));
    body = body.push(color_picker::view(
        "Emission",
        [
            material.emission[0],
            material.emission[1],
            material.emission[2],
            1.0,
        ],
        |axis| id::material_axis("emission", axis).into(),
        move |axis, value| emission_emit(Edit::Emission(axis, value)),
    ));
    body = body.push(slider::property(
        id::material_field("emission_strength"),
        "Emission strength",
        0.0..=20.0,
        material.emission_strength,
        {
            let emit = emit.clone();
            move |value| emit(Edit::EmissionStrength(value))
        },
    ));
    body = body.push(alpha);
    body = body.push(slider::property(
        id::material_field("alpha_cutoff"),
        "Alpha cutoff",
        0.0..=1.0,
        material.alpha_cutoff,
        move |value| cutoff_emit(Edit::AlphaCutoff(value)),
    ));
    body.into()
}

fn maps_body<'a, Message: Clone + 'a>(
    view: &MaterialView<'a>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let material = view.material;
    let mut body = Column::new().spacing(3);

    let slots = [
        (
            TextureSlot::Albedo,
            id::MATERIAL_TEXTURE_ALBEDO,
            &material.albedo_map,
            view.thumbnails[0],
        ),
        (
            TextureSlot::Normal,
            id::MATERIAL_TEXTURE_NORMAL,
            &material.normal_map,
            view.thumbnails[1],
        ),
        (
            TextureSlot::Orm,
            id::MATERIAL_TEXTURE_ORM,
            &material.orm_map,
            view.thumbnails[2],
        ),
        (
            TextureSlot::Emission,
            id::MATERIAL_TEXTURE_EMISSION,
            &material.emission_map,
            view.thumbnails[3],
        ),
    ];
    for (slot, slot_id, path, thumbnail) in slots {
        let pick = emit.clone();
        let clear = emit.clone();
        let clear_message = (!path.is_empty()).then(|| clear(Edit::ClearTexture(slot)));
        body = body.push(texture_slot::view(
            texture_slot::Slot {
                id: slot_id,
                label: slot.label(),
                path,
                thumbnail,
            },
            pick(Edit::OpenTexturePicker(slot)),
            clear_message,
            Some(emit(Edit::DropTextureAsset(slot))),
        ));
    }
    body = body.push(slider::property(
        id::material_field("normal_strength"),
        "Normal strength",
        0.0..=4.0,
        material.normal_strength,
        {
            let emit = emit.clone();
            move |value| emit(Edit::NormalStrength(value))
        },
    ));
    body = body.push(slider::property_step(
        id::material_field("height_scale"),
        "Parallax depth",
        0.0..=0.25,
        material.height_scale,
        0.005,
        {
            let emit = emit.clone();
            move |value| emit(Edit::HeightScale(value))
        },
    ));
    if material.height_scale > 0.0 {
        body = body.push(slider::property_step(
            id::material_field("parallax_steps"),
            "Parallax steps",
            4.0..=96.0,
            material.parallax_steps,
            1.0,
            {
                let emit = emit.clone();
                move |value| emit(Edit::ParallaxSteps(value))
            },
        ));
    }
    body = body.push(checkbox::property(
        id::material_field("displacement_mode"),
        "Geometric displacement (real relief)",
        material.displacement_mode == 1,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Geometric(value))
        },
    ));
    if material.displacement_mode == 1 {
        body = body.push(slider::property_step(
            id::material_field("displacement_scale"),
            "Displacement depth (m)",
            0.0..=0.5,
            material.displacement_scale,
            0.005,
            {
                let emit = emit.clone();
                move |value| emit(Edit::DisplacementScale(value))
            },
        ));
        body = body.push(slider::property_step(
            id::material_field("mesh_subdivisions"),
            "Mesh subdivisions",
            1.0..=9.0,
            material.mesh_subdivisions as f32,
            1.0,
            {
                let emit = emit.clone();
                move |value| emit(Edit::Subdivisions(value))
            },
        ));
    }
    body = body.push(button::block(
        id::MATERIAL_PACK_HEIGHT,
        "Pack a height map into the normal…",
        emit(Edit::OpenHeightPicker),
    ));
    body = body.push(checkbox::property(
        id::material_field("normal_directx"),
        "DirectX normal (green down)",
        material.normal_directx,
        {
            let emit = emit.clone();
            move |value| emit(Edit::NormalDirectX(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("ao_strength"),
        "AO strength",
        0.0..=1.0,
        material.ao_strength,
        move |value| emit(Edit::AoStrength(value)),
    ));
    body.into()
}

fn uv_body<'a, Message: Clone + 'a>(
    material: &'a Material,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let mut body = Column::new().spacing(4);
    body = body.push(checkbox::property(
        id::material_field("uv_object_scale"),
        "Scale UVs with the object",
        material.uv_object_scale,
        {
            let emit = emit.clone();
            move |value| emit(Edit::UvObjectScale(value))
        },
    ));
    for axis in 0..2 {
        let emit = emit.clone();
        let label = if axis == 0 { "Tiling X" } else { "Tiling Y" };
        body = body.push(slider::property(
            id::material_axis("uv_tiling", axis),
            label,
            0.01..=16.0,
            material.uv_tiling[axis],
            move |value| emit(Edit::UvTiling(axis, value)),
        ));
    }
    for axis in 0..2 {
        let emit = emit.clone();
        let label = if axis == 0 { "Offset X" } else { "Offset Y" };
        body = body.push(slider::property(
            id::material_axis("uv_offset", axis),
            label,
            -4.0..=4.0,
            material.uv_offset[axis],
            move |value| emit(Edit::UvOffset(axis, value)),
        ));
    }
    body.into()
}

fn advanced_body<'a, Message: Clone + 'a>(
    material: &'a Material,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let sheen_emit = emit.clone();
    let mut body = Column::new().spacing(4);
    body = body.push(slider::property(
        id::material_field("transmission"),
        "Transmission",
        0.0..=1.0,
        material.transmission,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Transmission(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("ior"),
        "IOR",
        1.0..=3.0,
        material.ior,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Ior(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("clearcoat"),
        "Clearcoat",
        0.0..=1.0,
        material.clearcoat,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Clearcoat(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("clearcoat_roughness"),
        "Clearcoat roughness",
        0.0..=1.0,
        material.clearcoat_roughness,
        {
            let emit = emit.clone();
            move |value| emit(Edit::ClearcoatRoughness(value))
        },
    ));
    body = body.push(color_picker::view(
        "Sheen",
        [
            material.sheen_color[0],
            material.sheen_color[1],
            material.sheen_color[2],
            1.0,
        ],
        |axis| id::material_axis("sheen", axis).into(),
        move |axis, value| sheen_emit(Edit::Sheen(axis, value)),
    ));
    body = body.push(slider::property(
        id::material_field("sheen_roughness"),
        "Sheen roughness",
        0.0..=1.0,
        material.sheen_roughness,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SheenRoughness(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("iridescence"),
        "Iridescence",
        0.0..=1.0,
        material.iridescence,
        {
            let emit = emit.clone();
            move |value| emit(Edit::Iridescence(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("iridescence_thickness"),
        "Film thickness (nm)",
        50.0..=1500.0,
        material.iridescence_thickness,
        {
            let emit = emit.clone();
            move |value| emit(Edit::IridescenceThickness(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("iridescence_ior"),
        "Film IOR",
        1.0..=2.5,
        material.iridescence_ior,
        {
            let emit = emit.clone();
            move |value| emit(Edit::IridescenceIor(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("reflection"),
        "Reflection",
        0.0..=1.0,
        material.reflection,
        move |value| emit(Edit::Reflection(value)),
    ));
    body.into()
}

fn baker_body<'a, Message: Clone + 'a>(
    view: &MaterialView<'a>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let mut body = Column::new().spacing(4);
    let source = view.source;
    let pick = emit.clone();
    let clear = emit.clone();
    body = body.push(texture_slot::view(
        texture_slot::Slot {
            id: id::MATERIAL_TEXTURE_SOURCE,
            label: "Source (height)",
            path: source,
            thumbnail: view.source_thumbnail,
        },
        pick(Edit::OpenTexturePicker(TextureSlot::Source)),
        (!source.is_empty()).then(|| clear(Edit::ClearTexture(TextureSlot::Source))),
        Some(emit(Edit::DropTextureAsset(TextureSlot::Source))),
    ));
    body = body.push(
        text("Defaults to the albedo map when empty. Outputs are written beside the .mat file.")
            .size(9)
            .color(theme::TEXT_MUTED),
    );
    body = body.push(slider::property(
        id::material_field("bake_strength"),
        "Strength",
        0.0..=4.0,
        view.bake_strength,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetBakeStrength(value))
        },
    ));
    body = body.push(slider::property(
        id::material_field("bake_radius"),
        "AO radius (px)",
        1.0..=64.0,
        view.bake_radius,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetBakeRadius(value))
        },
    ));
    let packing_options = vec![
        "ORM (glTF / Unreal / Godot)".to_owned(),
        "Unity HDRP mask map".to_owned(),
        "Unity metallic-smoothness".to_owned(),
    ];
    let packing_selected = packing_options
        .get(view.packing.min(2))
        .cloned()
        .unwrap_or_else(|| packing_options[0].clone());
    let packing_lookup = packing_options.clone();
    let packing_emit = emit.clone();
    body = body.push(dropdown::property(
        id::material_field("bake_packing"),
        "Packing",
        packing_options,
        Some(packing_selected),
        move |choice| {
            let index = packing_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            packing_emit(Edit::SetPacking(index))
        },
    ));
    body = body.push(checkbox::property(
        id::MATERIAL_BAKE_DIRECTX,
        "DirectX normal (green down)",
        view.directx,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetDirectX(value))
        },
    ));
    body = body.push(checkbox::property(
        id::MATERIAL_BAKE_WRAP,
        "Tileable (wrap edges)",
        view.wrap,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetWrap(value))
        },
    ));
    let mut buttons = row![].spacing(4);
    for (operation, label, button_id) in [
        (MapOperation::Normal, "Normal", id::MATERIAL_BAKE_NORMAL),
        (MapOperation::AmbientOcclusion, "AO", id::MATERIAL_BAKE_AO),
        (
            MapOperation::Roughness,
            "Roughness",
            id::MATERIAL_BAKE_ROUGHNESS,
        ),
        (
            MapOperation::Metallic,
            "Metallic",
            id::MATERIAL_BAKE_METALLIC,
        ),
        (MapOperation::Orm, "ORM", id::MATERIAL_BAKE_ORM),
        (MapOperation::All, "Generate All", id::MATERIAL_BAKE_ALL),
    ] {
        let emit = emit.clone();
        buttons = buttons.push(button::compact(
            button_id,
            label,
            emit(Edit::Generate(operation)),
        ));
    }
    body = body.push(buttons);
    let scene_ao_emit = emit.clone();
    body = body.push(button::compact(
        id::MATERIAL_BAKE_SCENE_AO,
        "AO from scene",
        scene_ao_emit(Edit::BakeSceneAo {
            resolution: 512,
            rays: 16,
            max_distance: 2.5,
        }),
    ));
    body = body.push(
        text("Bakes AO from the selected entity's mesh and assigns its ORM map.")
            .size(9)
            .color(theme::TEXT_MUTED),
    );
    if let Some(status) = view.baking {
        body = body.push(text(status).size(9).color(theme::WARNING));
    }
    body.into()
}
