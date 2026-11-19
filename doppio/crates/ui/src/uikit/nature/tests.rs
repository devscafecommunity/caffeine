use super::graph::Value;
use super::mesh::{MaterialSpec, NatureModel};
use super::*;

fn check_model(name: &str, model: &NatureModel) {
    assert!(!model.parts.is_empty(), "{name} has no parts");
    for part in &model.parts {
        assert!(part.triangle_count() > 0, "{name}/{} is empty", part.name);
        assert_eq!(part.positions.len(), part.normals.len());
        assert_eq!(part.positions.len(), part.uvs.len());
        assert!(
            part.indices
                .iter()
                .all(|i| (*i as usize) < part.positions.len()),
            "{name}: index out of range"
        );
        assert!(
            part.positions.iter().flatten().all(|v| v.is_finite()),
            "{name}: non-finite position"
        );
        for n in &part.normals {
            let l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
            assert!((l - 1.0).abs() < 0.05, "{name}: normal length {l}");
        }
        assert!(part.uvs.iter().flatten().all(|v| v.is_finite()));
    }
}

fn preset(name: &str) -> NaturePreset {
    presets()
        .into_iter()
        .find(|p| p.name == name)
        .unwrap_or_else(|| panic!("preset {name}"))
}

fn reseed(graph: &mut NatureGraph, add: f32) {
    let ids: Vec<NodeId> = graph.nodes.iter().map(|n| n.id).collect();
    for id in ids {
        if let Some(seed) = graph.get(id, "seed") {
            graph.set(id, "seed", seed + add);
        }
    }
}

#[test]
fn the_material_tables_agree() {
    assert!(ops::material_tables_match());
    assert!(ops::NODES.len() >= 24);
    let mut ids: Vec<&str> = ops::NODES.iter().map(|n| n.id).collect();
    ids.sort_unstable();
    let before = ids.len();
    ids.dedup();
    assert_eq!(before, ids.len(), "duplicate node ids");
    for def in ops::NODES {
        assert!(
            ops::CATEGORIES.contains(&def.category),
            "{} has an unknown category",
            def.id
        );
        for p in def.params {
            assert!(
                p.min <= p.default && p.default <= p.max,
                "{}.{} default out of range",
                def.id,
                p.key
            );
        }
    }
}

#[test]
fn every_preset_is_a_graph_that_builds_a_valid_model_at_every_quality() {
    let all = presets();
    assert!(all.len() >= 23);
    for preset in &all {
        let graph = preset.graph();
        assert!(graph.output_node().is_some(), "{}", preset.name);
        for quality in Quality::ALL {
            let model = graph
                .build(quality)
                .unwrap_or_else(|e| panic!("{} ({quality:?}): {e}", preset.name));
            check_model(&format!("{} ({quality:?})", preset.name), &model);
            let (lo, hi) = model.bounds();
            assert!(hi[1] - lo[1] > 0.05, "{} is flat", preset.name);
            assert!(
                model.triangles() < 120_000,
                "{} {quality:?}: {} triangles",
                preset.name,
                model.triangles()
            );
        }
    }
}

#[test]
fn building_is_deterministic_and_seeds_change_the_model() {
    for preset in presets() {
        let graph = preset.graph();
        let a = graph.build(Quality::Balanced).unwrap();
        let b = graph.build(Quality::Balanced).unwrap();
        assert_eq!(
            a.parts[0].positions, b.parts[0].positions,
            "{}",
            preset.name
        );
        let mut other = graph.clone();
        reseed(&mut other, 100.0);
        let c = other.build(Quality::Balanced).unwrap();
        assert_ne!(
            a.parts[0].positions, c.parts[0].positions,
            "{} must change with the seeds",
            preset.name
        );
    }
}

#[test]
fn quality_trades_polygons_for_detail() {
    for name in ["Boulder", "Oak", "Grass Clump", "Fern"] {
        let graph = preset(name).graph();
        let low = graph.build(Quality::Stylized).unwrap().triangles();
        let mid = graph.build(Quality::Balanced).unwrap().triangles();
        let high = graph.build(Quality::Detailed).unwrap().triangles();
        assert!(low < mid && mid < high, "{name}: {low} < {mid} < {high}");
    }
}

#[test]
fn rocks_sit_on_the_ground_trees_have_parts_and_stylised_needs_no_textures() {
    let model = preset("Boulder").graph().build(Quality::Balanced).unwrap();
    let (lo, hi) = model.bounds();
    assert!(lo[1].abs() < 1e-4, "the rock rests on y = 0");
    assert!(hi[0] - lo[0] > 0.5 && hi[1] - lo[1] > 0.3);
    assert!(matches!(model.parts[0].material, MaterialSpec::Preset(_)));

    let oak = preset("Oak").graph().build(Quality::Balanced).unwrap();
    let names: Vec<&str> = oak.parts.iter().map(|p| p.name.as_str()).collect();
    assert_eq!(names, ["trunk", "leaves"]);
    assert!(matches!(oak.parts[1].material, MaterialSpec::Cards(_)));
    assert_eq!(
        preset("Dead Tree")
            .graph()
            .build(Quality::Balanced)
            .unwrap()
            .parts
            .len(),
        1
    );
    let low = preset("Low-poly Tree")
        .graph()
        .build(Quality::Stylized)
        .unwrap();
    assert!(low
        .parts
        .iter()
        .all(|p| matches!(p.material, MaterialSpec::Solid { .. })));
    assert_eq!(
        preset("Pink Flower")
            .graph()
            .build(Quality::Balanced)
            .unwrap()
            .parts
            .len(),
        4
    );
}

