//! The masks a terrain graph exports to the engine next to its height (photoreal roadmap, phase 1).
//!
//! The engine derives `height`, `slope` and `curvature` from the heightfield itself, exactly, so they are not made
//! here. What a heightfield cannot tell is where water *goes* and what erosion *did*: this module makes
//!
//! * `flow`: how much of the terrain drains through a texel (D8 flow accumulation), log-scaled to 0..1;
//! * `wetness`: where the ground stays wet: along drainage lines that are not steep, in hollows, and at and
//!   below the sea level;
//! * `wear` and `deposit`: what hydraulic erosion took and left, when its outputs are wired to the Output node.
//!
//! All are 8 bits, on the heightfield's grid, deterministic (no randomness, ties broken by index).

use super::field::Field;

fn smoothstep(a: f32, b: f32, x: f32) -> f32 {
    let t = ((x - a) / (b - a)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// D8 flow accumulation: every texel starts with one unit of water and passes everything it has to its
/// steepest lower neighbour (diagonals count as longer); a pit keeps what it gets. Returns the accumulated
/// water as `ln(1 + area) / ln(1 + max area)`, 0..1.
pub fn flow_accumulation(height: &Field) -> Field {
    let n = height.res;
    let mut order: Vec<usize> = (0..n * n).collect();
    // Highest first; equal heights by index, so the result never depends on the sort's stability.
    order.sort_by(|&a, &b| {
        height.data[b]
            .total_cmp(&height.data[a])
            .then_with(|| a.cmp(&b))
    });
    let mut water = vec![1.0f32; n * n];
    for &i in &order {
        let (x, y) = ((i % n) as i32, (i / n) as i32);
        let mut best: Option<(usize, f32)> = None;
        for dy in -1i32..=1 {
            for dx in -1i32..=1 {
                if dx == 0 && dy == 0 {
                    continue;
                }
                let (nx, ny) = (x + dx, y + dy);
                if nx < 0 || ny < 0 || nx >= n as i32 || ny >= n as i32 {
                    continue;
                }
                let j = ny as usize * n + nx as usize;
                let drop = height.data[i] - height.data[j];
                if drop <= 0.0 {
                    continue;
                }
                let run = if dx != 0 && dy != 0 {
                    std::f32::consts::SQRT_2
                } else {
                    1.0
                };
                let steepness = drop / run;
                if best.is_none_or(|(_, s)| steepness > s) {
                    best = Some((j, steepness));
                }
            }
        }
        if let Some((j, _)) = best {
            water[j] += water[i];
        }
    }
    let max = water.iter().copied().fold(1.0f32, f32::max);
    let scale = 1.0 / (1.0 + max).ln();
    Field {
        res: n,
        data: water.iter().map(|w| (1.0 + w).ln() * scale).collect(),
    }
}

/// Wetness 0..1 from the drainage (`flow`), the slope (rise over run, as in the splat weights) and the sea:
/// drainage lines that are not steep, a little around them, and everything at or below the sea level.
pub fn wetness(height: &Field, flow: &Field, slope: &Field, sea: f32, beach: f32) -> Field {
    let n = height.res;
    let mut data = Vec::with_capacity(n * n);
    for i in 0..n * n {
        let drainage =
            smoothstep(0.45, 0.85, flow.data[i]) * (1.0 - smoothstep(0.5, 1.4, slope.data[i]));
        let shore = 1.0 - smoothstep(sea, sea + beach.max(0.004) * 2.0, height.data[i]);
        data.push(drainage.max(shore).clamp(0.0, 1.0));
    }
    Field { res: n, data }
}

/// A field scaled so its largest value is 1 (all zeros stay zero), as bytes.
pub fn normalised_bytes(field: &Field) -> Vec<u8> {
    let max = field.data.iter().copied().fold(0.0f32, f32::max);
    let scale = if max > 1e-9 { 1.0 / max } else { 0.0 };
    to_bytes_scaled(field, scale)
}

/// A field already in 0..1, as bytes.
pub fn to_bytes(field: &Field) -> Vec<u8> {
    to_bytes_scaled(field, 1.0)
}

fn to_bytes_scaled(field: &Field, scale: f32) -> Vec<u8> {
    field
        .data
        .iter()
        .map(|v| (v * scale).clamp(0.0, 1.0).mul_add(255.0, 0.5) as u8)
        .collect()
}
