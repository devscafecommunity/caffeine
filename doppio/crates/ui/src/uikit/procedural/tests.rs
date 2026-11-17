use super::*;

const RES: usize = 64;

fn tileable_input() -> Pixels {
    par_fill(RES, |_, _, u, v| {
        gray(fbm(Basis::Perlin, u, v, 4, 11, 4, 0.5, 2.0))
    })
}

fn eval_default(op: &OpDef, res: usize) -> Vec<Pixels> {
    let params: Vec<f32> = op.params.iter().map(|p| p.default).collect();
    let colors: Vec<[f32; 4]> = op.colors.iter().map(|c| c.default).collect();
    let input = tileable_input();
    let input = if res == RES {
        input
    } else {
        par_fill(res, |_, _, u, v| {
            gray(fbm(Basis::Perlin, u, v, 4, 11, 4, 0.5, 2.0))
        })
    };
    let inputs: Vec<Option<&[[f32; 4]]>> =
        op.inputs.iter().map(|_| Some(input.as_slice())).collect();
    (op.eval)(&Args::new(res, &params, &colors, &inputs))
}

#[test]
fn the_library_is_large_and_ids_are_unique() {
    let ops = all();
    assert!(ops.len() >= 100, "expected 100+ ops, got {}", ops.len());
    let mut ids: Vec<&str> = ops.iter().map(|op| op.id).collect();
    ids.sort_unstable();
    let before = ids.len();
    ids.dedup();
    assert_eq!(before, ids.len(), "duplicate op ids");
    for op in ops {
        assert!(!op.outputs.is_empty(), "{} has no outputs", op.id);
        assert!(
            CATEGORIES.contains(&op.category),
            "{} has unknown category {}",
            op.id,
            op.category
        );
        for p in op.params {
            assert!(
                p.min <= p.default && p.default <= p.max,
                "{}.{} default out of range",
                op.id,
                p.key
            );
        }
        let mut keys: Vec<&str> = op.params.iter().map(|p| p.key).collect();
        keys.sort_unstable();
        keys.dedup();
        assert_eq!(
            keys.len(),
            op.params.len(),
            "{} has duplicate param keys",
            op.id
        );
    }
}

#[test]
fn every_op_produces_finite_unit_range_images_with_default_params() {
    for op in all() {
        let outputs = eval_default(op, RES);
        assert_eq!(outputs.len(), op.outputs.len(), "{}: output count", op.id);
        for (index, image) in outputs.iter().enumerate() {
            assert_eq!(image.len(), RES * RES, "{} output {index}", op.id);
            for p in image {
                for c in p {
                    assert!(c.is_finite(), "{} output {index} has NaN/Inf", op.id);
                    assert!(
                        (-0.001..=1.001).contains(c),
                        "{} output {index} out of range: {c}",
                        op.id
                    );
                }
            }
        }
    }
}

#[test]
fn ops_are_deterministic() {
    for op in all() {
        let a = eval_default(op, RES);
        let b = eval_default(op, RES);
        assert_eq!(a, b, "{} is not deterministic", op.id);
    }
}

fn seam_error(image: &[[f32; 4]], res: usize) -> (f32, f32) {
    let l = |x: usize, y: usize| luma(image[y * res + x]);
    let (mut seam, mut typical) = (0.0f32, 0.0f32);
    for i in 0..res {
        seam += (l(0, i) - l(res - 1, i)).abs() + (l(i, 0) - l(i, res - 1)).abs();
    }
    seam /= (2 * res) as f32;
    for y in 0..res {
        for x in 0..res - 1 {
            typical += (l(x, y) - l(x + 1, y)).abs() + (l(y, x) - l(y, x + 1)).abs();
        }
    }
    typical /= (2 * res * (res - 1)) as f32;
    (seam, typical)
}

#[test]
fn tileable_ops_have_no_visible_seam() {
    for op in all().iter().filter(|op| op.tileable) {
        let outputs = eval_default(op, RES);
        let (seam, typical) = seam_error(&outputs[0], RES);
        assert!(
            seam <= typical * 2.6 + 0.035,
            "{} seam {seam:.4} vs typical neighbour difference {typical:.4}",
            op.id
        );
    }
}