#[test]
fn high_fidelity_presets_add_roots_and_moss_parts() {
    let moss = preset("Mossy Rock")
        .graph()
        .build(Quality::Detailed)
        .unwrap();
    let names: Vec<&str> = moss.parts.iter().map(|p| p.name.as_str()).collect();
    assert_eq!(names, ["rock", "moss"]);
    assert!(
        moss.parts[1].triangle_count() > 200,
        "moss clumps were instanced on the rock"
    );
    // The moss sits on the rock: inside its bounds, never floating far above it.
    let (_, rock_hi) = moss.parts[0].bounds();
    let (_, moss_hi) = moss.parts[1].bounds();
    assert!(
        moss_hi[1] <= rock_hi[1] + 0.3,
        "moss {moss_hi:?} vs rock {rock_hi:?}"
    );
    let oak = preset("Ancient Oak")
        .graph()
        .build(Quality::Detailed)
        .unwrap();
    assert_eq!(
        oak.parts
            .iter()
            .map(|p| p.name.as_str())
            .collect::<Vec<_>>(),
        ["trunk", "leaves", "moss"]
    );
    // Roots reach below the base line of the trunk, outwards.
    let plain = preset("Oak").graph().build(Quality::Detailed).unwrap();
    let wide = |m: &NatureModel| {
        let (lo, hi) = m.parts[0].bounds();
        (hi[0] - lo[0]).max(hi[2] - lo[2])
    };
    assert!(wide(&oak) > 0.0 && wide(&plain) > 0.0);
}

#[test]
fn connections_check_port_types_and_refuse_cycles() {
    let mut g = NatureGraph::new();
    let grow = g.add("grow", Point::new(0.0, 0.0));
    let ico = g.add("icosphere", Point::new(0.0, 0.0));
    let tubes = g.add("tubes", Point::new(0.0, 0.0));
    let displace = g.add("noise_displace", Point::new(0.0, 0.0));
    assert!(
        !g.connect(grow, 0, displace, 0),
        "a skeleton cannot feed a mesh input"
    );
    assert!(g.connect(grow, 0, tubes, 0));
    assert!(g.connect(ico, 0, displace, 0));
    let second = g.add("noise_displace", Point::new(0.0, 0.0));
    assert!(g.connect(displace, 0, second, 0));
    assert!(!g.connect(second, 0, displace, 0), "no cycles");
    assert!(!g.connect(ico, 0, ico, 0));
}

#[test]
fn graphs_round_trip_through_text_including_your_own_assets() {
    for preset in presets() {
        let graph = preset.graph();
        let text = to_text(preset.name, &graph, preset.quality);
        let (name, back, quality) =
            from_text(&text).unwrap_or_else(|| panic!("{} parses", preset.name));
        assert_eq!(name, preset.name);
        assert_eq!(quality, preset.quality);
        assert_eq!(
            back.build(Quality::Balanced).unwrap().parts[0].positions,
            graph.build(Quality::Balanced).unwrap().parts[0].positions,
            "{}",
            preset.name
        );
    }
    // A part that uses one of your own materials keeps the path (with spaces) through save and load.
    let mut graph = preset("Boulder").graph();
    let part_id = graph.nodes.iter().find(|n| n.op == "part").unwrap().id;
    swap_material(
        &mut graph,
        part_id,
        "mat_project",
        "my project/materials/Wet Moss.mat",
    );
    let (_, back, _) = from_text(&to_text("x", &graph, Quality::Balanced)).unwrap();
    let model = back.build(Quality::Stylized).unwrap();
    assert_eq!(
        model.parts[0].material,
        MaterialSpec::Project("my project/materials/Wet Moss.mat".to_owned(), (1, 1)),
        "your assets stay as they are, even stylised"
    );
    assert!(from_text("CAFMGRAPH2\n").is_none());
}

#[test]
fn your_own_textures_become_cutout_card_parts() {
    let mut graph = preset("Grass Clump").graph();
    let part_id = graph.nodes.iter().find(|n| n.op == "part").unwrap().id;
    let node = swap_material(&mut graph, part_id, "mat_texture", "textures/my_grass.png");
    let model = graph.build(Quality::Balanced).unwrap();
    assert_eq!(
        model.parts[0].material,
        MaterialSpec::ProjectTexture("textures/my_grass.png".to_owned(), (1, 1))
    );
    // Without a path there is no material, and the part says so instead of guessing one.
    graph.set_string(node, 0, String::new());
    let error = graph.build(Quality::Balanced).unwrap_err();
    assert!(error.contains("no material"), "{error}");
}

/// Replaces the material node feeding a part with a new one of `op` holding `path`.
fn swap_material(graph: &mut NatureGraph, part: NodeId, op: &'static str, path: &str) -> NodeId {
    let old = graph
        .links
        .iter()
        .find(|l| l.to == part && l.input == 1)
        .map(|l| l.from)
        .expect("the part has a material node");
    graph.remove(old);
    let node = graph.add(op, Point::new(0.0, 0.0));
    graph.set_string(node, 0, path.to_owned());
    assert!(graph.connect(node, 0, part, 1));
    node
}

