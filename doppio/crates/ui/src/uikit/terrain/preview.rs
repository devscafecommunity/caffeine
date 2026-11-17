//! A software 3D preview of a terrain: a z-buffered, lit heightfield with the splat colours and a
//! translucent sea plane. Drag to orbit, scroll to zoom. It shows shape and layers, not the engine's
//! final shading.

use super::TerrainModel;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct View {
    pub yaw: f32,
    pub pitch: f32,
    pub zoom: f32,
}

impl Default for View {
    fn default() -> Self {
        View {
            yaw: 0.8,
            pitch: 0.82,
            zoom: 1.0,
        }
    }
}

const GROUND: [f32; 3] = [0.30, 0.44, 0.20];
const ROCK: [f32; 3] = [0.46, 0.44, 0.41];
const DIRT: [f32; 3] = [0.64, 0.54, 0.38];
const MUD: [f32; 3] = [0.24, 0.21, 0.17];
const SEA: [f32; 3] = [0.07, 0.30, 0.45];

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}
fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
fn normalise(a: [f32; 3]) -> [f32; 3] {
    let l = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt().max(1e-9);
    [a[0] / l, a[1] / l, a[2] / l]
}

struct Target {
    w: usize,
    h: usize,
    depth: Vec<f32>,
    pixels: Vec<u8>,
}

