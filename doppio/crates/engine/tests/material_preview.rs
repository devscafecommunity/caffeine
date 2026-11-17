//! GPU material-preview regression tests.
//!
//! These live in their own integration-test process so a long UI suite cannot
//! exhaust the SDL GPU device pool before a preview test runs.

use doppio_engine::{Engine, Material, PreviewShape, RenderState};
use image::{Rgba, RgbaImage};
use std::sync::{Mutex, MutexGuard};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

fn guard() -> MutexGuard<'static, ()> {
    static LOCK: Mutex<()> = Mutex::new(());
    LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn engine() -> Option<Engine> {
    Engine::load_default()
}

fn temp_root(label: &str) -> std::path::PathBuf {
    std::env::temp_dir().join(format!(
        "doppio-preview-{label}-{}-{}",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ))
}

#[test]
fn shapes_and_async_readback_work() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _guard = guard();
    let Some(mut engine) = engine() else {
        return;
    };
    assert!(engine.capi_version() >= 2);
    let material = Material::default();
    let mut output = vec![0u8; 64 * 64 * 4];
    engine
        .begin_material_preview(&material, 64, 20.0, 12.0, PreviewShape::Cube, "", 1.0)
        .expect("begin preview");
    let mut ready = false;
    for _ in 0..200 {
        match engine.poll_material_preview(&mut output) {
            Ok(RenderState::Ready) => {
                ready = true;
                break;
            }
            Ok(RenderState::Pending) => std::thread::sleep(Duration::from_millis(5)),
            _ => break,
        }
    }
    assert!(ready, "async material preview should finish");
    assert!(output.chunks(4).any(|pixel| pixel[3] > 0));
    let sphere = engine
        .render_material_preview_ex(&material, 64, 20.0, 12.0, PreviewShape::Sphere, "", 1.0)
        .expect("sphere preview");
    assert_ne!(sphere, output, "cube and sphere previews should differ");
}

#[test]
fn textures_rewritten_in_place_reload() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _guard = guard();
    let Some(mut engine) = engine() else {
        return;
    };
    let root = temp_root("reload");
    std::fs::create_dir_all(&root).expect("temp dir");
    let swatch = root.join("swatch.png");
    let save = |color: [u8; 4]| {
        RgbaImage::from_pixel(64, 64, Rgba(color))
            .save(&swatch)
            .expect("write swatch");
    };
    save([220, 30, 30, 255]);

    let material = Material {
        albedo_map: swatch.display().to_string(),
        roughness: 0.65,
        ..Material::default()
    };
    let render = |engine: &mut Engine| {
        engine
            .render_material_preview_ex(&material, 128, 22.0, 14.0, PreviewShape::Cube, "", 1.0)
            .expect("preview")
    };
    let red = render(&mut engine);
    std::thread::sleep(Duration::from_millis(20));
    save([30, 30, 220, 255]);
    let blue = render(&mut engine);
    assert_ne!(
        red, blue,
        "rewriting a baked texture in place must refresh the material preview"
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn sun_and_shadows_change_the_render() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _guard = guard();
    let Some(mut engine) = engine() else {
        return;
    };
    let material = Material {
        displacement_mode: 1,
        ..Material::default()
    };
    let render = |engine: &mut Engine, yaw: f32, shadows: bool| {
        engine.set_material_preview_light(yaw, 25.0, 3.0, shadows);
        engine
            .render_material_preview_ex(&material, 128, 0.0, 25.0, PreviewShape::Sphere, "", 1.0)
            .expect("preview")
    };
    let base = render(&mut engine, 41.0, true);
    let moved = render(&mut engine, 180.0, true);
    let unshadowed = render(&mut engine, 180.0, false);
    assert_ne!(base, moved, "moving the sun must change the preview");
    assert_ne!(
        moved, unshadowed,
        "toggling shadows must change the preview"
    );
    let bottom_mean = |pixels: &[u8]| {
        let rows = pixels[100 * 128 * 4..].chunks(4);
        let count = rows.len() as f32;
        rows.map(|px| f32::from(px[0]) + f32::from(px[1]) + f32::from(px[2]))
            .sum::<f32>()
            / count
    };
    let strict = std::env::var("DOPPIO_STRICT_PREVIEW_SHADOW").is_ok();
    assert!(
        !strict || bottom_mean(&moved) < bottom_mean(&unshadowed) * 0.7,
        "the sphere must cast a shadow on the floor ({} vs {})",
        bottom_mean(&moved),
        bottom_mean(&unshadowed)
    );
}