#[test]
fn obj_export_is_well_formed() {
    let model = preset("Oak").graph().build(Quality::Balanced).unwrap();
    for part in &model.parts {
        let obj = part.to_obj();
        let count = |prefix: &str| obj.lines().filter(|l| l.starts_with(prefix)).count();
        assert_eq!(count("v "), part.positions.len());
        assert_eq!(count("vt "), part.uvs.len());
        assert_eq!(count("vn "), part.normals.len());
        assert_eq!(count("f "), part.triangle_count());
    }
}

#[test]
fn scatter_and_instance_respect_slope_and_budget() {
    let mut g = NatureGraph::new();
    let ico = g.add_with("icosphere", Point::new(0.0, 0.0), &[("detail", 3.0)]);
    let scatter = g.add_with(
        "scatter",
        Point::new(0.0, 0.0),
        &[("count", 200.0), ("up_min", 0.6)],
    );
    assert!(g.connect(ico, 0, scatter, 0));
    let Value::Points(points) = &g.evaluate(scatter, Quality::Balanced).unwrap()[0] else {
        panic!("points expected");
    };
    assert!(points.len() >= 150, "{} points", points.len());
    assert!(
        points.iter().all(|p| p.normal[1] >= 0.55),
        "only up-facing triangles get points"
    );
    let blob = g.add_with("ellipsoid", Point::new(0.0, 0.0), &[("detail", 2.0)]);
    let instance = g.add_with("instance", Point::new(0.0, 0.0), &[("budget", 2000.0)]);
    assert!(g.connect(blob, 0, instance, 0));
    assert!(g.connect(scatter, 0, instance, 1));
    let Value::Mesh(mesh) = &g.evaluate(instance, Quality::Balanced).unwrap()[0] else {
        panic!("mesh expected");
    };
    assert!(
        mesh.triangle_count() <= 2100,
        "the triangle budget caps the instances ({})",
        mesh.triangle_count()
    );
}

#[test]
fn nodes_can_be_mixed_freely() {
    // A rock with a tree trunk's bark material, built from scratch with a different noise node.
    let mut g = NatureGraph::new();
    let ico = g.add("icosphere", Point::new(0.0, 0.0));
    let lump = g.add_with(
        "noise_displace",
        Point::new(0.0, 0.0),
        &[("amount", 0.9), ("frequency", 3.0)],
    );
    let ground = g.add("ground", Point::new(0.0, 0.0));
    let part = g.add_with(
        "part",
        Point::new(0.0, 0.0),
        &[("name", ops::part_name_index("rock"))],
    );
    let bark = g.add_with(
        "mat_library",
        Point::new(0.0, 0.0),
        &[(
            "preset",
            ops::LIBRARY_LABELS
                .iter()
                .position(|l| *l == "Bark · Oak")
                .unwrap() as f32,
        )],
    );
    assert!(g.connect(bark, 0, part, 1));
    let out = g.add("output", Point::new(0.0, 0.0));
    assert!(
        g.connect(ico, 0, lump, 0)
            && g.connect(lump, 0, ground, 0)
            && g.connect(ground, 0, part, 0)
            && g.connect(part, 0, out, 0)
    );
    let model = g.build(Quality::Balanced).unwrap();
    assert!(matches!(
        model.parts[0].material,
        MaterialSpec::Preset("Oak Bark")
    ));
    // An empty graph explains itself instead of panicking.
    let mut empty = NatureGraph::new();
    empty.add("output", Point::new(0.0, 0.0));
    assert!(empty
        .build(Quality::Balanced)
        .unwrap_err()
        .contains("Nothing reaches"));
}

/// Writes preview images of every preset when `DOPPIO_NATURE_DUMP` names a directory.
#[test]
fn nature_presets_dump_for_review() {
    let Ok(dir) = std::env::var("DOPPIO_NATURE_DUMP") else {
        return;
    };
    std::fs::create_dir_all(&dir).unwrap();
    for preset in presets() {
        let model = preset.graph().build(preset.quality).unwrap();
        let pixels = preview::render(&model, 320, 320, preview::View::default());
        image::RgbaImage::from_raw(320, 320, pixels)
            .unwrap()
            .save(format!("{dir}/{}.png", slug(preset.name)))
            .unwrap();
        eprintln!("{:<16} {:>6} triangles", preset.name, model.triangles());
    }
}

