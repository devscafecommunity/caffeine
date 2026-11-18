//! Vegetation building blocks for the node graph: grass blades, fronds, bush leaves, petals, stems
//! and ellipsoids. Each returns a [`Part`]; graphs combine, move and scatter them.

use super::mesh::*;
use super::rock::icosphere;

fn new_part(name: &str) -> Part {
    Part::new(
        name,
        MaterialSpec::Solid {
            rgb: [0.3, 0.5, 0.2],
            roughness: 0.8,
        },
        [0.28, 0.5, 0.2],
    )
}

/// A curved strip card: leans over by `bend`, narrows towards the tip. Normals lean upwards so the
/// card is lit softly from both sides (the engine draws cards without culling).
#[allow(clippy::too_many_arguments)]
fn strip(
    part: &mut Part,
    base: V3,
    heading: V3,
    height: f32,
    width: f32,
    bend: f32,
    segments: u32,
    widest_at: f32,
) {
    let side = normalize([-heading[2], 0.0, heading[0]]);
    let rows = segments.max(2) as usize;
    let first = part.positions.len() as u32;
    for i in 0..=rows {
        let t = i as f32 / rows as f32;
        let along = mul(heading, bend * height * t * t);
        let lift = [0.0, height * t * (1.0 - 0.2 * bend * t), 0.0];
        let center = add(base, add(along, lift));
        // A blade is widest low down; a frond is widest in the middle.
        let profile = if widest_at > 0.0 {
            (std::f32::consts::PI * t.powf(0.8)).sin().max(0.05)
        } else {
            (1.0 - t).powf(0.8).max(0.04)
        };
        let half = width * 0.5 * profile;
        let tangent = normalize(add(
            mul(heading, 2.0 * bend * t),
            [0.0, 1.0 - 0.4 * bend * t, 0.0],
        ));
        let face = normalize(cross(side, tangent));
        let normal = normalize(add(mul(face, 0.45), [0.0, 0.8, 0.0]));
        let left = part.vertex(add(center, mul(side, -half)), normal, [0.0, t]);
        let right = part.vertex(add(center, mul(side, half)), normal, [1.0, t]);
        // Painted: darker (occluded) and rigid at the root, lighter and fluttering at the tip.
        let shade = 0.55 + 0.45 * t.powf(0.7);
        for v in [left, right] {
            part.set_color(v, [shade, shade, shade, t.powf(1.3)]);
        }
    }
    for i in 0..rows as u32 {
        let a = first + i * 2;
        part.quad(a, a + 1, a + 3, a + 2);
    }
}

/// A slim curved tube (stems).
fn stem(part: &mut Part, base: V3, top: V3, bulge: V3, r0: f32, r1: f32, sides: usize) {
    let rows = 5usize;
    let first = part.positions.len() as u32;
    let mut previous_u = frame_around(normalize(sub(top, base))).0;
    for i in 0..=rows {
        let t = i as f32 / rows as f32;
        let at = |t: f32| add(lerp(base, top, t), mul(bulge, 4.0 * t * (1.0 - t)));
        let center = at(t);
        let tangent = normalize(sub(at((t + 0.05).min(1.0)), at((t - 0.05).max(0.0))));
        previous_u = normalize(sub(previous_u, mul(tangent, dot(previous_u, tangent))));
        let v = cross(tangent, previous_u);
        let radius = r0 + (r1 - r0) * t;
        for k in 0..=sides {
            let a = k as f32 / sides as f32 * std::f32::consts::TAU;
            let normal = add(mul(previous_u, a.cos()), mul(v, a.sin()));
            part.vertex(
                add(center, mul(normal, radius)),
                normal,
                [k as f32 / sides as f32, t * 2.0],
            );
        }
    }
    let ring = (sides + 1) as u32;
    for i in 0..rows as u32 {
        for k in 0..sides as u32 {
            let a = first + i * ring + k;
            part.quad(a, a + 1, a + 1 + ring, a + ring);
        }
    }
}

#[allow(dead_code)]
fn blob(part: &mut Part, center: V3, radius: V3) {
    let (directions, faces) = icosphere(1);
    let base = part.positions.len() as u32;
    for d in &directions {
        part.vertex(
            add(
                center,
                [d[0] * radius[0], d[1] * radius[1], d[2] * radius[2]],
            ),
            *d,
            [0.5, 0.5],
        );
    }
    for f in &faces {
        part.triangle(base + f[0], base + f[1], base + f[2]);
    }
}

fn disc_point(rng: &mut Rng, radius: f32) -> V3 {
    let a = rng.range(0.0, std::f32::consts::TAU);
    let r = radius * rng.next().sqrt();
    [r * a.cos(), 0.0, r * a.sin()]
}

fn heading_away(rng: &mut Rng, at: V3) -> V3 {
    let jitter = rng.range(-0.7, 0.7);
    let base = if at[0] * at[0] + at[2] * at[2] < 1e-6 {
        let a = rng.range(0.0, std::f32::consts::TAU);
        [a.cos(), 0.0, a.sin()]
    } else {
        normalize([at[0], 0.0, at[2]])
    };
    let (c, s) = (jitter.cos(), jitter.sin());
    [base[0] * c - base[2] * s, 0.0, base[0] * s + base[2] * c]
}

/// A tuft of curved blades scattered over a disc.
pub fn blade_clump(
    count: u32,
    height: f32,
    width: f32,
    spread: f32,
    bend: f32,
    segments: u32,
    seed: u32,
) -> Part {
    let mut rng = Rng::new(seed);
    let mut part = new_part("blades");
    for _ in 0..count.clamp(1, 600) {
        let base = disc_point(&mut rng, spread);
        let heading = heading_away(&mut rng, base);
        strip(
            &mut part,
            base,
            heading,
            height * rng.range(0.55, 1.15),
            width * rng.range(0.8, 1.3),
            bend * rng.range(0.4, 1.2),
            segments,
            0.0,
        );
    }
    part
}

