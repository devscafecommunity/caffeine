//! Height fields: square grids of `f32` in 0..1, plus the noise and filters the nodes are made of.
//!
//! Every pattern is defined on the unit square, never in texels, so a preview at 129 x 129 and the
//! final terrain at 513 x 513 show the same landscape.

#[derive(Debug, Clone, PartialEq)]
pub struct Field {
    pub res: usize,
    pub data: Vec<f32>,
}

impl Field {
    pub fn new(res: usize, value: f32) -> Self {
        Field {
            res,
            data: vec![value; res * res],
        }
    }

    pub fn from_fn(res: usize, f: impl Fn(f32, f32) -> f32) -> Self {
        let mut field = Field::new(res, 0.0);
        let step = 1.0 / (res - 1) as f32;
        for y in 0..res {
            for x in 0..res {
                field.data[y * res + x] = f(x as f32 * step, y as f32 * step);
            }
        }
        field
    }

    #[inline]
    pub fn at(&self, x: usize, y: usize) -> f32 {
        self.data[y.min(self.res - 1) * self.res + x.min(self.res - 1)]
    }

    /// Bilinear sample at unit coordinates (clamped).
    pub fn sample(&self, u: f32, v: f32) -> f32 {
        let max = (self.res - 1) as f32;
        let (x, y) = (u.clamp(0.0, 1.0) * max, v.clamp(0.0, 1.0) * max);
        let (x0, y0) = (x.floor() as usize, y.floor() as usize);
        let (fx, fy) = (x - x0 as f32, y - y0 as f32);
        let a = self.at(x0, y0) * (1.0 - fx) + self.at(x0 + 1, y0) * fx;
        let b = self.at(x0, y0 + 1) * (1.0 - fx) + self.at(x0 + 1, y0 + 1) * fx;
        a * (1.0 - fy) + b * fy
    }

    pub fn map(&self, f: impl Fn(f32) -> f32) -> Field {
        Field {
            res: self.res,
            data: self.data.iter().map(|v| f(*v)).collect(),
        }
    }

    pub fn zip(&self, other: &Field, f: impl Fn(f32, f32) -> f32) -> Field {
        Field {
            res: self.res,
            data: self
                .data
                .iter()
                .zip(&other.data)
                .map(|(a, b)| f(*a, *b))
                .collect(),
        }
    }

    pub fn min_max(&self) -> (f32, f32) {
        self.data
            .iter()
            .fold((f32::MAX, f32::MIN), |(lo, hi), v| (lo.min(*v), hi.max(*v)))
    }

    /// Stretches the values to the full 0..1 range.
    pub fn normalised(&self) -> Field {
        let (lo, hi) = self.min_max();
        let span = (hi - lo).max(1e-6);
        self.map(|v| (v - lo) / span)
    }

    /// Slope as the gradient magnitude in "height per unit square", scaled by `scale`.
    pub fn slope(&self, scale: f32) -> Field {
        let n = self.res;
        let mut out = Field::new(n, 0.0);
        let texel = (n - 1) as f32;
        for y in 0..n {
            for x in 0..n {
                let dx = (self.at(x + 1, y) - self.at(x.saturating_sub(1), y)) * 0.5 * texel;
                let dy = (self.at(x, y + 1) - self.at(x, y.saturating_sub(1))) * 0.5 * texel;
                out.data[y * n + x] = (dx * dx + dy * dy).sqrt() * scale;
            }
        }
        out
    }

    /// Separable box blur, `radius` in texels (applied twice for a smoother kernel).
    pub fn blur(&self, radius: usize) -> Field {
        if radius == 0 {
            return self.clone();
        }
        let n = self.res;
        let pass = |src: &Field, horizontal: bool| {
            let mut out = Field::new(n, 0.0);
            for y in 0..n {
                for x in 0..n {
                    let mut sum = 0.0;
                    let mut count = 0.0;
                    for d in -(radius as i32)..=radius as i32 {
                        let (sx, sy) = if horizontal {
                            ((x as i32 + d).clamp(0, n as i32 - 1) as usize, y)
                        } else {
                            (x, (y as i32 + d).clamp(0, n as i32 - 1) as usize)
                        };
                        sum += src.at(sx, sy);
                        count += 1.0;
                    }
                    out.data[y * n + x] = sum / count;
                }
            }
            out
        };
        let once = pass(&pass(self, true), false);
        pass(&pass(&once, true), false)
    }
}

