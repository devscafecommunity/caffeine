use super::erosion::{hydraulic, thermal, Hydraulic};
use super::field::{fbm, ridged, Field};
use super::*;

fn preset(name: &str) -> TerrainPreset {
    presets().into_iter().find(|p| p.name == name).expect(name)
}

fn rough(f: &Field) -> f32 {
    f.slope(1.0).data.iter().sum::<f32>() / f.data.len() as f32
}

#[test]
fn noise_is_unit_range_deterministic_and_resolution_independent() {
    let a = fbm(1.3, 2.7, 4, 6, 0.5, 2.0);
    assert_eq!(a, fbm(1.3, 2.7, 4, 6, 0.5, 2.0));
    assert!((0.0..=1.0).contains(&a) && (0.0..=1.0).contains(&ridged(0.4, 0.9, 1, 5, 2.0, 2.0)));
    assert_ne!(fbm(1.3, 2.7, 4, 6, 0.5, 2.0), fbm(1.3, 2.7, 5, 6, 0.5, 2.0));
    // The same graph at two resolutions shows the same landscape: compare at the shared corners.
    let g = preset("Rolling Hills").graph();
    let lo = g.build(65).unwrap().height;
    let hi = g.build(129).unwrap().height;
    for (x, y) in [(0usize, 0usize), (32, 32), (64, 64), (16, 48)] {
        let d = (lo.at(x, y) - hi.at(x * 2, y * 2)).abs();
        assert!(d < 0.03, "({x},{y}) differs by {d}");
    }
}

#[test]
fn every_preset_builds_a_valid_terrain() {
    for p in presets() {
        let g = p.graph();
        let m = g.build(97).unwrap_or_else(|e| panic!("{}: {e}", p.name));
        assert!(
            m.height
                .data
                .iter()
                .all(|v| (0.0..=1.0).contains(v) && v.is_finite()),
            "{}",
            p.name
        );
        let (lo, hi) = m.height.min_max();
        assert!(hi - lo > 0.25, "{} is nearly flat: {lo}..{hi}", p.name);
        assert_eq!(m.splat.len(), 97 * 97 * 4);
        // The splat weights always sum to ~1.
        for px in m.splat.chunks(4).step_by(37) {
            let s: u32 = px.iter().map(|v| *v as u32).sum();
            assert!((250..=260).contains(&s), "{}: weights sum to {s}", p.name);
        }
        assert!(m.max_height > 5.0 && m.size[0] >= 50.0);
    }
}

#[test]
fn thermal_erosion_softens_and_conserves() {
    let base = Field::from_fn(65, |x, y| ridged(x * 3.0, y * 3.0, 3, 6, 2.0, 2.0));
    let eroded = thermal(&base, 40, 0.3, 0.8);
    assert!(
        rough(&eroded) < rough(&base) * 0.95,
        "{} vs {}",
        rough(&eroded),
        rough(&base)
    );
    let (a, b): (f32, f32) = (base.data.iter().sum(), eroded.data.iter().sum());
    assert!(
        (a - b).abs() / a < 0.002,
        "material is conserved: {a} vs {b}"
    );
}

#[test]
fn hydraulic_erosion_carves_and_deposits_deterministically() {
    let base = Field::from_fn(129, |x, y| {
        (ridged(x * 2.0, y * 2.0, 2, 6, 2.0, 2.0) * 0.8 + 0.1)
            * (1.0 - ((x - 0.5).powi(2) + (y - 0.5).powi(2)).sqrt())
    });
    let params = Hydraulic {
        droplets: 20_000,
        inertia: 0.12,
        capacity: 1.4,
        erosion: 0.35,
        deposition: 0.3,
        evaporation: 0.015,
        seed: 1,
    };
    let a = hydraulic(&base, params);
    let b = hydraulic(&base, params);
    assert_eq!(a.height, b.height, "deterministic");
    assert_ne!(a.height, base, "the terrain changes");
    assert!(a.wear.data.iter().any(|v| *v > 0.5) && a.deposit.data.iter().any(|v| *v > 0.5));
    let mean_change = a
        .height
        .zip(&base, |x, y| (x - y).abs())
        .data
        .iter()
        .sum::<f32>()
        / base.data.len() as f32;
    assert!(mean_change > 1e-4 && mean_change < 0.1, "{mean_change}");
    assert!(a.height.data.iter().all(|v| v.is_finite()));
}

