//! A small software rasteriser used for the editor preview and the documentation images.
//!
//! It draws a [`NatureModel`] with a z-buffer, simple lighting and supersampling, orbiting around
//! the model's centre. Card parts (leaves, petals, blades) are cut with a leaf-shaped alpha test
//! so their silhouettes read like the real cut-out textures. It is a preview, not the engine's
//! renderer: no textures, shadows or tone mapping.

use super::mesh::*;
use super::NatureModel;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct View {
    pub yaw: f32,
    pub pitch: f32,
    pub zoom: f32,
}

impl Default for View {
    fn default() -> Self {
        View {
            yaw: 0.6,
            pitch: 0.28,
            zoom: 1.0,
        }
    }
}

const SUPERSAMPLE: usize = 2;

/// Renders to RGBA8 (`width * height * 4` bytes).
pub fn render(model: &NatureModel, width: u32, height: u32, view: View) -> Vec<u8> {
    let (w, h) = (width as usize * SUPERSAMPLE, height as usize * SUPERSAMPLE);
    let mut color = vec![[0.0f32; 3]; w * h];
    let mut depth = vec![f32::MAX; w * h];
    // Background: a soft vertical gradient.
    for y in 0..h {
        let t = y as f32 / h as f32;
        let bg = [
            0.17 + 0.07 * (1.0 - t),
            0.19 + 0.08 * (1.0 - t),
            0.23 + 0.1 * (1.0 - t),
        ];
        for x in 0..w {
            color[y * w + x] = bg;
        }
    }
    let (lo, hi) = model.bounds();
    if lo[0] <= hi[0] {
        let center = lerp(lo, hi, 0.5);
        let radius = (length(sub(hi, lo)) * 0.5).max(0.05);
        let distance = radius * 2.5 / view.zoom.clamp(0.2, 8.0);
        let (sy, cy) = view.yaw.sin_cos();
        let (sp, cp) = view.pitch.clamp(-1.4, 1.4).sin_cos();
        let eye = add(center, mul([sy * cp, sp, cy * cp], distance));
        let forward = normalize(sub(center, eye));
        let right = normalize(cross(forward, [0.0, 1.0, 0.0]));
        let up = cross(right, forward);
        let focal = 1.0 / (40.0f32.to_radians() * 0.5).tan();
        let light = normalize(add(add(mul(up, 0.6), mul(right, -0.5)), mul(forward, -0.7)));
        let project = |p: V3| -> Option<(f32, f32, f32)> {
            let rel = sub(p, eye);
            let z = dot(rel, forward);
            if z < 0.05 {
                return None;
            }
            let x = dot(rel, right) * focal / z;
            let y = dot(rel, up) * focal / z;
            Some((
                (x * 0.5 + 0.5) * w as f32,
                (0.5 - y * 0.5 * (w as f32 / h as f32)) * h as f32,
                z,
            ))
        };
        // The ground disc under the model reads as a contact shadow.
        draw_ground(&mut color, w, h, &project, center, lo[1], radius);
        for part in &model.parts {
            let card = matches!(part.material, MaterialSpec::Cards(_));
            for tri in part.indices.chunks_exact(3) {
                let ids = [tri[0] as usize, tri[1] as usize, tri[2] as usize];
                let pts: Vec<Option<(f32, f32, f32)>> =
                    ids.iter().map(|i| project(part.positions[*i])).collect();
                let (Some(a), Some(b), Some(c)) = (pts[0], pts[1], pts[2]) else {
                    continue;
                };
                let area = (b.0 - a.0) * (c.1 - a.1) - (c.0 - a.0) * (b.1 - a.1);
                if area.abs() < 1e-6 {
                    continue;
                }
                let min_x = a.0.min(b.0).min(c.0).floor().max(0.0) as usize;
                let max_x = (a.0.max(b.0).max(c.0).ceil() as isize)
                    .min(w as isize - 1)
                    .max(0) as usize;
                let min_y = a.1.min(b.1).min(c.1).floor().max(0.0) as usize;
                let max_y = (a.1.max(b.1).max(c.1).ceil() as isize)
                    .min(h as isize - 1)
                    .max(0) as usize;
                for y in min_y..=max_y {
                    for x in min_x..=max_x {
                        let (px, py) = (x as f32 + 0.5, y as f32 + 0.5);
                        let w0 = ((b.0 - px) * (c.1 - py) - (c.0 - px) * (b.1 - py)) / area;
                        let w1 = ((c.0 - px) * (a.1 - py) - (a.0 - px) * (c.1 - py)) / area;
                        let w2 = 1.0 - w0 - w1;
                        if w0 < 0.0 || w1 < 0.0 || w2 < 0.0 {
                            continue;
                        }
                        let z = w0 * a.2 + w1 * b.2 + w2 * c.2;
                        if z >= depth[y * w + x] {
                            continue;
                        }
                        if card {
                            let u = w0 * part.uvs[ids[0]][0]
                                + w1 * part.uvs[ids[1]][0]
                                + w2 * part.uvs[ids[2]][0];
                            let v = w0 * part.uvs[ids[0]][1]
                                + w1 * part.uvs[ids[1]][1]
                                + w2 * part.uvs[ids[2]][1];
                            let profile = (std::f32::consts::PI * v.clamp(0.0, 1.0))
                                .sin()
                                .max(0.0)
                                .powf(0.7);
                            if (u - 0.5).abs() * 2.0 > profile {
                                continue;
                            }
                        }
                        let mut normal = [0.0f32; 3];
                        for (k, weight) in [w0, w1, w2].iter().enumerate() {
                            normal = add(normal, mul(part.normals[ids[k]], *weight));
                        }
                        let normal = normalize(normal);
                        // Cards and solids are lit from both sides.
                        let facing = normal;
                        let lambert = dot(facing, light).abs() * 0.62 + 0.38;
                        let sky = 0.82 + 0.18 * facing[1];
                        let shade = (lambert * sky).clamp(0.0, 1.2);
                        depth[y * w + x] = z;
                        color[y * w + x] = [
                            (part.preview[0] * shade).min(1.0),
                            (part.preview[1] * shade).min(1.0),
                            (part.preview[2] * shade).min(1.0),
                        ];
                    }
                }
            }
        }
    }
    // Box-filter down to the requested size.
    let mut out = vec![0u8; width as usize * height as usize * 4];
    let n = (SUPERSAMPLE * SUPERSAMPLE) as f32;
    for y in 0..height as usize {
        for x in 0..width as usize {
            let mut sum = [0.0f32; 3];
            for sy in 0..SUPERSAMPLE {
                for sx in 0..SUPERSAMPLE {
                    let c = color[(y * SUPERSAMPLE + sy) * w + x * SUPERSAMPLE + sx];
                    for k in 0..3 {
                        sum[k] += c[k] / n;
                    }
                }
            }
            let o = (y * width as usize + x) * 4;
            out[o] = (sum[0] * 255.0) as u8;
            out[o + 1] = (sum[1] * 255.0) as u8;
            out[o + 2] = (sum[2] * 255.0) as u8;
            out[o + 3] = 255;
        }
    }
    out
}