/// Arching fronds radiating from the centre (widest in the middle).
pub fn fronds(
    count: u32,
    height: f32,
    width: f32,
    spread: f32,
    bend: f32,
    segments: u32,
    seed: u32,
) -> Part {
    let mut rng = Rng::new(seed);
    let mut part = new_part("fronds");
    let count = count.clamp(1, 200);
    for i in 0..count {
        let a = i as f32 / count as f32 * std::f32::consts::TAU + rng.range(-0.2, 0.2);
        let heading = [a.cos(), 0.0, a.sin()];
        let base = mul(heading, spread * 0.08);
        strip(
            &mut part,
            base,
            heading,
            height * rng.range(0.7, 1.1),
            width * rng.range(0.9, 1.2),
            (bend * 1.4).max(0.6) * rng.range(0.8, 1.1),
            segments.max(4),
            1.0,
        );
    }
    part
}

/// Leaf cards filling a squashed half-sphere, facing outwards.
pub fn leaf_bush(count: u32, height: f32, width: f32, spread: f32, seed: u32) -> Part {
    let mut rng = Rng::new(seed);
    let mut part = new_part("leaves");
    for _ in 0..count.clamp(1, 1500) {
        let mut d = rng.direction();
        d[1] = d[1].abs();
        let radius = spread * rng.range(0.25, 1.0);
        let at = [
            d[0] * radius,
            d[1] * radius * (height / spread.max(0.01)),
            d[2] * radius,
        ];
        let size = width * rng.range(0.8, 1.3);
        let up = normalize(add(mul(d, 0.8), mul(rng.direction(), 0.5)));
        let side = normalize(cross(up, rng.direction()));
        let normal = normalize(add(d, [0.0, 0.3, 0.0]));
        let (w, l) = (size * 0.5, size);
        let tip = add(at, mul(up, l));
        let a = part.vertex(add(at, mul(side, -w * 0.2)), normal, [0.4, 0.02]);
        let b = part.vertex(add(at, mul(side, w * 0.2)), normal, [0.6, 0.02]);
        let c = part.vertex(add(tip, mul(side, w)), normal, [1.0, 0.98]);
        let e = part.vertex(add(tip, mul(side, -w)), normal, [0.0, 0.98]);
        part.quad(a, b, c, e);
    }
    part
}

/// A curved stem from the ground to `height`, leaning by `lean`.
pub fn stem_tube(height: f32, radius: f32, lean: f32, sides: u32, seed: u32) -> Part {
    let mut rng = Rng::new(seed);
    let mut part = new_part("stem");
    let offset = disc_point(&mut rng, lean * height * 0.3);
    stem(
        &mut part,
        [0.0; 3],
        add([0.0, height, 0.0], offset),
        mul(offset, -0.5),
        radius,
        radius * 0.66,
        sides.clamp(3, 12) as usize,
    );
    part
}

/// Petals fanned and cupped around a flower head `height` above the ground.
pub fn petals(count: u32, size: f32, cup: f32, height: f32, lean: f32, seed: u32) -> Part {
    let mut rng = Rng::new(seed);
    let mut part = new_part("petals");
    let offset = disc_point(&mut rng, lean * height * 0.3);
    let top = add([0.0, height, 0.0], offset);
    let facing = normalize(add([0.0, 1.0, 0.0], mul(offset, 0.6)));
    let (u, v) = frame_around(facing);
    let petal_length = size * 1.2;
    let count = count.clamp(3, 40);
    for k in 0..count {
        let a = k as f32 / count as f32 * std::f32::consts::TAU;
        let out = add(mul(u, a.cos()), mul(v, a.sin()));
        let dir = normalize(add(mul(out, 1.0 - cup * 0.45), mul(facing, cup * 0.9)));
        let side = normalize(cross(dir, facing));
        let normal = normalize(add(facing, mul(out, 0.4)));
        let (w, l) = (petal_length * 0.34, petal_length);
        let base = add(top, mul(out, size * 0.12));
        let tip = add(base, mul(dir, l));
        let a0 = part.vertex(add(base, mul(side, -w * 0.3)), normal, [0.4, 0.02]);
        let a1 = part.vertex(add(base, mul(side, w * 0.3)), normal, [0.6, 0.02]);
        let a2 = part.vertex(add(tip, mul(side, w)), normal, [1.0, 0.98]);
        let a3 = part.vertex(add(tip, mul(side, -w)), normal, [0.0, 0.98]);
        part.quad(a0, a1, a2, a3);
    }
    part
}

/// An ellipsoid (flower centres, moss clumps, pebbles, tree blobs).
pub fn ellipsoid(radius: V3, offset: V3, detail: u32, wobble: f32, seed: u32) -> Part {
    let mut part = new_part("blob");
    let (directions, faces) = icosphere(detail.clamp(1, 4));
    for d in &directions {
        let w =
            1.0 + wobble * (noise3(add(mul(*d, 1.9), [seed as f32, 0.0, 0.0]), seed) - 0.5) * 2.0;
        part.vertex(
            add(
                offset,
                [
                    d[0] * radius[0] * w,
                    d[1] * radius[1] * w,
                    d[2] * radius[2] * w,
                ],
            ),
            *d,
            [0.5 + d[0] * 0.5, 0.5 + d[2] * 0.5],
        );
    }
    for f in &faces {
        part.triangle(f[0], f[1], f[2]);
    }
    part
}