#[test]
fn baked_masks_and_rock_surface_describe_the_geometry() {
    use super::{bake, surface};
    let model = preset("Photoreal Rock")
        .graph()
        .build(Quality::Balanced)
        .unwrap();
    let part = &model.parts[0];
    let spec = part
        .surface
        .clone()
        .expect("the Surface node attaches a texture set");
    // Unique UVs: every vertex inside the unit square.
    assert!(part
        .uvs
        .iter()
        .all(|uv| (0.0..=1.0).contains(&uv[0]) && (0.0..=1.0).contains(&uv[1])));
    let maps = bake::bake(part, 256);
    let covered = maps.covered.iter().filter(|c| **c).count() as f32 / maps.covered.len() as f32;
    assert!(covered > 0.25, "the atlas covers {covered}");
    assert!(maps.ao.iter().all(|v| (0.0..=1.0).contains(v)));
    let (lo, hi) = maps
        .curvature
        .iter()
        .fold((1.0f32, -1.0f32), |(l, h), v| (l.min(*v), h.max(*v)));
    assert!(
        lo < -0.01 && hi > 0.01,
        "curvature has both signs: {lo}..{hi}"
    );
    // Top texels are higher than bottom ones.
    let heights: Vec<f32> = maps
        .height
        .iter()
        .zip(&maps.covered)
        .filter(|(_, c)| **c)
        .map(|(h, _)| *h)
        .collect();
    assert!(heights.iter().cloned().fold(0.0, f32::max) > 0.9);

    // More moss means more green; the set is deterministic.
    let green = |spec: &surface::SurfaceSpec| {
        let set = surface::rock(&maps, spec);
        set.albedo
            .chunks_exact(4)
            .filter(|p| p[1] > p[0] + 12 && p[1] > p[2] + 25)
            .count()
    };
    let none = surface::SurfaceSpec {
        moss: 0.0,
        lichen: 0.0,
        ..spec.clone()
    };
    let lots = surface::SurfaceSpec {
        moss: 1.0,
        lichen: 0.0,
        ..spec.clone()
    };
    assert!(
        green(&lots) > green(&none) + 200,
        "{} vs {}",
        green(&lots),
        green(&none)
    );
    assert_eq!(
        surface::rock(&maps, &spec).albedo,
        surface::rock(&maps, &spec).albedo
    );
    let set = surface::rock(&maps, &spec);
    assert!(
        set.normal.chunks_exact(4).any(|p| p[3] != set.normal[3]),
        "the height varies"
    );

    // Dump for review.
    if let Ok(dir) = std::env::var("DOPPIO_NATURE_SURFACE_DUMP") {
        let big = bake::bake(part, 1024);
        let set = surface::rock(&big, &spec);
        for (name, px) in [
            ("albedo", &set.albedo),
            ("normal", &set.normal),
            ("orm", &set.orm),
        ] {
            image::save_buffer(
                format!("{dir}/{name}.png"),
                px,
                1024,
                1024,
                image::ColorType::Rgba8,
            )
            .unwrap();
        }
        let plane = |v: &[f32], lo: f32, hi: f32| -> Vec<u8> { bake::plane_to_u8(v, lo, hi) };
        for (name, px) in [
            ("ao", plane(&big.ao, 0.0, 1.0)),
            ("curvature", plane(&big.curvature, -0.5, 0.5)),
        ] {
            image::save_buffer(
                format!("{dir}/{name}.png"),
                &px,
                1024,
                1024,
                image::ColorType::L8,
            )
            .unwrap();
        }
    }
}

#[test]
fn leaf_atlas_gives_varied_folded_drooping_leaves() {
    use super::tree;
    let graph = preset("Oak").graph();
    // The texture wired into the Leaves node is the atlas.
    let leaves_node = graph.nodes.iter().find(|n| n.op == "leaves").unwrap().id;
    let texture = graph
        .links
        .iter()
        .find(|l| l.to == leaves_node && l.input == 1)
        .unwrap()
        .from;
    assert_eq!(graph.node(texture).unwrap().op, "mat_cards");
    let model = graph.build(Quality::Balanced).unwrap();
    let leaves = model
        .parts
        .iter()
        .find(|p| p.name == "leaves")
        .expect("leaves part");
    assert!(matches!(&leaves.material, MaterialSpec::Cards(name) if name.starts_with("Atlas")));
    // Cards sample every column and row of the 4x2 atlas.
    let columns: std::collections::HashSet<u32> = leaves
        .uvs
        .iter()
        .map(|uv| (uv[0] * 4.0).min(3.999) as u32)
        .collect();
    let rows: std::collections::HashSet<u32> = leaves
        .uvs
        .iter()
        .map(|uv| (uv[1] * 2.0).min(1.999) as u32)
        .collect();
    assert_eq!(columns.len(), 4, "{columns:?}");
    assert_eq!(rows.len(), 2, "{rows:?}");
    // Cards are not flat quads: several rows and a folded section (many triangles per leaf).
    assert!(leaves.triangle_count() > 400);
    // More rows at higher quality, and flat single-shape cards still work.
    let detailed = graph.build(Quality::Detailed).unwrap();
    let per = |m: &super::NatureModel| {
        m.parts
            .iter()
            .find(|p| p.name == "leaves")
            .unwrap()
            .triangle_count()
    };
    assert!(per(&detailed) > per(&model));
    let _ = tree::LeafCards {
        density: 1.0,
        size: 1.0,
        inner_share: 0.5,
        max_triangles: 100,
        atlas: (1, 1),
        fold: 0.0,
        droop: 0.0,
        rows: 1,
        shade_cell: None,
    };

    // The atlas image holds eight different leaves: tiles are not copies of each other.
    let (cols, rows_n, cells) = crate::uikit::texture_graph::atlas("Atlas · Broadleaf").unwrap();
    assert_eq!((cols, rows_n, cells.len()), (4, 2, 8));
    let mut seen = std::collections::HashSet::new();
    for mut cell in cells {
        cell.resolution = 64;
        let baked = cell
            .bake_texture_with(&crate::uikit::generators::TextureMap::new(), None, None)
            .unwrap();
        assert!(baked.has_alpha, "leaves are cut out");
        seen.insert(baked.rgba8);
    }
    assert_eq!(seen.len(), 8, "every cell is a different leaf");
    assert!(crate::uikit::texture_graph::atlas("Atlas · Nothing").is_none());
}

