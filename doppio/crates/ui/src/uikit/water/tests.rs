use super::*;

fn preset(name: &str) -> WaterPreset {
    presets().into_iter().find(|p| p.name == name).expect(name)
}

/// Gives a hand-built graph the Water Material its Output requires.
fn with_material(g: &mut WaterGraph) {
    let out = g.output_node().unwrap();
    let look = g.add("look", Point::ORIGIN);
    assert!(g.connect(look, 0, out, 1));
}

fn peak(water: &Water) -> f32 {
    let mut peak = 0.0f32;
    for i in 0..60 {
        for j in 0..60 {
            let (h, ..) = preview::sample(water, i as f32 * 1.7, j as f32 * 1.7, 0.0);
            peak = peak.max(h.abs());
        }
    }
    peak
}

#[test]
fn every_preset_builds_valid_water() {
    for p in presets() {
        let graph = p.graph();
        let water = graph.build().unwrap_or_else(|e| panic!("{}: {e}", p.name));
        assert!(
            !water.waves.is_empty() && water.waves.len() <= MAX_WATER_WAVES,
            "{}",
            p.name
        );
        for w in &water.waves {
            assert!(
                w.wavelength > 0.0 && w.amplitude >= 0.0 && w.steepness <= 1.2,
                "{}: {w:?}",
                p.name
            );
            assert!(w.direction.iter().all(|v| v.is_finite()));
        }
        assert!(water.size[0] >= 2.0 && water.depth > 0.0, "{}", p.name);
        // The engine's Gerstner stability bound: crests never fold over themselves.
        let q: f32 = water
            .waves
            .iter()
            .map(|w| w.steepness * std::f32::consts::TAU / w.wavelength * w.amplitude)
            .sum();
        assert!(q <= 1.3, "{}: sum(Q k A) = {q}", p.name);
        assert!(find("output").is_some());
    }
}

#[test]
fn presets_have_the_right_character() {
    let sea = preset("Open Sea").graph().build().unwrap();
    let storm = preset("Storm").graph().build().unwrap();
    let lake = preset("Calm Lake").graph().build().unwrap();
    let pool = preset("Swimming Pool").graph().build().unwrap();
    assert!(
        peak(&storm) > peak(&sea) * 1.4,
        "{} vs {}",
        peak(&storm),
        peak(&sea)
    );
    assert!(
        peak(&sea) > peak(&lake) * 3.0,
        "{} vs {}",
        peak(&sea),
        peak(&lake)
    );
    assert!(peak(&lake) > 0.01 && peak(&pool) < 0.05);
    assert!(storm.albedo[2] < sea.albedo[2] + 0.2 && sea.depth > lake.depth);
    // Stormier water carries more foam: a lower threshold.
    assert!(storm.foam_threshold < sea.foam_threshold);
}

#[test]
fn graphs_are_deterministic_and_seeds_change_them() {
    let a = preset("Open Sea").graph().build().unwrap();
    let b = preset("Open Sea").graph().build().unwrap();
    assert_eq!(a, b);
    let mut graph = preset("Open Sea").graph();
    let wind = graph.nodes.iter().find(|n| n.op == "wind").unwrap().id;
    graph.set(wind, "seed", 77.0);
    assert_ne!(graph.build().unwrap().waves, a.waves);
}

#[test]
fn wind_raises_taller_longer_waves() {
    let height = |wind: f32| {
        let mut g = WaterGraph::new();
        let out = g.add("output", Point::ORIGIN);
        let w = g.add_with("wind", Point::ORIGIN, &[("wind", wind)]);
        assert!(g.connect(w, 0, out, 0));
        with_material(&mut g);
        g.build().unwrap()
    };
    let (calm, gale) = (height(4.0), height(20.0));
    let top = |w: &Water| w.waves.iter().map(|v| v.wavelength).fold(0.0, f32::max);
    assert!(top(&gale) > top(&calm));
    assert!(peak(&gale) > peak(&calm) * 3.0);
    // No wind: no waves wired, so the water is flat.
    let flat = height(0.0);
    assert_eq!(peak(&flat), 0.0);
}

#[test]
fn connections_check_port_types_and_refuse_cycles() {
    let mut g = WaterGraph::new();
    let out = g.add("output", Point::ORIGIN);
    let wave = g.add("wave", Point::ORIGIN);
    let look = g.add("look", Point::ORIGIN);
    let scale = g.add("scale", Point::ORIGIN);
    assert!(
        !g.connect(look, 0, out, 0),
        "a Look cannot feed a Waves port"
    );
    assert!(
        !g.connect(wave, 0, out, 1),
        "waves cannot feed the Look port"
    );
    assert!(g.connect(wave, 0, scale, 0));
    assert!(g.connect(scale, 0, out, 0));
    assert!(g.connect(look, 0, out, 1));
    assert!(!g.connect(out, 0, scale, 0), "the Output has no outputs");
    let merge = g.add("merge", Point::ORIGIN);
    assert!(g.connect(scale, 0, merge, 0));
    assert!(!g.connect(merge, 0, scale, 0), "no cycles");
}