#[test]
fn most_generators_have_real_variation() {
    let mut flat = Vec::new();
    for op in all().iter().filter(|op| op.inputs.is_empty()) {
        // Thin patterns (grids) need a little resolution to register.
        let outputs = eval_default(op, 128);
        let values: Vec<f32> = outputs[1.min(outputs.len() - 1)]
            .iter()
            .map(|p| p[0])
            .collect();
        let mean = values.iter().sum::<f32>() / values.len() as f32;
        let var = values.iter().map(|v| (v - mean).powi(2)).sum::<f32>() / values.len() as f32;
        if var.sqrt() < 0.02 {
            flat.push(op.id);
        }
    }
    assert!(flat.is_empty(), "flat generators: {flat:?}");
}

#[test]
fn normal_height_round_trip_recovers_the_relief() {
    let height: Vec<f32> = par_scalar(RES, |_, _, u, v| {
        fbm(Basis::Perlin, u, v, 3, 5, 3, 0.5, 2.0)
    });
    let normal = height::height_to_normal(&height, RES, 2.0, 1, false);
    // A relief must tilt the normals.
    let tilt = normal.iter().map(|p| (p[0] - 0.5).abs()).sum::<f32>() / normal.len() as f32;
    assert!(tilt > 0.004, "normals look flat: {tilt}");
    let back = height::normal_to_height(&normal, RES, false);
    let (ma, mb) = (
        height.iter().sum::<f32>() / height.len() as f32,
        back.iter().sum::<f32>() / back.len() as f32,
    );
    let (mut num, mut da, mut db) = (0.0f32, 0.0f32, 0.0f32);
    for (a, b) in height.iter().zip(back.iter()) {
        num += (a - ma) * (b - mb);
        da += (a - ma).powi(2);
        db += (b - mb).powi(2);
    }
    let correlation = num / (da.sqrt() * db.sqrt()).max(1e-9);
    assert!(correlation > 0.85, "round trip correlation {correlation}");
}

#[test]
fn multiscale_ao_follows_the_height_cavity() {
    let flat = vec![0.5f32; RES * RES];
    let open = height::multiscale_ao(&flat, RES, 0.05, 1.0, 0.6);
    assert!(
        open.iter().all(|value| (*value - 1.0).abs() < 1e-4),
        "a flat plane should stay fully open"
    );

    let mut pit = vec![0.5f32; RES * RES];
    let (cx, cy) = (RES / 2, RES / 2);
    for y in cy - 4..=cy + 4 {
        for x in cx - 4..=cx + 4 {
            pit[y * RES + x] = 0.0;
        }
    }
    let ao = height::multiscale_ao(&pit, RES, 0.05, 1.0, 0.6);
    assert!(ao[cy * RES + cx] < 0.85, "pit centre should darken");
    assert!(ao[0] > 0.99, "corners far from the pit should stay open");
}

#[test]
fn blur_keeps_the_average_and_smooths() {
    let base = tileable_input();
    let blurred = gaussian_plane(&base.iter().map(|p| p[0]).collect::<Vec<_>>(), RES, 5.0);
    let mean = |v: &[f32]| v.iter().sum::<f32>() / v.len() as f32;
    let original: Vec<f32> = base.iter().map(|p| p[0]).collect();
    assert!((mean(&original) - mean(&blurred)).abs() < 0.01);
    let roughness = |v: &[f32]| v.windows(2).map(|w| (w[0] - w[1]).abs()).sum::<f32>();
    assert!(roughness(&blurred) < roughness(&original));
}

#[test]
fn herringbone_and_bricks_leave_gaps_and_tops() {
    for id in [
        "herringbone",
        "bricks_pro",
        "tiles",
        "hex_tiles",
        "planks",
        "basket_weave",
        "cobblestones",
    ] {
        let op = find(id).expect("op").def();
        let outputs = eval_default(op, 128);
        let value = &outputs[1];
        let gaps = value.iter().filter(|p| p[0] < 0.05).count();
        let tops = value.iter().filter(|p| p[0] > 0.4).count();
        assert!(gaps > 20, "{id}: no gaps ({gaps})");
        assert!(
            tops > value.len() / 4,
            "{id}: not enough raised area ({tops})"
        );
    }
}

