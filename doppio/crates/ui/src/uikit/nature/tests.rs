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