#[test]
fn merge_keeps_the_strongest_sixteen_and_scale_modifies() {
    let mut g = WaterGraph::new();
    let out = g.add("output", Point::ORIGIN);
    let merge = g.add("merge", Point::ORIGIN);
    for i in 0..4 {
        let w = g.add_with(
            "wind",
            Point::ORIGIN,
            &[
                ("wind", 8.0 + i as f32 * 3.0),
                ("count", 12.0),
                ("seed", i as f32),
            ],
        );
        assert!(g.connect(w, 0, merge, i));
    }
    assert!(g.connect(merge, 0, out, 0));
    with_material(&mut g);
    let water = g.build().unwrap();
    assert_eq!(water.waves.len(), MAX_WATER_WAVES);
    let min = water
        .waves
        .iter()
        .map(|w| w.amplitude)
        .fold(f32::MAX, f32::min);
    assert!(min > 0.0005);

    let mut g = preset("Open Sea").graph();
    let before = peak(&g.build().unwrap());
    let out = g.output_node().unwrap();
    let scale = g.add_with("scale", Point::ORIGIN, &[("amplitude", 2.0)]);
    let merge = g
        .links
        .iter()
        .find(|l| l.to == out && l.input == 0)
        .unwrap()
        .from;
    assert!(g.connect(merge, 0, scale, 0));
    assert!(g.connect(scale, 0, out, 0));
    assert!(peak(&g.build().unwrap()) > before * 1.8);
}

#[test]
fn text_round_trip_keeps_everything() {
    for p in presets() {
        let graph = p.graph();
        let text = to_text(p.name, &graph);
        let (name, back) = from_text(&text).unwrap_or_else(|| panic!("{}", p.name));
        assert_eq!(name, p.name);
        assert_eq!(back.build().unwrap(), graph.build().unwrap(), "{}", p.name);
    }
    assert!(from_text("nope").is_none());
}

#[test]
fn preview_animates_and_frames_the_waves() {
    let water = preset("Open Sea").graph().build().unwrap();
    let a = preview::render(&water, 64, 0.0);
    let b = preview::render(&water, 64, 2.0);
    assert_eq!(a.len(), 64 * 64 * 4);
    assert_ne!(a, b, "time moves the waves");
    // The surface is not a flat colour.
    let first = &a[0..3];
    assert!((0..a.len() / 4).any(|i| a[i * 4..i * 4 + 3] != *first));
    let flat = WaterGraph::new();
    assert!(flat.build().is_err(), "an empty graph has no Output");
}

#[test]
fn waves_finer_than_the_mesh_fade_out_instead_of_aliasing() {
    let build = |size: f32, resolution: f32, wavelength: f32| {
        let mut g = WaterGraph::new();
        let out = g.add_with(
            "output",
            Point::ORIGIN,
            &[
                ("size_x", size),
                ("size_z", size),
                ("resolution", resolution),
            ],
        );
        let rip = g.add_with(
            "ripples",
            Point::ORIGIN,
            &[
                ("wavelength", wavelength),
                ("amplitude", 0.05),
                ("count", 3.0),
            ],
        );
        assert!(g.connect(rip, 0, out, 0));
        with_material(&mut g);
        g.build_report().unwrap()
    };
    // 100 m over 96 cells = 1.04 m per cell: 0.3 m ripples cannot be shown.
    let (flat, dropped) = build(100.0, 2.0, 0.3);
    assert_eq!(flat.waves.len(), 1);
    assert_eq!(flat.waves[0].amplitude, 0.0, "falls back to flat water");
    assert!(dropped >= 3, "{dropped}");
    // The same ripples on a small pond at a fine mesh are kept in full.
    let (pond, dropped) = build(16.0, 5.0, 0.8);
    assert_eq!(dropped, 0);
    assert!(pond.waves.iter().all(|w| w.amplitude > 0.02));
}

#[test]
fn presets_keep_every_wave_their_mesh_can_show() {
    for p in presets() {
        let (water, dropped) = p.graph().build_report().unwrap();
        let cell = water.size[0].max(water.size[1]) / water.resolution as f32;
        for w in &water.waves {
            if w.amplitude > 0.0 {
                assert!(
                    w.wavelength / cell >= 2.5,
                    "{}: {} m waves on {cell} m cells",
                    p.name,
                    w.wavelength
                );
            }
        }
        assert!(
            water.waves.iter().any(|w| w.amplitude > 0.0),
            "{} is flat",
            p.name
        );
        assert!(dropped <= 1, "{} drops {dropped} waves", p.name);
    }
}

#[test]
fn water_cannot_exist_without_its_material() {
    let mut g = WaterGraph::new();
    let out = g.add("output", Point::ORIGIN);
    let wave = g.add("wave", Point::ORIGIN);
    assert!(g.connect(wave, 0, out, 0));
    let error = g.build().unwrap_err();
    assert!(
        error.contains("no material") && error.contains("Water Material"),
        "{error}"
    );
    // Wiring the material makes it build, and the colour comes from that node.
    let look = g.add_with(
        "look",
        Point::ORIGIN,
        &[("r", 0.1), ("g", 0.6), ("b", 0.4), ("opacity", 0.5)],
    );
    assert!(g.connect(look, 0, out, 1));
    let water = g.build().unwrap();
    assert_eq!(water.albedo, [0.1, 0.6, 0.4, 0.5]);
    // Every preset wires an explicit Water Material node.
    for p in presets() {
        let graph = p.graph();
        let output = graph.output_node().unwrap();
        let link = graph
            .links
            .iter()
            .find(|l| l.to == output && l.input == 1)
            .expect(p.name);
        assert_eq!(graph.node(link.from).unwrap().op, "look", "{}", p.name);
    }
}

#[test]
fn old_graphs_without_a_material_get_the_default_one() {
    let mut graph = presets()[0].graph();
    let look = graph.nodes.iter().find(|n| n.op == "look").unwrap().id;
    graph.remove(look);
    assert!(graph.build().is_err(), "no material, no water");
    let loaded = WaterGraph::from_text(&graph.to_text()).expect("loads");
    assert!(loaded.nodes.iter().any(|n| n.op == "look"));
    assert!(loaded.build().is_ok(), "the migrated graph builds again");
}