#[test]
fn channel_split_and_merge_round_trip() {
    let split = find("channel_split").expect("split").def();
    let merge = find("channel_merge").expect("merge").def();
    let src: Pixels = par_fill(RES, |_, _, u, v| [u, v, 0.25, 0.75]);
    let params = split.params.iter().map(|p| p.default).collect::<Vec<_>>();
    let planes = (split.eval)(&Args::new(RES, &params, &[], &[Some(&src)]));
    assert_eq!(planes.len(), 4);
    let inputs: Vec<Option<&[[f32; 4]]>> = planes.iter().map(|p| Some(p.as_slice())).collect();
    let merged = (merge.eval)(&Args::new(RES, &[], &[], &inputs));
    for (a, b) in src.iter().zip(merged[0].iter()) {
        for c in 0..4 {
            assert!((a[c] - b[c]).abs() < 1e-5);
        }
    }
}

/// Writes every VFX op over a checkerboard when `DOPPIO_VFX_DUMP` names a directory (visual QA).
#[test]
fn vfx_ops_dump_for_review() {
    let Ok(dir) = std::env::var("DOPPIO_VFX_DUMP") else {
        return;
    };
    std::fs::create_dir_all(&dir).unwrap();
    for op in all().iter().filter(|op| op.category == "VFX & Shapes") {
        let res = 256;
        let outputs = eval_default(op, res);
        let mut img = image::RgbaImage::new(res as u32, res as u32);
        for (i, p) in outputs[0].iter().enumerate() {
            let (x, y) = (i % res, i / res);
            let checker = if ((x / 16) + (y / 16)) % 2 == 0 {
                0.30
            } else {
                0.42
            };
            let a = p[3];
            let c = |v: f32| ((v * a + checker * (1.0 - a)).clamp(0.0, 1.0) * 255.0) as u8;
            img.put_pixel(
                x as u32,
                y as u32,
                image::Rgba([c(p[0]), c(p[1]), c(p[2]), 255]),
            );
        }
        img.save(format!("{dir}/{}.png", op.id)).unwrap();
    }
}

/// Evaluates an op with its defaults on a given input image.
fn eval_on(id: &str, res: usize, inputs: &[&[[f32; 4]]]) -> Vec<Pixels> {
    let op = find(id).unwrap_or_else(|| panic!("op {id}"));
    let def = op.def();
    let params: Vec<f32> = def.params.iter().map(|p| p.default).collect();
    let colors: Vec<[f32; 4]> = def.colors.iter().map(|c| c.default).collect();
    let slots: Vec<Option<&[[f32; 4]]>> = (0..def.inputs.len())
        .map(|i| inputs.get(i).copied())
        .collect();
    (def.eval)(&Args::new(res, &params, &colors, &slots))
}

/// Writes contact sheets of the Variation ops over real inputs when `DOPPIO_VAR_DUMP` names a
/// directory (visual QA and the documentation images).
#[test]
fn variation_ops_dump_for_review() {
    let Ok(dir) = std::env::var("DOPPIO_VAR_DUMP") else {
        return;
    };
    std::fs::create_dir_all(&dir).unwrap();
    let res = 256;
    let save = |name: &str, image: &[[f32; 4]]| {
        let mut img = image::RgbaImage::new(res as u32, res as u32);
        for (i, p) in image.iter().enumerate() {
            let c = |v: f32| (v.clamp(0.0, 1.0) * 255.0) as u8;
            img.put_pixel(
                (i % res) as u32,
                (i / res) as u32,
                image::Rgba([c(p[0]), c(p[1]), c(p[2]), 255]),
            );
        }
        img.save(format!("{dir}/{name}.png")).unwrap();
    };
    let bricks = eval_on("bricks_pro", res, &[]);
    let brick = bricks[0].clone();
    save("0-input-bricks", &brick);
    save(
        "1-noise-variation",
        &eval_on("variation_noise", res, &[&brick])[0],
    );
    save(
        "2-cell-variation",
        &eval_on("variation_cells", res, &[&brick])[0],
    );
    save(
        "3-patch-blend",
        &eval_on(
            "variation_patches",
            res,
            &[&brick, &eval_on("dirt", res, &[])[0]],
        )[0],
    );
    save(
        "4-tile-shuffle",
        &eval_on("variation_shuffle", res, &[&brick])[0],
    );
    save("5-wfc-bricks", &eval_on("variation_wfc", res, &[&brick])[0]);
    save("6-wfc-builtin", &eval_on("variation_wfc", res, &[])[0]);
    save(
        "7-diamond-square",
        &eval_on("variation_diamond_square", res, &[])[0],
    );
    save(
        "6b-wfc-other-seed",
        &eval_with_seed("variation_wfc", res, &[], 9.0),
    );
    for id in ["truchet", "cobblestones", "hex_tiles", "scales"] {
        let sample = eval_on(id, res, &[]);
        save(&format!("8-input-{id}"), &sample[0]);
        save(
            &format!("9-wfc-{id}"),
            &eval_on("variation_wfc", res, &[&sample[0]])[0],
        );
    }
}