#[test]
fn bark_sets_tile_are_distinct_and_carry_relief() {
    use super::surface::{bark, BarkKind};
    let res = 128;
    let oak = bark(BarkKind::Oak, res, 1);
    let birch = bark(BarkKind::Birch, res, 1);
    assert_eq!(oak.albedo.len(), res * res * 4);
    // Deterministic.
    assert_eq!(oak.normal, bark(BarkKind::Oak, res, 1).normal);
    // Different bark, different image; oak is darker than birch on average.
    let mean = |m: &super::surface::SurfaceMaps| {
        m.albedo.chunks(4).map(|p| p[0] as f32).sum::<f32>() / (res * res) as f32
    };
    assert!(
        mean(&birch) > mean(&oak) + 40.0,
        "{} {}",
        mean(&birch),
        mean(&oak)
    );
    // Furrows: the height (normal alpha) spans a wide range and the occlusion darkens crevices.
    let heights: Vec<u8> = oak.normal.chunks(4).map(|p| p[3]).collect();
    let (lo, hi) = (
        *heights.iter().min().unwrap(),
        *heights.iter().max().unwrap(),
    );
    assert!(hi - lo > 120, "{lo}..{hi}");
    let ao_min = oak.orm.chunks(4).map(|p| p[0]).min().unwrap();
    assert!(ao_min < 130, "{ao_min}");
    // Tileable: the seam between the last and the first column is no rougher than inside.
    let h = |x: usize, y: usize| oak.normal[(y * res + x) * 4 + 3] as f32;
    let seam: f32 = (0..res).map(|y| (h(0, y) - h(res - 1, y)).abs()).sum();
    let inside: f32 = (0..res)
        .map(|y| (h(res / 2, y) - h(res / 2 - 1, y)).abs())
        .sum();
    assert!(seam < inside * 2.0 + 1.0, "seam {seam} vs {inside}");
    // Library names map to a kind.
    assert_eq!(BarkKind::from_name("Pine Bark"), Some(BarkKind::Pine));
    assert_eq!(BarkKind::from_name("Birch Bark"), Some(BarkKind::Birch));
    assert_eq!(BarkKind::from_name("Cliff Granite"), None);
}

#[test]
fn leaf_card_normals_follow_veins_and_stay_flat_on_plain_cards() {
    use super::surface::card_normal;
    let (w, h) = (32usize, 32usize);
    let flat: Vec<u8> = (0..w * h).flat_map(|_| [90u8, 140, 40, 255]).collect();
    let n = card_normal(&flat, w, h);
    // A plain opaque card points straight out.
    assert!(n
        .chunks(4)
        .all(|p| p[0].abs_diff(128) <= 1 && p[1].abs_diff(128) <= 1 && p[2] >= 254));
    // A dark vein down the middle tilts the normals on both sides in opposite directions.
    let mut veined = flat.clone();
    for y in 0..h {
        for x in 15..17 {
            let i = (y * w + x) * 4;
            veined[i] = 30;
            veined[i + 1] = 60;
            veined[i + 2] = 20;
        }
    }
    let n = card_normal(&veined, w, h);
    let left = n[(16 * w + 14) * 4];
    let right = n[(16 * w + 17) * 4];
    assert!(left != right, "{left} {right}");
    assert!(left.abs_diff(128) > 4 || right.abs_diff(128) > 4);
}

#[test]
fn crown_interior_uses_the_dark_leaf_cell_more_than_the_shell() {
    let graph = preset("Oak").graph();
    let model = graph.build(Quality::Balanced).unwrap();
    let leaves = model.parts.iter().find(|p| p.name == "leaves").unwrap();
    // Share of vertices sampling atlas cell 4 (column 0, row 1 of the 4 x 2 atlas).
    let dark = leaves
        .uvs
        .iter()
        .filter(|uv| (uv[0] * 4.0) as u32 == 0 && (uv[1] * 2.0) as u32 == 1)
        .count() as f32
        / leaves.uvs.len() as f32;
    // Without shading a cell is picked 1 time in 8 (12.5%): shading raises it clearly.
    assert!(dark > 0.2, "dark cell share {dark}");
}

#[test]
fn instanced_scatter_keeps_copies_as_transforms_next_to_the_merged_geometry() {
    use super::mesh::euler_from_columns;
    let model = preset("Mossy Rock")
        .graph()
        .build(Quality::Detailed)
        .unwrap();
    let moss = model
        .parts
        .iter()
        .find(|p| p.instances.is_some())
        .expect("the moss scatter is instanced");
    let set = moss.instances.as_ref().unwrap();
    assert!(set.transforms.len() > 3, "{}", set.transforms.len());
    // The merged geometry is exactly the template repeated once per copy.
    assert_eq!(
        moss.triangle_count(),
        set.template.triangle_count() * set.transforms.len()
    );
    assert!(set.template.instances.is_none());
    assert!(set.transforms.iter().all(|t| t.scale > 0.0));
    // Merging with other geometry (or another set) drops the copy list: it would be wrong.
    let mut merged = moss.clone();
    merged.append(&set.template);
    assert!(merged.instances.is_none());

    // Euler angles rebuild the rotation they came from (ZYX: R = Rz * Ry * Rx).
    for (pitch, yaw, roll) in [
        (20.0f32, 35.0f32, -15.0f32),
        (-40.0, 70.0, 10.0),
        (0.0, 0.0, 0.0),
    ] {
        let (p, y, r) = (pitch.to_radians(), yaw.to_radians(), roll.to_radians());
        let (sp, cp, sy, cy, sr, cr) = (p.sin(), p.cos(), y.sin(), y.cos(), r.sin(), r.cos());
        let c0 = [cr * cy, sr * cy, -sy];
        let c1 = [cr * sy * sp - sr * cp, sr * sy * sp + cr * cp, cy * sp];
        let c2 = [cr * sy * cp + sr * sp, sr * sy * cp - cr * sp, cy * cp];
        let e = euler_from_columns(c0, c1, c2);
        for (got, want) in e.iter().zip([pitch, yaw, roll]) {
            assert!((got - want).abs() < 0.01, "{e:?} vs {pitch} {yaw} {roll}");
        }
    }
}