fn hash(x: i32, y: i32, seed: u32) -> u32 {
    let mut h = (x as u32).wrapping_mul(374_761_393)
        ^ (y as u32).wrapping_mul(668_265_263)
        ^ seed.wrapping_mul(2_246_822_519);
    h = (h ^ (h >> 13)).wrapping_mul(1_274_126_177);
    h ^ (h >> 16)
}

fn gradient(x: i32, y: i32, seed: u32, dx: f32, dy: f32) -> f32 {
    let angle = hash(x, y, seed) as f32 / u32::MAX as f32 * std::f32::consts::TAU;
    angle.cos() * dx + angle.sin() * dy
}

/// 2D gradient (Perlin) noise in about -1..1.
pub fn perlin(x: f32, y: f32, seed: u32) -> f32 {
    let (x0, y0) = (x.floor() as i32, y.floor() as i32);
    let (fx, fy) = (x - x0 as f32, y - y0 as f32);
    let fade = |t: f32| t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    let (u, v) = (fade(fx), fade(fy));
    let n00 = gradient(x0, y0, seed, fx, fy);
    let n10 = gradient(x0 + 1, y0, seed, fx - 1.0, fy);
    let n01 = gradient(x0, y0 + 1, seed, fx, fy - 1.0);
    let n11 = gradient(x0 + 1, y0 + 1, seed, fx - 1.0, fy - 1.0);
    let a = n00 + (n10 - n00) * u;
    let b = n01 + (n11 - n01) * u;
    (a + (b - a) * v) * 1.41
}

/// Fractal Brownian motion in about 0..1.
pub fn fbm(x: f32, y: f32, seed: u32, octaves: u32, persistence: f32, lacunarity: f32) -> f32 {
    let (mut total, mut amplitude, mut norm, mut frequency) = (0.0, 1.0, 0.0, 1.0);
    for octave in 0..octaves.max(1) {
        total += perlin(
            x * frequency,
            y * frequency,
            seed.wrapping_add(octave * 131),
        ) * amplitude;
        norm += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    (total / norm * 0.5 + 0.5).clamp(0.0, 1.0)
}

/// Ridged multifractal: sharp crests where the noise crosses zero.
pub fn ridged(x: f32, y: f32, seed: u32, octaves: u32, gain: f32, lacunarity: f32) -> f32 {
    let (mut total, mut amplitude, mut norm, mut frequency, mut weight) =
        (0.0, 1.0, 0.0, 1.0, 1.0f32);
    for octave in 0..octaves.max(1) {
        let n = 1.0
            - perlin(
                x * frequency,
                y * frequency,
                seed.wrapping_add(octave * 197),
            )
            .abs();
        let n = n * n * weight;
        weight = (n * gain).clamp(0.0, 1.0);
        total += n * amplitude;
        norm += amplitude;
        amplitude *= 0.5;
        frequency *= lacunarity;
    }
    (total / norm).clamp(0.0, 1.0)
}

/// Voronoi: distance to the nearest feature point (F1), the second (F2) and the cell's random value.
pub fn voronoi(x: f32, y: f32, seed: u32, jitter: f32) -> (f32, f32, f32) {
    let (cx, cy) = (x.floor() as i32, y.floor() as i32);
    let (mut f1, mut f2, mut id) = (f32::MAX, f32::MAX, 0.0);
    for j in -1..=1 {
        for i in -1..=1 {
            let (gx, gy) = (cx + i, cy + j);
            let h = hash(gx, gy, seed);
            let px = gx as f32 + 0.5 + ((h & 0xFFFF) as f32 / 65535.0 - 0.5) * jitter;
            let py = gy as f32 + 0.5 + ((h >> 16) as f32 / 65535.0 - 0.5) * jitter;
            let d = ((px - x).powi(2) + (py - y).powi(2)).sqrt();
            if d < f1 {
                f2 = f1;
                f1 = d;
                id = (h >> 8) as f32 / 16_777_215.0;
            } else if d < f2 {
                f2 = d;
            }
        }
    }
    (f1, f2, id)
}