// ------------------------------------------------------------------ variation

fn eval_with_seed(id: &str, res: usize, inputs: &[&[[f32; 4]]], seed: f32) -> Pixels {
    let op = find(id).unwrap();
    let def = op.def();
    let mut params: Vec<f32> = def.params.iter().map(|p| p.default).collect();
    let index = def
        .params
        .iter()
        .position(|p| p.key == "seed")
        .expect("seed param");
    params[index] = seed;
    let colors: Vec<[f32; 4]> = def.colors.iter().map(|c| c.default).collect();
    let slots: Vec<Option<&[[f32; 4]]>> = (0..def.inputs.len())
        .map(|i| inputs.get(i).copied())
        .collect();
    (def.eval)(&Args::new(res, &params, &colors, &slots)).remove(0)
}

fn mean_abs_difference(a: &[[f32; 4]], b: &[[f32; 4]]) -> f32 {
    a.iter()
        .zip(b.iter())
        .map(|(p, q)| (0..3).map(|c| (p[c] - q[c]).abs()).sum::<f32>() / 3.0)
        .sum::<f32>()
        / a.len() as f32
}

#[test]
fn variation_ops_are_deterministic_and_the_seed_makes_a_new_variation() {
    let input = tileable_input();
    let inverted: Pixels = input
        .iter()
        .map(|p| [1.0 - p[0], 1.0 - p[1], 1.0 - p[2], 1.0])
        .collect();
    let wanted = [
        "variation_noise",
        "variation_cells",
        "variation_patches",
        "variation_shuffle",
        "variation_wfc",
        "variation_diamond_square",
    ];
    for id in wanted {
        let inputs: Vec<&[[f32; 4]]> = vec![&input, &inverted];
        let a = eval_with_seed(id, RES, &inputs, 3.0);
        let b = eval_with_seed(id, RES, &inputs, 3.0);
        let c = eval_with_seed(id, RES, &inputs, 4.0);
        assert_eq!(a, b, "{id} must be deterministic");
        assert!(
            mean_abs_difference(&a, &c) > 0.002,
            "{id}: a different seed must give a visibly different variation"
        );
    }
}

/// Edge-wrapping seam compared with the largest interior step: hard-edged cell ops cannot pass the
/// generic seam test (their cell borders are real features), but the image edge must never be worse
/// than the worst interior border.
#[test]
fn hard_edged_variation_ops_wrap_like_their_interior_borders() {
    let input = tileable_input();
    for id in ["variation_cells", "variation_shuffle", "variation_wfc"] {
        let res = RES;
        let image = eval_with_seed(id, res, &[&input], 5.0);
        let l = |x: usize, y: usize| luma(image[y * res + x]);
        let seam = (0..res)
            .map(|y| (l(0, y) - l(res - 1, y)).abs())
            .sum::<f32>()
            / res as f32;
        let worst_interior = (0..res - 1)
            .map(|x| (0..res).map(|y| (l(x, y) - l(x + 1, y)).abs()).sum::<f32>() / res as f32)
            .fold(0.0f32, f32::max);
        assert!(
            seam <= worst_interior + 0.02,
            "{id}: wrap seam {seam:.4} exceeds the worst interior border {worst_interior:.4}"
        );
    }
}

