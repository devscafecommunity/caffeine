//! Erosion: thermal (talus slumping) and hydraulic (water droplets that carry sediment).
//!
//! Both work on the unit-height field and are deterministic for a seed. Hydraulic erosion also
//! returns a *wear* map (where soil was removed) and a *deposit* map (where it settled), which the
//! graph can use as masks.

use super::field::Field;

struct Rng(u64);
impl Rng {
    fn new(seed: u32) -> Self {
        Rng((seed as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1)
    }
    fn unit(&mut self) -> f32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        (self.0 >> 40) as f32 / 16_777_216.0
    }
}

const NEIGHBOURS: [(i32, i32); 4] = [(1, 0), (-1, 0), (0, 1), (0, -1)];

/// Moves material from slopes steeper than `talus` (a slope in unit heights per unit of width) to
/// lower neighbours.
pub fn thermal(field: &Field, iterations: u32, talus: f32, strength: f32) -> Field {
    let n = field.res;
    let mut h = field.clone();
    let threshold = talus / (n - 1) as f32;
    for _ in 0..iterations {
        let before = h.clone();
        for y in 0..n {
            for x in 0..n {
                let centre = before.at(x, y);
                let mut total = 0.0;
                let mut biggest = 0.0f32;
                let mut drops = [0.0f32; 4];
                for (k, (dx, dy)) in NEIGHBOURS.iter().enumerate() {
                    let (nx, ny) = (x as i32 + dx, y as i32 + dy);
                    if nx < 0 || ny < 0 || nx >= n as i32 || ny >= n as i32 {
                        continue;
                    }
                    let d = centre - before.at(nx as usize, ny as usize);
                    if d > threshold {
                        drops[k] = d - threshold;
                        total += drops[k];
                        biggest = biggest.max(drops[k]);
                    }
                }
                if total <= 0.0 {
                    continue;
                }
                let moved = biggest * 0.5 * strength;
                h.data[y * n + x] -= moved;
                for (k, (dx, dy)) in NEIGHBOURS.iter().enumerate() {
                    if drops[k] > 0.0 {
                        let (nx, ny) = ((x as i32 + dx) as usize, (y as i32 + dy) as usize);
                        h.data[ny * n + nx] += moved * drops[k] / total;
                    }
                }
            }
        }
    }
    h
}

#[derive(Debug, Clone, Copy)]
pub struct Hydraulic {
    pub droplets: u32,
    pub inertia: f32,
    pub capacity: f32,
    pub erosion: f32,
    pub deposition: f32,
    pub evaporation: f32,
    pub seed: u32,
}

pub struct Eroded {
    pub height: Field,
    pub wear: Field,
    pub deposit: Field,
}

/// Bilinear height and gradient (per texel) at a fractional texel position.
fn gradient_and_height(h: &Field, x: f32, y: f32) -> (f32, f32, f32) {
    let (xi, yi) = (x.floor() as usize, y.floor() as usize);
    let (fx, fy) = (x - xi as f32, y - yi as f32);
    let (a, b, c, d) = (
        h.at(xi, yi),
        h.at(xi + 1, yi),
        h.at(xi, yi + 1),
        h.at(xi + 1, yi + 1),
    );
    (
        (b - a) * (1.0 - fy) + (d - c) * fy,
        (c - a) * (1.0 - fx) + (d - b) * fx,
        a * (1.0 - fx) * (1.0 - fy) + b * fx * (1.0 - fy) + c * (1.0 - fx) * fy + d * fx * fy,
    )
}

/// Particle-based hydraulic erosion: each droplet rolls downhill, picks up sediment while fast and
/// steep, and drops it where it slows down.
pub fn hydraulic(field: &Field, p: Hydraulic) -> Eroded {
    let n = field.res;
    let mut h = field.clone();
    let mut wear = Field::new(n, 0.0);
    let mut deposit = Field::new(n, 0.0);
    let mut rng = Rng::new(p.seed);
    for _ in 0..p.droplets {
        let mut x = rng.unit() * (n as f32 - 4.0) + 1.5;
        let mut y = rng.unit() * (n as f32 - 4.0) + 1.5;
        let (mut dx, mut dy, mut speed, mut water, mut sediment) =
            (0.0f32, 0.0f32, 1.0f32, 1.0f32, 0.0f32);
        for _ in 0..56 {
            let (xi, yi) = (x.floor() as usize, y.floor() as usize);
            if xi + 2 >= n || yi + 2 >= n {
                break;
            }
            let (gx, gy, height) = gradient_and_height(&h, x, y);
            dx = dx * p.inertia - gx * (1.0 - p.inertia) * 256.0;
            dy = dy * p.inertia - gy * (1.0 - p.inertia) * 256.0;
            let len = (dx * dx + dy * dy).sqrt();
            if len < 1e-9 {
                break;
            }
            dx /= len;
            dy /= len;
            let (nx, ny) = (x + dx, y + dy);
            if nx < 1.0 || ny < 1.0 || nx >= n as f32 - 2.0 || ny >= n as f32 - 2.0 {
                break;
            }
            let (_, _, new_height) = gradient_and_height(&h, nx, ny);
            let delta = new_height - height; // negative going downhill
            let capacity = (-delta).max(0.0004) * speed * water * p.capacity * 12.0;
            let (fx, fy) = (x - xi as f32, y - yi as f32);
            let weights = [
                (xi, yi, (1.0 - fx) * (1.0 - fy)),
                (xi + 1, yi, fx * (1.0 - fy)),
                (xi, yi + 1, (1.0 - fx) * fy),
                (xi + 1, yi + 1, fx * fy),
            ];
            if sediment > capacity || delta > 0.0 {
                let amount = if delta > 0.0 {
                    delta.min(sediment)
                } else {
                    (sediment - capacity) * p.deposition
                };
                sediment -= amount;
                for (wx, wy, w) in weights {
                    h.data[wy * n + wx] += amount * w;
                    deposit.data[wy * n + wx] += amount * w;
                }
            } else {
                let amount = ((capacity - sediment) * p.erosion).min((-delta).max(0.0));
                sediment += amount;
                for (wx, wy, w) in weights {
                    h.data[wy * n + wx] -= amount * w;
                    wear.data[wy * n + wx] += amount * w;
                }
            }
            speed = (speed * speed - delta * 600.0)
                .max(0.0)
                .sqrt()
                .clamp(0.1, 4.0);
            water *= 1.0 - p.evaporation;
            x = nx;
            y = ny;
            if water < 0.02 {
                break;
            }
        }
    }
    Eroded {
        height: h,
        wear: wear.normalised(),
        deposit: deposit.normalised(),
    }
}