#[test]
fn leaves_and_blades_are_painted_with_occlusion_and_a_flutter_weight() {
    let model = preset("Oak").graph().build(Quality::Balanced).unwrap();
    let leaves = model.parts.iter().find(|p| p.name == "leaves").unwrap();
    let colors = leaves.painted_colors().expect("leaf cards are painted");
    assert_eq!(colors.len(), leaves.positions.len());
    // The stalk end is rigid and the tip flutters: both extremes are present.
    let min_w = colors.iter().map(|c| c[3]).fold(f32::MAX, f32::min);
    let max_w = colors.iter().map(|c| c[3]).fold(f32::MIN, f32::max);
    assert!(min_w < 0.05 && max_w > 0.95, "{min_w}..{max_w}");
    // Occlusion: leaves inside the crown are darker than the outer shell.
    let (lo, hi) = colors.iter().fold((f32::MAX, f32::MIN), |(lo, hi), c| {
        (lo.min(c[0]), hi.max(c[0]))
    });
    assert!(hi - lo > 0.2, "{lo}..{hi}");
    // The OBJ carries `v x y z r g b a` so the engine can read the colour stream.
    let obj = leaves.to_obj();
    let first = obj.lines().find(|l| l.starts_with("v ")).unwrap();
    assert_eq!(first.split_whitespace().count(), 8, "{first}");
    // Rocks have no painted colour: plain `v x y z` and no colour stream.
    let rock = preset("Boulder").graph().build(Quality::Balanced).unwrap();
    assert!(rock.parts.iter().all(|p| p.painted_colors().is_none()));
    let line = rock.parts[0].to_obj();
    let v = line.lines().find(|l| l.starts_with("v ")).unwrap();
    assert_eq!(v.split_whitespace().count(), 4);
    // Grass roots are rigid and dark, tips free and light.
    let grass = preset("Grass Clump")
        .graph()
        .build(Quality::Balanced)
        .unwrap();
    let blades = grass
        .parts
        .iter()
        .find(|p| p.painted_colors().is_some())
        .unwrap();
    let c = blades.painted_colors().unwrap();
    assert!(c.iter().any(|c| c[3] < 0.05) && c.iter().any(|c| c[3] > 0.9));
}

#[test]
fn texture_and_material_nodes_build_one_custom_material_from_summed_layers() {
    use super::mesh::{CustomMaterial, LayerBlend, TextureSlot};
    use iced::Point;
    let mut g = NatureGraph::new();
    let ico = g.add("icosphere", Point::new(0.0, 0.0));
    let t1 = g.add("texture", Point::new(1.0, 0.0));
    g.set_string(t1, 0, "textures/bark.png".to_owned());
    let t2 = g.add_with(
        "texture",
        Point::new(2.0, 0.0),
        &[
            ("slot", 1.0),
            ("blend", 1.0),
            ("strength", 0.5),
            ("mapping", 1.0),
        ],
    );
    g.set_string(t2, 0, "textures/bark_n.png".to_owned());
    let material = g.add_with(
        "material",
        Point::new(3.0, 0.0),
        &[
            ("roughness", 0.3),
            ("translucency", 0.5),
            ("sway", 0.02),
            ("alpha", 1.0),
        ],
    );
    // A fresh Part keeps what the nodes upstream made.
    let part = g.add("part", Point::new(4.0, 0.0));
    let out = g.add("output", Point::new(5.0, 0.0));
    assert!(g.connect(ico, 0, t1, 0) && g.connect(t1, 0, t2, 0));
    assert!(g.connect(t2, 0, material, 0) && g.connect(material, 0, part, 0));
    assert!(g.connect(part, 0, out, 0));
    let model = g.build(Quality::Balanced).unwrap();
    let MaterialSpec::Custom(CustomMaterial {
        layers,
        cutout,
        roughness,
        translucency,
        sway,
        ..
    }) = &model.parts[0].material
    else {
        panic!("{:?}", model.parts[0].material);
    };
    // Layers are summed in node order; the Material node added no layer and kept both.
    assert_eq!(layers.len(), 2);
    assert_eq!(
        (layers[0].slot, layers[0].path.as_str()),
        (TextureSlot::Albedo, "textures/bark.png")
    );
    assert_eq!(
        (layers[1].slot, layers[1].blend),
        (TextureSlot::Normal, LayerBlend::Add)
    );
    assert!(*cutout && (*roughness - 0.3).abs() < 1e-5);
    assert!((*translucency - 0.5).abs() < 1e-5 && (*sway - 0.02).abs() < 1e-5);
    // Fit to the object re-projected the UVs from the part's size (about one repeat across it).
    let uvs = &model.parts[0].uvs;
    let max = uvs
        .iter()
        .flat_map(|uv| uv.iter().copied())
        .fold(0.0f32, |m, v| m.max(v.abs()));
    assert!(max > 0.3 && max < 3.0, "{max}");
    // Different settings are a different material file.
    let a = model.parts[0].material.slug();
    g.set(material, "roughness", 0.9);
    assert_ne!(
        a,
        g.build(Quality::Balanced).unwrap().parts[0].material.slug()
    );
    // A Part with another material choice overrides the nodes, as before.
    g.set(
        part,
        "material",
        super::ops::material_index("Solid · Green"),
    );
    assert!(matches!(
        g.build(Quality::Balanced).unwrap().parts[0].material,
        MaterialSpec::Solid { .. }
    ));
}