impl Target {
    fn triangle(&mut self, p: [[f32; 3]; 3], colours: [[f32; 3]; 3], alpha: f32) {
        let area =
            (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
        if area.abs() < 1e-6 {
            return;
        }
        let min_x = p
            .iter()
            .map(|q| q[0])
            .fold(f32::MAX, f32::min)
            .floor()
            .max(0.0) as usize;
        let max_x = (p
            .iter()
            .map(|q| q[0])
            .fold(f32::MIN, f32::max)
            .ceil()
            .max(0.0) as usize)
            .min(self.w - 1);
        let min_y = p
            .iter()
            .map(|q| q[1])
            .fold(f32::MAX, f32::min)
            .floor()
            .max(0.0) as usize;
        let max_y = (p
            .iter()
            .map(|q| q[1])
            .fold(f32::MIN, f32::max)
            .ceil()
            .max(0.0) as usize)
            .min(self.h - 1);
        for y in min_y..=max_y {
            for x in min_x..=max_x {
                let (px, py) = (x as f32 + 0.5, y as f32 + 0.5);
                let w0 = ((p[1][0] - px) * (p[2][1] - py) - (p[2][0] - px) * (p[1][1] - py)) / area;
                let w1 = ((p[2][0] - px) * (p[0][1] - py) - (p[0][0] - px) * (p[2][1] - py)) / area;
                let w2 = 1.0 - w0 - w1;
                if w0 < 0.0 || w1 < 0.0 || w2 < 0.0 {
                    continue;
                }
                let z = p[0][2] * w0 + p[1][2] * w1 + p[2][2] * w2;
                let o = y * self.w + x;
                if z >= self.depth[o] {
                    continue;
                }
                let mut rgb = [0.0f32; 3];
                for (c, value) in rgb.iter_mut().enumerate() {
                    *value = colours[0][c] * w0 + colours[1][c] * w1 + colours[2][c] * w2;
                }
                if alpha >= 1.0 {
                    self.depth[o] = z;
                    for (c, value) in rgb.iter().enumerate() {
                        self.pixels[o * 4 + c] =
                            (value.clamp(0.0, 1.0).powf(1.0 / 2.2) * 255.0) as u8;
                    }
                } else {
                    for (c, value) in rgb.iter().enumerate() {
                        let old = (self.pixels[o * 4 + c] as f32 / 255.0).powf(2.2);
                        let mixed = old * (1.0 - alpha) + value * alpha;
                        self.pixels[o * 4 + c] =
                            (mixed.clamp(0.0, 1.0).powf(1.0 / 2.2) * 255.0) as u8;
                    }
                }
            }
        }
    }
}

pub fn render(model: &TerrainModel, sea_unit: f32, width: u32, height: u32, view: View) -> Vec<u8> {
    let (w, h) = (width as usize, height as usize);
    let mut target = Target {
        w,
        h,
        depth: vec![f32::MAX; w * h],
        pixels: vec![255; w * h * 4],
    };
    // Background: a soft vertical gradient.
    for y in 0..h {
        let t = y as f32 / h as f32;
        for x in 0..w {
            let o = (y * w + x) * 4;
            target.pixels[o] = (40.0 + 28.0 * (1.0 - t)) as u8;
            target.pixels[o + 1] = (46.0 + 36.0 * (1.0 - t)) as u8;
            target.pixels[o + 2] = (58.0 + 50.0 * (1.0 - t)) as u8;
        }
    }
    let field = &model.height;
    let n = field.res;
    let stride = ((n - 1) / 96).max(1);
    let cells = (n - 1) / stride;
    // World: x, z in metres centred on the terrain, y up. Slight vertical exaggeration keeps low relief readable.
    let (sx, sz) = (model.size[0], model.size[1]);
    let exaggeration = 1.4;
    let vertex = |i: usize, j: usize| -> [f32; 3] {
        let (gx, gy) = (i * stride, j * stride);
        [
            (gx as f32 / (n - 1) as f32 - 0.5) * sx,
            field.at(gx, gy) * model.max_height * exaggeration,
            (gy as f32 / (n - 1) as f32 - 0.5) * sz,
        ]
    };
    let radius = (sx * sx + sz * sz).sqrt() * 0.5;
    let (sin_y, cos_y) = view.yaw.sin_cos();
    let (sin_p, cos_p) = view.pitch.sin_cos();
    let scale = (w.min(h) as f32 * 0.46) / radius * view.zoom;
    let project = |p: [f32; 3]| -> [f32; 3] {
        let (x, z) = (p[0] * cos_y - p[2] * sin_y, p[0] * sin_y + p[2] * cos_y);
        let (y, z) = (p[1] * cos_p - z * sin_p, p[1] * sin_p + z * cos_p);
        // Smaller depth is nearer: higher points and points closer to the camera win the z-test.
        [w as f32 * 0.5 + x * scale, h as f32 * 0.58 - y * scale, -z]
    };
    let sun = normalise([-0.5, 0.75, 0.35]);
    let colour_at = |i: usize, j: usize| -> [f32; 3] {
        let o = ((j * stride).min(n - 1) * n + (i * stride).min(n - 1)) * 4;
        let weights = [
            model.splat[o] as f32 / 255.0,
            model.splat[o + 1] as f32 / 255.0,
            model.splat[o + 2] as f32 / 255.0,
            model.splat[o + 3] as f32 / 255.0,
        ];
        let mut rgb = [0.0f32; 3];
        for (layer, base) in [GROUND, ROCK, DIRT, MUD].iter().enumerate() {
            for c in 0..3 {
                rgb[c] += base[c] * weights[layer];
            }
        }
        rgb
    };
    for j in 0..cells {
        for i in 0..cells {
            let quad = [(i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1)];
            let world: Vec<[f32; 3]> = quad.iter().map(|(a, b)| vertex(*a, *b)).collect();
            let colours: Vec<[f32; 3]> = quad.iter().map(|(a, b)| colour_at(*a, *b)).collect();
            for tri in [[0usize, 1, 2], [1, 3, 2]] {
                let (a, b, c) = (world[tri[0]], world[tri[1]], world[tri[2]]);
                let mut normal = normalise(cross(sub(c, a), sub(b, a)));
                if normal[1] < 0.0 {
                    normal = [-normal[0], -normal[1], -normal[2]];
                }
                let light = 0.28
                    + 0.72
                        * (normal[0] * sun[0] + normal[1] * sun[1] + normal[2] * sun[2]).max(0.0);
                let lit = |k: usize| {
                    let col = colours[tri[k]];
                    [col[0] * light, col[1] * light, col[2] * light]
                };
                target.triangle(
                    [project(a), project(b), project(c)],
                    [lit(0), lit(1), lit(2)],
                    1.0,
                );
            }
        }
    }
    // The sea: a translucent plane at the sea level, drawn after the land (z-tested against it).
    let sea_y = sea_unit * model.max_height * exaggeration;
    let corners = [
        [-sx * 0.5, sea_y, -sz * 0.5],
        [sx * 0.5, sea_y, -sz * 0.5],
        [-sx * 0.5, sea_y, sz * 0.5],
        [sx * 0.5, sea_y, sz * 0.5],
    ];
    let projected: Vec<[f32; 3]> = corners.iter().map(|c| project(*c)).collect();
    if sea_unit > 0.001 {
        for tri in [[0usize, 1, 2], [1, 3, 2]] {
            target.triangle(
                [projected[tri[0]], projected[tri[1]], projected[tri[2]]],
                [SEA, SEA, SEA],
                0.55,
            );
        }
    }
    target.pixels
}
