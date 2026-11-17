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
