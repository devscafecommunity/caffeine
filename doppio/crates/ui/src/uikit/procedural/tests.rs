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