#[test]
fn diamond_square_is_periodic_fractal_and_roughness_changes_it() {
    let size = 64;
    let smooth = diamond_square_for_test(size, 1.1);
    let rough = diamond_square_for_test(size, 0.25);
    // Periodic: the field is continuous across the wrap, like any interior step.
    for field in [&smooth, &rough] {
        let step = |a: f32, b: f32| (a - b).abs();
        let wrap: f32 = (0..size)
            .map(|y| step(field[y * size], field[y * size + size - 1]))
            .sum::<f32>()
            / size as f32;
        let interior: f32 = (0..size)
            .flat_map(|y| (0..size - 1).map(move |x| (x, y)))
            .map(|(x, y)| step(field[y * size + x], field[y * size + x + 1]))
            .sum::<f32>()
            / (size * (size - 1)) as f32;
        assert!(
            wrap <= interior * 2.5 + 0.02,
            "wrap {wrap} vs interior {interior}"
        );
    }
    // Spans 0..1 and a lower Hurst exponent (rougher) has more high-frequency energy.
    let roughness = |f: &[f32]| {
        (0..size)
            .flat_map(|y| (0..size - 1).map(move |x| (x, y)))
            .map(|(x, y)| (f[y * size + x] - f[y * size + x + 1]).abs())
            .sum::<f32>()
            / (size * (size - 1)) as f32
    };
    assert!(roughness(&rough) > roughness(&smooth) * 1.3);
    assert!(smooth.iter().cloned().fold(f32::MAX, f32::min) < 0.01);
    assert!(smooth.iter().cloned().fold(f32::MIN, f32::max) > 0.99);
}

fn diamond_square_for_test(size: usize, roughness: f32) -> Vec<f32> {
    variation::diamond_square(size, roughness, 9)
}

#[test]
fn wave_function_collapse_only_builds_arrangements_the_sample_allows() {
    // A 6x6 periodic sample: vertical stripes of width two with a dotted second row.
    let s = 6;
    let sample: Vec<u8> = (0..s * s)
        .map(|i| {
            let (x, y) = (i % s, i / s);
            if y % 3 == 0 {
                1
            } else if (x / 2) % 2 == 0 {
                2
            } else {
                0
            }
        })
        .collect();
    let n = 3;
    let allowed: std::collections::HashSet<Vec<u8>> = (0..s * s)
        .map(|i| {
            let (x, y) = (i % s, i / s);
            (0..n * n)
                .map(|k| sample[((y + k / n) % s) * s + (x + k % n) % s])
                .collect()
        })
        .collect();
    // A multiple of the sample's period, so the periodic output can close on itself.
    let out = 12;
    let result = variation::wfc_bands(&sample, s, n, 0, out, 42).expect("the sample is consistent");
    assert_eq!(result.len(), out * out);
    // Every periodic 3x3 window of the result is a window of the sample.
    for y in 0..out {
        for x in 0..out {
            let window: Vec<u8> = (0..n * n)
                .map(|k| result[((y + k / n) % out) * out + (x + k % n) % out])
                .collect();
            assert!(
                allowed.contains(&window),
                "window at ({x},{y}) is not in the sample: {window:?}"
            );
        }
    }
    // And it is a new arrangement, not a copy of one tiling of the sample.
    let other = variation::wfc_bands(&sample, s, n, 0, out, 7).unwrap();
    assert_ne!(
        result, other,
        "different seeds explore different arrangements"
    );
}

#[test]
fn the_variation_class_is_registered_and_listed_in_the_palette() {
    let ops: Vec<&str> = all()
        .iter()
        .filter(|op| op.category == "Variation")
        .map(|op| op.id)
        .collect();
    assert_eq!(ops.len(), 6, "{ops:?}");
    assert!(CATEGORIES.contains(&"Variation"));
    assert!(crate::uikit::generators::palette()
        .iter()
        .any(|kind| kind.category() == "Variation"));
    assert!(crate::uikit::texture_graph::palette()
        .iter()
        .any(|kind| kind.category() == "Variation"));
}
