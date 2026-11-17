//! A top-down preview of a water body, computed on the CPU from the same waves the engine animates.
//!
//! It shows the shape of the surface (slopes lit by a sun, sky reflection by a Fresnel term, and
//! foam where crests compress), not the final engine shading. `time` animates it.

use doppio_engine::Water;

const GRAVITY: f32 = 9.81;

/// One wave with everything that does not depend on the pixel or the time worked out once.
struct Prepared {
    dir: [f32; 2],
    k: f32,
    omega: f32,
    amplitude: f32,
    steepness: f32,
    phase: f32,
}

fn prepare(water: &Water) -> Vec<Prepared> {
    water
        .waves
        .iter()
        .map(|wave| {
            let len = (wave.direction[0].powi(2) + wave.direction[1].powi(2))
                .sqrt()
                .max(1e-6);
            let k = std::f32::consts::TAU / wave.wavelength.max(0.05);
            Prepared {
                dir: [wave.direction[0] / len, wave.direction[1] / len],
                k,
                omega: (GRAVITY * k * (k * water.depth.max(0.01)).tanh())
                    .max(0.0)
                    .sqrt(),
                amplitude: wave.amplitude,
                steepness: wave.steepness,
                phase: wave.phase,
            }
        })
        .collect()
}

fn sample_prepared(waves: &[Prepared], x: f32, z: f32, clock: f32) -> (f32, f32, f32, f32) {
    let (mut h, mut sx, mut sz, mut j) = (0.0f32, 0.0f32, 0.0f32, 1.0f32);
    for wave in waves {
        let theta = wave.k * (wave.dir[0] * x + wave.dir[1] * z) - wave.omega * clock + wave.phase;
        let (sin, cos) = theta.sin_cos();
        h += wave.amplitude * cos;
        sx -= wave.amplitude * wave.k * wave.dir[0] * sin;
        sz -= wave.amplitude * wave.k * wave.dir[1] * sin;
        j -= wave.steepness * wave.k * wave.amplitude * cos;
    }
    (h, sx, sz, j)
}

/// Height, slope (dh/dx, dh/dz) and the Jacobian of the horizontal mapping at a rest position.
pub fn sample(water: &Water, x: f32, z: f32, time: f32) -> (f32, f32, f32, f32) {
    sample_prepared(&prepare(water), x, z, time * water.time_scale)
}

/// The longest wave, to frame the preview.
fn longest(water: &Water) -> f32 {
    water.waves.iter().map(|w| w.wavelength).fold(2.0, f32::max)
}

fn smoothstep(a: f32, b: f32, x: f32) -> f32 {
    let t = ((x - a) / (b - a)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// `size` x `size` RGBA8.
pub fn render(water: &Water, size: u32, time: f32) -> Vec<u8> {
    let waves = prepare(water);
    let clock = time * water.time_scale;
    let amplitude_sum = water
        .waves
        .iter()
        .map(|w| w.amplitude)
        .sum::<f32>()
        .max(0.05);
    let span = (longest(water) * 2.0)
        .clamp(8.0, 160.0)
        .min(water.size[0].max(water.size[1]));
    let mut pixels = vec![255u8; (size * size * 4) as usize];
    let body = [water.albedo[0], water.albedo[1], water.albedo[2]];
    let sun = {
        let v = [-0.45f32, 0.75, 0.35];
        let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
        [v[0] / l, v[1] / l, v[2] / l]
    };
    for py in 0..size {
        for px in 0..size {
            let x = (px as f32 / size as f32 - 0.5) * span;
            let z = (py as f32 / size as f32 - 0.5) * span;
            let (h, sx, sz, j) = sample_prepared(&waves, x, z, clock);
            let n = {
                let v = [-sx, 1.0, -sz];
                let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
                [v[0] / l, v[1] / l, v[2] / l]
            };
            // Looking mostly down: the view vector is +Y tilted a little toward -Z.
            let view = [0.0, 0.94, -0.34];
            let n_dot_v = (n[0] * view[0] + n[1] * view[1] + n[2] * view[2]).clamp(0.0, 1.0);
            let fresnel = 0.02 + 0.98 * (1.0 - n_dot_v).powi(5);
            let diffuse = (n[0] * sun[0] + n[1] * sun[1] + n[2] * sun[2]).max(0.0);
            // Deeper troughs are a little darker; opacity lets the colour through.
            let depth_tint = (0.8 + h * 0.6 / amplitude_sum).clamp(0.55, 1.15);
            let reflect_y = 2.0 * n_dot_v * n[1] - view[1];
            let sky = [
                0.45 + 0.25 * reflect_y.max(0.0),
                0.62 + 0.2 * reflect_y.max(0.0),
                0.85,
            ];
            let half = {
                let v = [sun[0] + view[0], sun[1] + view[1], sun[2] + view[2]];
                let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
                [v[0] / l, v[1] / l, v[2] / l]
            };
            let spec = (n[0] * half[0] + n[1] * half[1] + n[2] * half[2])
                .max(0.0)
                .powf(60.0 + 600.0 * (1.0 - water.roughness.clamp(0.02, 0.6)));
            let foam = smoothstep(water.foam_threshold, water.foam_threshold - 0.22, j);
            let mut out = [0.0f32; 3];
            for c in 0..3 {
                let lit = body[c] * (0.35 + 0.65 * diffuse) * depth_tint;
                let mut v = lit * water.albedo[3] + body[c] * 0.6 * (1.0 - water.albedo[3]);
                v = v * (1.0 - fresnel) + sky[c] * fresnel;
                v += spec * 0.6;
                v = v * (1.0 - foam * 0.85) + 0.95 * foam * 0.85;
                out[c] = v.clamp(0.0, 1.0).powf(1.0 / 2.2);
            }
            let o = ((py * size + px) * 4) as usize;
            pixels[o] = (out[0] * 255.0) as u8;
            pixels[o + 1] = (out[1] * 255.0) as u8;
            pixels[o + 2] = (out[2] * 255.0) as u8;
        }
    }
    pixels
}