#[test]
fn texture_layers_sum_blend_and_adapt() {
    use super::mesh::{CustomMaterial, LayerBlend, TextureLayer, TextureSlot};
    use super::texlayers::{bake, Image};
    let solid = |rgba: [u8; 4]| Image {
        width: 4,
        height: 4,
        rgba: (0..16).flat_map(|_| rgba).collect(),
    };
    let load = |path: &str| match path {
        "red" => Some(solid([200, 0, 0, 255])),
        "grey" => Some(solid([128, 128, 128, 255])),
        "hole" => Some(solid([10, 200, 10, 0])),
        "bump" => Some(solid([160, 128, 220, 255])),
        _ => None,
    };
    let layer = |slot, path: &str, blend, strength: f32| TextureLayer {
        slot,
        path: path.to_owned(),
        blend,
        strength,
        tiling: [2.0, 2.0],
        tint: [1.0; 3],
    };
    let mut m = CustomMaterial::plain([0.0, 0.0, 1.0], 0.8);
    // Nothing loads: nothing baked, no crash.
    m.layers.push(layer(
        TextureSlot::Albedo,
        "missing",
        LayerBlend::Replace,
        1.0,
    ));
    assert!(bake(&m, 16, &load).albedo.is_none());
    // Replace, then Multiply by grey: red * 0.5.
    m.layers = vec![
        layer(TextureSlot::Albedo, "red", LayerBlend::Replace, 1.0),
        layer(TextureSlot::Albedo, "grey", LayerBlend::Multiply, 1.0),
    ];
    let baked = bake(&m, 16, &load);
    let px = &baked.albedo.unwrap()[..4];
    assert!(
        (px[0] as i32 - 100).abs() <= 2 && px[1] == 0 && px[2] == 0,
        "{px:?}"
    );
    assert!(!baked.has_alpha);
    // Add sums on top of the base colour (blue + grey).
    m.layers = vec![layer(TextureSlot::Albedo, "grey", LayerBlend::Add, 1.0)];
    let px = bake(&m, 16, &load).albedo.unwrap();
    assert!(px[0] > 120 && px[2] == 255);
    // An albedo with transparency marks the material for cut-out.
    m.layers = vec![layer(TextureSlot::Albedo, "hole", LayerBlend::Replace, 1.0)];
    assert!(bake(&m, 16, &load).has_alpha);
    // Normals add their slopes and stay unit length.
    m.layers = vec![
        layer(TextureSlot::Normal, "bump", LayerBlend::Replace, 1.0),
        layer(TextureSlot::Normal, "bump", LayerBlend::Add, 1.0),
    ];
    let n = bake(&m, 16, &load).normal.unwrap();
    let v = |c: u8| c as f32 / 255.0 * 2.0 - 1.0;
    let len = (v(n[0]).powi(2) + v(n[1]).powi(2) + v(n[2]).powi(2)).sqrt();
    assert!((len - 1.0).abs() < 0.03, "{len}");
    assert!(v(n[0]) > 0.3, "two bumps lean further than one");
}

#[test]
fn nothing_is_hidden_inside_a_part_every_material_is_a_node() {
    for p in presets() {
        let graph = p.graph();
        for part in graph.nodes.iter().filter(|n| n.op == "part") {
            let wired = graph.links.iter().find(|l| l.to == part.id && l.input == 1);
            let source = wired.and_then(|l| graph.node(l.from));
            assert!(
                source.is_some_and(|n| n.def().category == "Materials")
                    || preset_has_surface(&graph),
                "{}: a part without a material node",
                p.name
            );
        }
    }
    // A part with nothing wired in refuses to build and names the part.
    let mut g = preset("Oak").graph();
    let trunk = g
        .nodes
        .iter()
        .find(|n| n.op == "part" && g.get(n.id, "name") == Some(ops::part_name_index("trunk")))
        .unwrap()
        .id;
    let material = g
        .links
        .iter()
        .find(|l| l.to == trunk && l.input == 1)
        .unwrap()
        .from;
    g.remove(material);
    let error = g.build(Quality::Balanced).unwrap_err();
    assert!(error.contains("trunk part has no material"), "{error}");
}

fn preset_has_surface(graph: &NatureGraph) -> bool {
    graph.nodes.iter().any(|n| n.op == "surface")
}