fn draw_ground(
    color: &mut [[f32; 3]],
    w: usize,
    h: usize,
    project: &dyn Fn(V3) -> Option<(f32, f32, f32)>,
    center: V3,
    floor: f32,
    radius: f32,
) {
    let ring: Vec<(f32, f32)> = (0..48)
        .filter_map(|k| {
            let a = k as f32 / 48.0 * std::f32::consts::TAU;
            project([
                center[0] + a.cos() * radius * 1.6,
                floor,
                center[2] + a.sin() * radius * 1.6,
            ])
            .map(|p| (p.0, p.1))
        })
        .collect();
    if ring.len() < 8 {
        return;
    }
    let cx = ring.iter().map(|p| p.0).sum::<f32>() / ring.len() as f32;
    let cy = ring.iter().map(|p| p.1).sum::<f32>() / ring.len() as f32;
    let rx = ring
        .iter()
        .map(|p| (p.0 - cx).abs())
        .fold(0.0, f32::max)
        .max(1.0);
    let ry = ring
        .iter()
        .map(|p| (p.1 - cy).abs())
        .fold(0.0, f32::max)
        .max(1.0);
    for y in 0..h {
        for x in 0..w {
            let d = ((x as f32 - cx) / rx).powi(2) + ((y as f32 - cy) / ry).powi(2);
            if d < 1.0 {
                let t = (1.0 - d).powf(0.7) * 0.5;
                let c = &mut color[y * w + x];
                for k in 0..3 {
                    c[k] = c[k] * (1.0 - t) + [0.30, 0.36, 0.27][k] * t;
                }
            }
        }
    }
}
