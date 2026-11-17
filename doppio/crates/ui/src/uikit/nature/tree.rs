//! Tree growth as reusable pieces for the node graph: a **skeleton** (branches as polylines with
//! radii), tubes around it, leaf cards and blob crowns.
//!
//! `grow` makes the skeleton; `tubes`, `leaf_cards` and `leaf_blobs` turn it into meshes. They are
//! separate so a graph can mix them: a high-fidelity tree adds roots and moss, a low-poly one uses
//! blobs, and nothing about the branching has to change.

use super::mesh::*;
use super::rock::icosphere;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TreeStyle {
    /// A trunk and a rounded crown of branching limbs.
    Broadleaf,
    /// A straight trunk with whorls of drooping branches (pine, spruce).
    Conifer,
    /// Bare, twisted branches.
    Dead,
}

impl TreeStyle {
    pub const LABELS: [&'static str; 3] = ["Broadleaf", "Conifer", "Dead"];

    pub fn from_index(index: usize) -> Self {
        match index {
            1 => TreeStyle::Conifer,
            2 => TreeStyle::Dead,
            _ => TreeStyle::Broadleaf,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct GrowParams {
    pub style: TreeStyle,
    pub height: f32,
    pub trunk_radius: f32,
    pub levels: u32,
    pub branches: u32,
    pub branch_angle: f32,
    pub length_ratio: f32,
    pub radius_ratio: f32,
    pub taper: f32,
    pub gnarl: f32,
    pub droop: f32,
    pub start: f32,
    pub max_segments: u32,
    pub seed: u32,
}

#[derive(Debug, Clone)]
pub struct Branch {
    pub points: Vec<V3>,
    pub radii: Vec<f32>,
    /// 0 is the trunk.
    pub level: u32,
    /// The last generation: it carries leaves.
    pub terminal: bool,
    /// Where the bark texture starts along this branch (so neighbours do not match).
    pub uv_start: f32,
    /// 0 = wood, 1 = root (roots never carry leaves).
    pub kind: u8,
}

#[derive(Debug, Clone, Default)]
pub struct Skeleton {
    pub branches: Vec<Branch>,
    pub height: f32,
    pub conifer: bool,
    pub crown_center: V3,
}

struct Grower<'a> {
    p: &'a GrowParams,
    rng: Rng,
    out: Vec<Branch>,
    segments: u32,
}

impl Grower<'_> {
    fn grow(
        &mut self,
        start: V3,
        direction: V3,
        length: f32,
        radius: f32,
        level: u32,
        uv_start: f32,
    ) {
        let p = self.p;
        if self.segments >= p.max_segments || radius < 0.004 || length < 0.05 {
            return;
        }
        let steps = (3 + (length * 1.6) as usize).clamp(3, 9);
        let step_length = length / steps as f32;
        let mut points = vec![start];
        let mut directions = vec![direction];
        let mut dir = direction;
        for i in 0..steps {
            let at = *points.last().unwrap();
            let bend = [
                noise3(mul(add(at, [p.seed as f32, 0.0, 0.0]), 1.7), p.seed) - 0.5,
                noise3(mul(add(at, [0.0, p.seed as f32, 0.0]), 1.7), p.seed + 7) - 0.5,
                noise3(mul(add(at, [0.0, 0.0, p.seed as f32]), 1.7), p.seed + 13) - 0.5,
            ];
            let up = if level == 0 { 0.06 } else { 0.0 };
            dir = normalize(add(
                add(dir, mul(bend, p.gnarl * 1.6)),
                [
                    0.0,
                    up - p.droop * 0.22 * (i as f32 + 1.0) / steps as f32,
                    0.0,
                ],
            ));
            points.push(add(at, mul(dir, step_length)));
            directions.push(dir);
        }
        let radii: Vec<f32> = (0..=steps)
            .map(|i| radius * (1.0 - p.taper * (i as f32 / steps as f32)).max(0.04))
            .collect();
        let terminal = level + 1 >= p.levels;
        self.out.push(Branch {
            points: points.clone(),
            radii: radii.clone(),
            level,
            terminal,
            uv_start,
            kind: 0,
        });
        self.segments += steps as u32;
        if terminal {
            return;
        }
        let conifer_trunk = p.style == TreeStyle::Conifer && level == 0;
        let count = if conifer_trunk {
            (p.branches * 4).clamp(6, 40)
        } else {
            p.branches.max(1)
        };
        for k in 0..count {
            let t = if conifer_trunk {
                p.start + (1.0 - p.start) * (k as f32 / count as f32)
            } else {
                p.start + (1.0 - p.start) * self.rng.range(0.0, 1.0)
            };
            let index = ((t * steps as f32) as usize).min(steps);
            let (origin, parent_dir) = (points[index], directions[index]);
            let (u, v) = frame_around(parent_dir);
            let azimuth = if conifer_trunk {
                k as f32 * 2.399 + self.rng.range(-0.3, 0.3)
            } else {
                self.rng.range(0.0, std::f32::consts::TAU)
            };
            let angle = if conifer_trunk {
                (80.0 - p.droop * 25.0 + self.rng.range(-8.0, 8.0)).to_radians()
            } else {
                (p.branch_angle + self.rng.range(-12.0, 12.0)).to_radians()
            };
            let spread = add(mul(u, azimuth.cos()), mul(v, azimuth.sin()));
            let child_dir = normalize(add(mul(parent_dir, angle.cos()), mul(spread, angle.sin())));
            let mut child_length = length * p.length_ratio * self.rng.range(0.75, 1.1);
            if conifer_trunk {
                child_length = p.height * 0.45 * (1.0 - t).powf(0.85) + p.height * 0.05;
            }
            let child_radius = radii[index] * p.radius_ratio;
            let uv = self.rng.range(0.0, 3.0);
            self.grow(origin, child_dir, child_length, child_radius, level + 1, uv);
        }
    }
}

/// Grows the branch skeleton: a trunk and recursive limbs.
pub fn grow(p: &GrowParams) -> Skeleton {
    let mut grower = Grower {
        p,
        rng: Rng::new(p.seed),
        out: Vec::new(),
        segments: 0,
    };
    let lean = grower.rng.direction();
    let direction = normalize([lean[0] * 0.08, 1.0, lean[2] * 0.08]);
    grower.grow([0.0; 3], direction, p.height, p.trunk_radius, 0, 0.0);
    Skeleton {
        branches: grower.out,
        height: p.height,
        conifer: p.style == TreeStyle::Conifer,
        crown_center: [0.0, p.height * 0.75, 0.0],
    }
}

/// Tapered tubes around every branch (parallel-transport rings, tip closed to a point).
pub fn tubes(
    skeleton: &Skeleton,
    radial: u32,
    bark_scale: f32,
    min_level: u32,
    max_level: u32,
) -> Part {
    let sides = radial.clamp(3, 16) as usize;
    let mut part = Part::new(
        "tubes",
        MaterialSpec::Solid {
            rgb: [0.4, 0.3, 0.2],
            roughness: 0.9,
        },
        [0.38, 0.27, 0.18],
    );
    for branch in skeleton
        .branches
        .iter()
        .filter(|b| b.level >= min_level && b.level <= max_level)
    {
        let points = &branch.points;
        let base = part.positions.len() as u32;
        let mut frame_u = frame_around(normalize(sub(points[1], points[0]))).0;
        let mut travelled = branch.uv_start / bark_scale.max(0.01);
        for (i, point) in points.iter().enumerate() {
            let tangent = if i + 1 < points.len() {
                normalize(sub(points[i + 1], *point))
            } else {
                normalize(sub(*point, points[i - 1]))
            };
            frame_u = normalize(sub(frame_u, mul(tangent, dot(frame_u, tangent))));
            let frame_v = cross(tangent, frame_u);
            if i > 0 {
                travelled += length(sub(*point, points[i - 1]));
            }
            for k in 0..=sides {
                let a = k as f32 / sides as f32 * std::f32::consts::TAU;
                let normal = add(mul(frame_u, a.cos()), mul(frame_v, a.sin()));
                part.vertex(
                    add(*point, mul(normal, branch.radii[i])),
                    normal,
                    [k as f32 / sides as f32, travelled * bark_scale],
                );
            }
        }
        let ring = (sides + 1) as u32;
        for i in 0..(points.len() as u32 - 1) {
            for k in 0..sides as u32 {
                let a = base + i * ring + k;
                part.quad(a, a + 1, a + 1 + ring, a + ring);
            }
        }
    }
    part
}

/// Leaf cards along the outer branches.
#[allow(clippy::too_many_arguments)]
pub fn leaf_cards(
    skeleton: &Skeleton,
    density: f32,
    size: f32,
    inner_share: f32,
    max_triangles: usize,
    seed: u32,
) -> Part {
    let mut part = Part::new(
        "leaves",
        MaterialSpec::Solid {
            rgb: [0.3, 0.5, 0.2],
            roughness: 0.8,
        },
        [0.28, 0.5, 0.2],
    );
    let mut rng = Rng::new(seed.wrapping_add(777));
    for branch in skeleton.branches.iter().filter(|b| b.kind == 0) {
        let share = if branch.terminal || skeleton.conifer && branch.level > 0 {
            1.0
        } else if branch.level > 0 && !skeleton.conifer {
            inner_share
        } else {
            0.0
        };
        if share <= 0.0 {
            continue;
        }
        let points = &branch.points;
        let tip = *points.last().unwrap();
        let branch_length: f32 = points.windows(2).map(|w| length(sub(w[1], w[0]))).sum();
        let count = ((branch_length * density * share * if skeleton.conifer { 1.6 } else { 1.0 })
            .ceil() as usize)
            .clamp(2, 60);
        for _ in 0..count {
            if part.triangle_count() * 2 > max_triangles * 2 {
                return part;
            }
            let t = rng.range(if branch.level == 0 { 0.0 } else { 0.35 }, 1.0);
            let f = t * (points.len() - 1) as f32;
            let i = (f as usize).min(points.len() - 2);
            let at = lerp(points[i], points[i + 1], f - i as f32);
            let along = normalize(sub(tip, points[0]));
            let leaf_size = size * rng.range(0.7, 1.2);
            let outward = normalize(sub(at, skeleton.crown_center));
            let random = rng.direction();
            let mut up = normalize(add(
                add(mul(outward, 0.55), mul(random, 0.7)),
                mul(along, 0.25),
            ));
            if skeleton.conifer {
                up = normalize(add(mul(random, 0.35), [outward[0], -0.35, outward[2]]));
            }
            let side = normalize(cross(up, rng.direction()));
            let face = normalize(cross(side, up));
            let normal = normalize(add(mul(outward, 0.7), mul(face, 0.3)));
            let (w, l) = (leaf_size * 0.5, leaf_size);
            let top = add(at, mul(up, l));
            let a = part.vertex(add(at, mul(side, -w * 0.2)), normal, [0.4, 0.02]);
            let b = part.vertex(add(at, mul(side, w * 0.2)), normal, [0.6, 0.02]);
            let c = part.vertex(add(top, mul(side, w)), normal, [1.0, 0.98]);
            let d = part.vertex(add(top, mul(side, -w)), normal, [0.0, 0.98]);
            part.quad(a, b, c, d);
        }
    }
    part
}

/// Low-poly blobs at the terminal branch tips (a stylised crown).
pub fn leaf_blobs(skeleton: &Skeleton, size: f32, detail: u32, seed: u32) -> Part {
    let mut part = Part::new(
        "leaves",
        MaterialSpec::Solid {
            rgb: [0.3, 0.5, 0.2],
            roughness: 0.8,
        },
        [0.28, 0.5, 0.2],
    );
    let mut rng = Rng::new(seed.wrapping_add(31));
    let (directions, faces) = icosphere(detail.clamp(1, 3));
    for branch in skeleton
        .branches
        .iter()
        .filter(|b| b.terminal && b.kind == 0)
    {
        let center = *branch.points.last().unwrap();
        let radius = size * rng.range(0.9, 1.5);
        let base = part.positions.len() as u32;
        for d in &directions {
            let wobble = 0.8 + 0.4 * noise3(add(mul(*d, 1.7), center), seed);
            part.vertex(add(center, mul(*d, radius * wobble)), *d, [0.5, 0.5]);
        }
        for f in &faces {
            part.triangle(base + f[0], base + f[1], base + f[2]);
        }
    }
    part
}