#[test]
fn nodes_behave() {
    let mut g = TerrainGraph::new();
    let out = g.add("output", Point::ORIGIN);
    let noise = g.add("perlin", Point::ORIGIN);
    let terrace = g.add_with(
        "terrace",
        Point::ORIGIN,
        &[("steps", 5.0), ("sharpness", 1.0)],
    );
    assert!(g.connect(noise, 0, terrace, 0));
    assert!(g.connect(terrace, 0, out, 0));
    let flat = g.evaluate(terrace, 129).unwrap().remove(0);
    // Terraces gather heights near the step levels: more mass close to k/5 than a smooth field has.
    let near = |f: &Field| {
        f.data
            .iter()
            .filter(|v| (((**v) * 5.0).fract() < 0.12) || (**v * 5.0).fract() > 0.88)
            .count()
    };
    let smooth = g.evaluate(noise, 129).unwrap().remove(0);
    assert!(
        near(&flat) > near(&smooth) + 1000,
        "{} vs {}",
        near(&flat),
        near(&smooth)
    );
    // Invert, Levels and Combine.
    let inv = {
        let i = g.add("invert", Point::ORIGIN);
        assert!(g.connect(noise, 0, i, 0));
        g.evaluate(i, 33).unwrap().remove(0)
    };
    let raw = g.evaluate(noise, 33).unwrap().remove(0);
    assert!((inv.data[100] + raw.data[100] - 1.0).abs() < 1e-5);
    // Height Range selects mid heights; Slope is high on steep ground.
    let rng = g.add_with(
        "range",
        Point::ORIGIN,
        &[("low", 0.4), ("high", 0.6), ("falloff", 0.02)],
    );
    assert!(g.connect(noise, 0, rng, 0));
    let m = g.evaluate(rng, 65).unwrap().remove(0);
    let n = g.evaluate(noise, 65).unwrap().remove(0);
    for i in (0..m.data.len()).step_by(13) {
        if (0.42..=0.58).contains(&n.data[i]) {
            assert!(m.data[i] > 0.95);
        }
        if n.data[i] < 0.3 || n.data[i] > 0.7 {
            assert!(m.data[i] < 0.05);
        }
    }
}

#[test]
fn connections_refuse_cycles_and_bad_ports() {
    let mut g = TerrainGraph::new();
    let out = g.add("output", Point::ORIGIN);
    let a = g.add("perlin", Point::ORIGIN);
    let b = g.add("levels", Point::ORIGIN);
    assert!(g.connect(a, 0, b, 0) && g.connect(b, 0, out, 0));
    assert!(!g.connect(out, 0, b, 0), "the Output has no outputs");
    assert!(!g.connect(a, 3, b, 0), "no such output");
    assert!(!g.connect(a, 0, b, 5), "no such input");
    let c = g.add("blur", Point::ORIGIN);
    assert!(g.connect(b, 0, c, 0));
    assert!(!g.connect(c, 0, b, 0), "no cycles");
    assert!(TerrainGraph::new().build(33).is_err());
    let mut empty = TerrainGraph::new();
    empty.add("output", Point::ORIGIN);
    assert!(empty.build(33).unwrap_err().contains("Wire a height"));
}

#[test]
fn splat_layers_follow_height_and_slope() {
    let g = preset("Volcanic Island").graph();
    let m = g.build(129).unwrap();
    let sea = m.sea_metres / m.max_height;
    let layers = |i: usize| {
        (
            m.splat[i * 4],
            m.splat[i * 4 + 1],
            m.splat[i * 4 + 2],
            m.splat[i * 4 + 3],
        )
    };
    let (mut mud_under_sea, mut ground_somewhere, mut rock_somewhere) = (0, 0, 0);
    for i in 0..129 * 129 {
        let (ground, rock, _, mud) = layers(i);
        if m.height.data[i] < sea - 0.03 && mud > 200 {
            mud_under_sea += 1;
        }
        if ground > 200 {
            ground_somewhere += 1;
        }
        if rock > 200 {
            rock_somewhere += 1;
        }
    }
    assert!(mud_under_sea > 500, "the sea floor is mud: {mud_under_sea}");
    assert!(
        ground_somewhere > 200 && rock_somewhere > 200,
        "{ground_somewhere} / {rock_somewhere}"
    );
}

#[test]
fn text_round_trip_and_seeds() {
    for p in presets() {
        let g = p.graph();
        let text = to_text(p.name, &g);
        let (name, back) = from_text(&text).unwrap_or_else(|| panic!("{}", p.name));
        assert_eq!(name, p.name);
        assert_eq!(back.build(33).unwrap(), g.build(33).unwrap(), "{}", p.name);
    }
    assert!(from_text("nope").is_none());
    let mut g = preset("Alpine Range").graph();
    let before = g.build(65).unwrap().height;
    let ridge = g.nodes.iter().find(|n| n.op == "ridged").unwrap().id;
    g.set(ridge, "seed", 42.0);
    assert_ne!(g.build(65).unwrap().height, before);
}

#[test]
fn preview_renders_and_orbits() {
    let m = preset("Alpine Range").graph().build(97).unwrap();
    let a = preview::render(&m, 0.1, 160, 120, preview::View::default());
    let b = preview::render(
        &m,
        0.1,
        160,
        120,
        preview::View {
            yaw: 2.0,
            ..Default::default()
        },
    );
    assert_eq!(a.len(), 160 * 120 * 4);
    assert_ne!(a, b, "orbiting changes the picture");
    // Land is drawn over the background: the centre differs from the corner.
    let centre = &a[((60 * 160 + 80) * 4)..((60 * 160 + 80) * 4 + 3)];
    assert_ne!(centre, &a[0..3]);
}

/// Writes the preview of every preset to `DOPPIO_TERRAIN_DUMP` (documentation images).
#[test]
fn terrain_presets_dump_for_review() {
    let Ok(dir) = std::env::var("DOPPIO_TERRAIN_DUMP") else {
        return;
    };
    for p in presets() {
        let g = p.graph();
        let m = g.build(g.output_resolution()).unwrap();
        let sea = m.sea_metres / m.max_height;
        let pixels = preview::render(&m, sea, 480, 360, preview::View::default());
        image::save_buffer(
            format!("{dir}/{}.png", p.name.to_lowercase().replace(' ', "_")),
            &pixels,
            480,
            360,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
}
