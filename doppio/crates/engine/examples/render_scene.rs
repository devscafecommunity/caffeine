//! Builds a tiny scene through the C ABI and renders it to a PNG, with no editor and no window.
//!
//! ```text
//! cmake --build build --target caffeine-capi -j
//! cd doppio && cargo run -p doppio-engine --example render_scene -- /tmp/scene.png
//! ```
//!
//! `CAFFEINE_CAPI_LIB` points at another `libcaffeine-capi` when it is not in `build/`.
//! See `caffeine-website/content/en/docs/api/headless-engine-api.md`.

use doppio_engine::{Engine, FieldValue};

/// Adds a component by name and sets fields by name: the whole "scene description" language.
struct Scene<'a> {
    engine: &'a mut Engine,
}

impl Scene<'_> {
    fn component(&mut self, entity: u32, name: &str) -> u32 {
        let type_id = self
            .engine
            .component_types()
            .iter()
            .find(|t| t.name == name)
            .unwrap_or_else(|| panic!("no component named {name}"))
            .id;
        self.engine.add_component(entity, type_id).expect("add");
        type_id
    }

    fn set(&mut self, entity: u32, type_id: u32, field: &str, value: FieldValue) {
        let component = self
            .engine
            .entity_components(entity)
            .iter()
            .position(|c| c.type_id == type_id)
            .expect("component was added") as u32;
        let index = self
            .engine
            .component_types()
            .iter()
            .find(|t| t.id == type_id)
            .and_then(|t| t.fields.iter().position(|f| f.name == field))
            .unwrap_or_else(|| panic!("no field named {field}")) as u32;
        self.engine
            .set_component_field(entity, component, index, &value)
            .expect("set field");
    }

    /// An entity drawn as a built-in shape (`Cube`, `Sphere`, `Plane`...).
    fn shape(&mut self, name: &str, primitive: &str, at: [f32; 3], scale: [f32; 3]) -> u32 {
        let entity = self.engine.create_entity(name).expect("entity");
        let filter = self.component(entity, "Mesh Filter");
        let option = self
            .engine
            .component_types()
            .iter()
            .find(|t| t.id == filter)
            .and_then(|t| t.fields.iter().find(|f| f.name == "Primitive"))
            .and_then(|f| f.options.iter().position(|o| o == primitive))
            .unwrap_or_else(|| panic!("no primitive named {primitive}"));
        self.set(entity, filter, "Primitive", FieldValue::Enum(option as i32));
        self.component(entity, "Mesh Renderer");
        self.engine.set_position(entity, at).expect("position");
        self.engine.set_scale(entity, scale).expect("scale");
        entity
    }
}

fn main() {
    let out = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "scene.png".to_owned());
    let mut engine =
        Engine::load_default().expect("libcaffeine-capi not found (see CAFFEINE_CAPI_LIB)");

    // A project folder for the materials (any folder works).
    let root = std::env::temp_dir().join("caffeine-render-scene");
    std::fs::create_dir_all(&root).expect("folder");
    engine
        .set_asset_root(&root.to_string_lossy())
        .expect("asset root");

    let mut scene = Scene {
        engine: &mut engine,
    };
    let ground = scene.shape("Ground", "Plane", [0.0, 0.0, 0.0], [10.0, 1.0, 10.0]);
    let cube = scene.shape("Cube", "Cube", [-1.2, 0.5, 0.0], [1.0, 1.0, 1.0]);
    let ball = scene.shape("Ball", "Sphere", [1.2, 0.5, 0.0], [1.0, 1.0, 1.0]);

    let sun = scene.engine.create_entity("Sun").expect("entity");
    let light = scene.component(sun, "Light");
    scene.set(sun, light, "Intensity", FieldValue::F32(1.2));
    scene.component(sun, "Directional Light");
    scene
        .engine
        .set_rotation(sun, [-40.0, 130.0, 0.0])
        .expect("rotation");

    // Materials are `.mat` assets: write them, then assign them to entities by path.
    for (entity, name, albedo, roughness) in [
        (ground, "ground", [0.25, 0.4, 0.2, 1.0], 0.9),
        (cube, "red", [0.8, 0.1, 0.1, 1.0], 0.4),
        (ball, "steel", [0.7, 0.7, 0.75, 1.0], 0.2),
    ] {
        let mut material = scene.engine.new_material().expect("material");
        material.albedo = albedo;
        material.roughness = roughness;
        let path = root.join(format!("{name}.mat"));
        scene
            .engine
            .save_material(&path.to_string_lossy(), &material)
            .expect("save");
        scene
            .engine
            .apply_material(entity, &path.to_string_lossy())
            .expect("apply");
    }

    // The camera orbits a focus point: yaw/pitch in radians, distance in metres.
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.6,
            pitch: 0.35,
            distance: 6.0,
            focus: [0.0, 0.5, 0.0],
        })
        .expect("camera");

    // The editor grid is on by default; a clean picture turns it off.
    let mut settings = engine.viewport_settings().expect("settings");
    settings.show_grid = false;
    engine.set_viewport_settings(settings).expect("settings");

    let (width, height) = (960, 540);
    let pixels = engine.render(width, height).expect("render");
    image::save_buffer(&out, &pixels, width, height, image::ColorType::Rgba8).expect("png");
    println!("wrote {out} ({width}x{height})");
}