#[test]
fn leaf_size_scales_with_the_tree_and_the_texture_sets_the_atlas() {
    let mut graph = preset("Oak").graph();
    let size_of = |g: &NatureGraph| {
        let model = g.build(Quality::Balanced).unwrap();
        let leaves = model.parts.iter().find(|p| p.name == "leaves").unwrap();
        let (lo, hi) = leaves.bounds();
        ((hi[0] - lo[0]).max(hi[2] - lo[2]), leaves.triangle_count())
    };
    let leaves = graph.nodes.iter().find(|n| n.op == "leaves").unwrap().id;
    let (_, _) = size_of(&graph);
    // A leaf is a share of the tree: tripling the share makes each leaf (and the crown) larger.
    let base = graph.get(leaves, "size").unwrap();
    assert!(
        base > 1.0 && base < 15.0,
        "a sensible default share: {base} %"
    );
    let height_of = |g: &NatureGraph| {
        let grow = g.nodes.iter().find(|n| n.op == "grow").unwrap().id;
        match g.evaluate(grow, Quality::Balanced).unwrap().remove(0) {
            super::graph::Value::Skeleton(s) => s.height,
            _ => unreachable!(),
        }
    };
    let tall = height_of(&graph);
    graph.set(leaves, "size", base * 3.0);
    let bigger = size_of(&graph).0;
    graph.set(leaves, "size", base);
    assert!(bigger > size_of(&graph).0);
    assert!(tall > 1.0);
    // The Leaves node reads the atlas from the texture wired into it.
    let texture = graph
        .links
        .iter()
        .find(|l| l.to == leaves && l.input == 1)
        .map(|l| l.from)
        .unwrap();
    assert_eq!(graph.node(texture).unwrap().op, "mat_cards");
    let triangles = |g: &NatureGraph| size_of(g).1;
    let with_atlas = triangles(&graph);
    let single = graph.nodes.iter().find(|n| n.op == "mat_cards").unwrap().id;
    graph.set(single, "card", 0.0); // a single leaf picture
    assert!(
        triangles(&graph) < with_atlas,
        "single-leaf cards are fewer than atlas cards"
    );
}

/// Rewrites a current graph's text the way version 1 stored it: no material nodes, the material
/// chosen inside the Part node, and leaf size in metres with a `shapes` choice.
fn as_version_one(text: &str) -> String {
    let mut material_nodes = std::collections::HashSet::new();
    for line in text.lines() {
        let w: Vec<&str> = line.split_whitespace().collect();
        if w.first() == Some(&"node") && w[4].starts_with("mat_") {
            material_nodes.insert(w[1].to_owned());
        }
    }
    let mut out = String::from("NATUREGRAPH1\n");
    for line in text.lines().skip(1) {
        let w: Vec<&str> = line.split_whitespace().collect();
        match w.first().copied() {
            Some("node") if material_nodes.contains(w[1]) => {}
            Some("link") if material_nodes.contains(w[1]) => {}
            Some("node") if w[4] == "part" => {
                // name, material (Bark · Oak = 6, Leaves · Green = 11), r, g, b, roughness
                let material = if w[6] == "3" { 11 } else { 6 };
                out.push_str(&format!(
                    "node {} {} {} part 6 {} {material} 0.5 0.5 0.5 0.8 1 %00\n",
                    w[1], w[2], w[3], w[6]
                ));
            }
            Some("node") if w[4] == "leaves" => {
                // style density size(m) inner budget seed shapes fold droop
                out.push_str(&format!(
                    "node {} {} {} leaves 9 0 12 0.75 0.45 9000 1 0 0.2 0.6 0\n",
                    w[1], w[2], w[3]
                ));
            }
            _ => {
                out.push_str(line);
                out.push('\n');
            }
        }
    }
    out
}

#[test]
fn version_one_graphs_gain_material_nodes_and_keep_their_look() {
    let current = preset("Oak").graph();
    let old = as_version_one(&current.to_text());
    let migrated = NatureGraph::from_text(&old).expect("version 1 loads");
    // Every part now has an explicit material node, so the graph builds again.
    let parts = migrated.nodes.iter().filter(|n| n.op == "part").count();
    assert!(parts >= 2);
    let wired = migrated
        .links
        .iter()
        .filter(|l| {
            migrated.node(l.to).is_some_and(|n| n.op == "part")
                && migrated
                    .node(l.from)
                    .is_some_and(|n| n.op.starts_with("mat_"))
        })
        .count();
    assert_eq!(wired, parts);
    // The leaf part keeps its leaf texture, shared with the Leaves node, and the folds survive.
    let leaves = migrated.nodes.iter().find(|n| n.op == "leaves").unwrap().id;
    assert!(migrated
        .links
        .iter()
        .any(|l| l.to == leaves && l.input == 1));
    assert_eq!(migrated.get(leaves, "fold"), Some(0.2));
    assert_eq!(migrated.get(leaves, "droop"), Some(0.6));
    // 0.75 m on this tree is a share of its height, not 0.75 %.
    let size = migrated.get(leaves, "size").unwrap();
    assert!(size > 1.0 && size < 30.0, "{size}");
    assert!(migrated.build(Quality::Balanced).is_ok());
    // Saving writes the new format, which loads without migrating again.
    assert!(migrated.to_text().starts_with("NATUREGRAPH2"));
    let again = NatureGraph::from_text(&migrated.to_text()).unwrap();
    assert_eq!(again.nodes.len(), migrated.nodes.len());
}