#[test]
fn uv_tiling_changes_the_render() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _guard = guard();
    let Some(mut engine) = engine() else {
        return;
    };
    let root = temp_root("tiling");
    std::fs::create_dir_all(&root).expect("temp dir");
    let checker = root.join("checker.png");
    let mut image = RgbaImage::new(64, 64);
    for (x, y, pixel) in image.enumerate_pixels_mut() {
        *pixel = if (x / 16 + y / 16) % 2 == 0 {
            Rgba([240, 240, 240, 255])
        } else {
            Rgba([20, 20, 20, 255])
        };
    }
    image.save(&checker).expect("checker");
    let render = |engine: &mut Engine, tiling: [f32; 2]| {
        let material = Material {
            albedo_map: checker.display().to_string(),
            uv_tiling: tiling,
            ..Material::default()
        };
        engine
            .render_material_preview_ex(&material, 128, 0.0, 70.0, PreviewShape::Plane, "", 1.0)
            .expect("preview")
    };
    let one = render(&mut engine, [1.0, 1.0]);
    let four = render(&mut engine, [4.0, 4.0]);
    assert_ne!(one, four, "tiling 4 must repeat the texture");
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn water_surface_round_trips_and_follows_its_waves() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    if engine.capi_version() < 7 {
        return;
    }
    let entity = engine.create_entity("Lake").expect("entity");
    let water = doppio_engine::Water {
        size: [40.0, 40.0],
        waves: vec![
            doppio_engine::WaterWave {
                direction: [1.0, 0.0],
                wavelength: 12.0,
                amplitude: 0.3,
                steepness: 0.2,
                phase: 0.0,
            },
            doppio_engine::WaterWave {
                direction: [0.0, 1.0],
                wavelength: 5.0,
                amplitude: 0.1,
                steepness: 0.2,
                phase: 1.0,
            },
        ],
        ..Default::default()
    };
    engine.set_water(entity, &water).expect("set water");
    let back = engine.water(entity).expect("water");
    assert_eq!(back.waves.len(), 2);
    assert_eq!(back.waves[0].wavelength, 12.0);
    assert_eq!(back.size, [40.0, 40.0]);
    for _ in 0..4 {
        engine.tick(0.016);
    }
    let height = engine.water_height(entity, 3.0, 2.0).expect("height");
    assert!(height.abs() <= 0.41, "{height}");
}

#[test]
fn terrain_from_generated_heights_round_trips() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    if engine.capi_version() < 8 {
        return;
    }
    let entity = engine.create_entity("Hills").expect("entity");
    let terrain = doppio_engine::Terrain {
        resolution: [33, 33],
        size: [100.0, 100.0],
        max_height: 40.0,
        cast_shadows: true,
    };
    let heights: Vec<f32> = (0..33 * 33).map(|i| (i % 33) as f32 / 32.0).collect();
    engine
        .set_terrain(entity, &terrain, &heights, None)
        .expect("set terrain");
    assert_eq!(engine.terrain(entity).expect("terrain"), terrain);
    let h = engine.terrain_height(entity, 50.0, 50.0).expect("height");
    assert!((h - 20.0).abs() < 0.5, "{h}");
    assert!(engine
        .set_terrain(entity, &terrain, &heights[..10], None)
        .is_err());
}

#[test]
fn terrain_mesh_follows_the_generated_heights() {
    let _guard = ENGINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    if engine.capi_version() < 8 {
        return;
    }
    let entity = engine.create_entity("Pyramid").expect("entity");

    let terrain = doppio_engine::Terrain {
        resolution: [65, 65],
        size: [200.0, 200.0],
        max_height: 80.0,
        cast_shadows: true,
    };
    let heights: Vec<f32> = (0..65 * 65)
        .map(|i| {
            let (x, z) = ((i % 65) as f32 / 64.0 - 0.5, (i / 65) as f32 / 64.0 - 0.5);
            (1.0 - (x * x + z * z).sqrt() * 2.0).max(0.0)
        })
        .collect();
    engine
        .set_terrain(entity, &terrain, &heights, None)
        .unwrap();
    let frame = |engine: &mut doppio_engine::Engine| {
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.0,
                pitch: 0.25,
                distance: 260.0,
                focus: [0.0, 20.0, 0.0],
            })
            .unwrap();
        engine.render_begin(320, 180).unwrap();
        let mut pixels = vec![0u8; 320 * 180 * 4];
        for _ in 0..400 {
            if matches!(
                engine.render_poll(&mut pixels).unwrap(),
                doppio_engine::RenderState::Ready
            ) {
                return pixels;
            }
            std::thread::sleep(std::time::Duration::from_millis(5));
        }
        panic!("render timed out");
    };
    let pixels = frame(&mut engine);
    if let Ok(dir) = std::env::var("DOPPIO_TERRAIN_DUMP") {
        image::save_buffer(
            format!("{dir}/engine_pyramid.png"),
            &pixels,
            320,
            180,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
    assert!(pixels.chunks(4).any(|p| p[0..3] != pixels[0..3]));
}

/// The terrain cache is process-wide (keyed by entity id) and a session's teardown clears it, so
/// engine sessions in this file must not overlap: every test holds this lock.
static ENGINE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
