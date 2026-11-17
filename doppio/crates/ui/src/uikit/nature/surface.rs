//! Generated surfaces: a full PBR texture set (albedo, normal + height, occlusion/roughness/metal) for
//! one part, driven by the [`MeshMaps`] baked from its own geometry.
//!
//! Layers for **rock**: stone colour with strata, fine grain, cracks that follow ridged noise and
//! deepen in crevices, edge wear on convex curvature, lichen crusts on exposed faces, moss in
//! cavities and on top, and settled dust. Noise is evaluated in 3D at the baked positions, so there
//! are no seams.

use super::bake::MeshMaps;
use super::mesh::*;

#[derive(Debug, Clone, PartialEq)]
pub struct SurfaceSpec {
    /// 0 .. 1 amount of each layer.
    pub moss: f32,
    pub lichen: f32,
    pub cracks: f32,
    pub wear: f32,
    pub dust: f32,
    /// Pattern frequency (features per metre scale).
    pub scale: f32,
    /// 0 = cool grey stone, 1 = warm brown sandstone.
    pub warmth: f32,
    /// Texture size: 256, 512, 1024 or 2048.
    pub resolution: u32,
    pub seed: u32,
}

impl Default for SurfaceSpec {
    fn default() -> Self {
        SurfaceSpec {
            moss: 0.5,
            lichen: 0.4,
            cracks: 0.5,
            wear: 0.5,
            dust: 0.3,
            scale: 1.0,
            warmth: 0.4,
            resolution: 1024,
            seed: 1,
        }
    }
}

impl SurfaceSpec {
    /// File-name stem for the generated maps.
    pub fn slug(&self) -> String {
        let q = |v: f32| (v * 100.0).round() as i32;
        format!(
            "rock_m{}_l{}_c{}_w{}_d{}_s{}_t{}_r{}_{}",
            q(self.moss),
            q(self.lichen),
            q(self.cracks),
            q(self.wear),
            q(self.dust),
            q(self.scale),
            q(self.warmth),
            self.resolution,
            self.seed
        )
    }
}

/// RGBA8 images, `res` x `res`, top row first.
pub struct SurfaceMaps {
    pub res: usize,
    /// Base colour; alpha 255.
    pub albedo: Vec<u8>,
    /// Tangent-space normal (OpenGL convention, +Y up); alpha holds the height for parallax and
    /// displacement.
    pub normal: Vec<u8>,
    /// Packed occlusion (R), roughness (G), metallic (B).
    pub orm: Vec<u8>,
}

fn smoothstep(a: f32, b: f32, x: f32) -> f32 {
    let t = ((x - a) / (b - a)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

fn contrast(x: f32, k: f32) -> f32 {
    ((x - 0.5) * k + 0.5).clamp(0.0, 1.0)
}

fn blend(a: V3, b: V3, t: f32) -> V3 {
    lerp(a, b, t.clamp(0.0, 1.0))
}

/// Generates the rock texture set for the maps of one part.
pub fn rock(maps: &MeshMaps, spec: &SurfaceSpec) -> SurfaceMaps {
    let res = maps.res;
    let n = res * res;
    let seed = spec.seed.wrapping_mul(7919);
    let freq = spec.scale.max(0.05) * 2.0 / maps.size.max(0.05) * 3.0;
    let stone_cool = [0.46, 0.46, 0.45];
    let stone_dark = [0.29, 0.28, 0.27];
    let stone_warm = [0.58, 0.50, 0.40];
    let warm_dark = [0.36, 0.29, 0.22];
    let moss_a = [0.17, 0.30, 0.07];
    let moss_b = [0.34, 0.46, 0.12];
    let lichen_a = [0.62, 0.66, 0.38];
    let lichen_b = [0.80, 0.78, 0.62];
    let dust_c = [0.58, 0.52, 0.40];

    let mut albedo = vec![[0.0f32; 3]; n];
    let mut rough = vec![0.8f32; n];
    let mut occlusion = vec![1.0f32; n];
    let mut height = vec![0.5f32; n];
    for o in 0..n {
        if !maps.covered[o] {
            continue;
        }
        let p = maps.position[o];
        let nrm = maps.normal[o];
        let q = mul(p, freq);
        let curv = maps.curvature[o];
        let cavity = 1.0 - maps.ao[o];
        let up = nrm[1].max(0.0);

        // Stone: two tones broken up by low-frequency noise, faint strata, fine grain.
        let tone = contrast(fbm3(mul(q, 0.8), seed, 4), 2.4);
        let strata = 0.5 + 0.5 * ((p[1] * freq * 1.3 + fbm3(mul(q, 0.5), seed + 3, 3) * 5.0).sin());
        let base_light = blend(stone_cool, stone_warm, spec.warmth);
        let base_dark = blend(stone_dark, warm_dark, spec.warmth);
        let mut colour = blend(base_dark, base_light, tone * 0.75 + strata * 0.25);
        let grain = fbm3(mul(q, 16.0), seed + 9, 3);
        let speckle = noise3(mul(q, 55.0), seed + 13);
        let brightness = 0.86 + grain * 0.28 + (speckle - 0.5) * 0.18;
        colour = mul(colour, brightness);

        // Height: rolling surface, grain, cracks.
        let ridged = 1.0 - (2.0 * fbm3(mul(q, 3.2), seed + 5, 4) - 1.0).abs();
        let crack_line = smoothstep(0.86, 0.975, ridged) * spec.cracks;
        let crack = (crack_line * (0.55 + cavity)).clamp(0.0, 1.0);
        colour = mul(colour, 1.0 - crack * 0.62);
        let mut h = 0.45 + (fbm3(mul(q, 5.0), seed + 21, 5) - 0.5) * 0.9 + (grain - 0.5) * 0.35;
        h -= crack * 0.55;
        h += curv.max(0.0) * 0.12;

        // Edge wear: convex edges lighten and smooth.
        let wear_noise = fbm3(mul(q, 7.0), seed + 31, 3);
        let wear =
            smoothstep(0.08, 0.55, curv) * spec.wear * smoothstep(0.25, 0.7, wear_noise + 0.25);
        colour = blend(colour, mul(base_light, 1.28), wear * 0.7);

        // Dust settles on upward faces and in shallow hollows.
        let dust = spec.dust
            * up
            * up
            * (0.35 + 0.65 * smoothstep(0.3, 0.7, fbm3(mul(q, 2.5), seed + 41, 3)));
        colour = blend(colour, dust_c, dust * 0.55);

        // Lichen crusts on exposed, mostly upward faces.
        let lich_field = fbm3(mul(q, 4.5), seed + 51, 4);
        let crust = noise3(mul(q, 38.0), seed + 57);
        let lichen = smoothstep(0.58, 0.68, lich_field + (crust - 0.5) * 0.12)
            * spec.lichen
            * (0.35 + 0.65 * up)
            * (1.0 - cavity * 0.6);
        let lichen_colour = blend(lichen_a, lichen_b, crust);
        colour = blend(colour, lichen_colour, lichen * 0.85);
        h += lichen * 0.06 * crust;

        // Moss: cavities, upward faces and moisture patches.
        let moss_field = fbm3(mul(q, 3.0), seed + 61, 4);
        let potential = cavity * 1.25 + up * 0.75 + (moss_field - 0.5) * 1.0 - curv.max(0.0) * 0.8;
        let threshold = 1.2 - spec.moss * 0.95;
        let moss =
            smoothstep(threshold, threshold + 0.22, potential) * smoothstep(0.0, 0.25, spec.moss);
        let tuft = fbm3(mul(q, 24.0), seed + 67, 3);
        let moss_colour = blend(moss_a, moss_b, tuft);
        colour = blend(colour, moss_colour, moss * 0.95);
        h += moss * (0.18 + 0.25 * tuft);

        let mut r = 0.84 + (grain - 0.5) * 0.2;
        r -= wear * 0.22;
        r = r * (1.0 - moss) + 0.96 * moss;
        r = r * (1.0 - lichen * 0.5) + 0.9 * lichen * 0.5;
        r *= 1.0 - cavity * 0.15;

        // Occlusion: the baked AO plus fine cavities from the height.
        let micro = (1.0 - crack * 0.7) * (0.82 + 0.18 * smoothstep(0.2, 0.7, h));
        occlusion[o] = (maps.ao[o].powf(0.9) * micro).clamp(0.0, 1.0);
        albedo[o] = [
            colour[0].clamp(0.0, 1.0),
            colour[1].clamp(0.0, 1.0),
            colour[2].clamp(0.0, 1.0),
        ];
        rough[o] = r.clamp(0.2, 1.0);
        height[o] = h.clamp(0.0, 1.0);
    }

    // Normal map from the height (tangent space, OpenGL convention).
    // Keeps the slope the same at every resolution (the height changes less per texel).
    let strength = res as f32 / 256.0 * 0.6;
    let sample = |x: i32, y: i32| {
        height[y.clamp(0, res as i32 - 1) as usize * res + x.clamp(0, res as i32 - 1) as usize]
    };
    let mut out = SurfaceMaps {
        res,
        albedo: vec![255; n * 4],
        normal: vec![255; n * 4],
        orm: vec![255; n * 4],
    };
    for y in 0..res {
        for x in 0..res {
            let o = y * res + x;
            let (xi, yi) = (x as i32, y as i32);
            let dx = (sample(xi + 1, yi) - sample(xi - 1, yi)) * 0.5 * strength;
            let dy = (sample(xi, yi + 1) - sample(xi, yi - 1)) * 0.5 * strength;
            // Texture rows follow +v here (the engine does not flip V), so green is -dh/drow.
            let nv = normalize([-dx, -dy, 1.0]);
            let enc = |v: f32| ((v * 0.5 + 0.5).clamp(0.0, 1.0) * 255.0).round() as u8;
            out.normal[o * 4..o * 4 + 4].copy_from_slice(&[
                enc(nv[0]),
                enc(nv[1]),
                enc(nv[2]),
                (height[o] * 255.0).round() as u8,
            ]);
            let byte = |v: f32| (v.clamp(0.0, 1.0) * 255.0).round() as u8;
            out.albedo[o * 4..o * 4 + 4].copy_from_slice(&[
                byte(albedo[o][0]),
                byte(albedo[o][1]),
                byte(albedo[o][2]),
                255,
            ]);
            out.orm[o * 4..o * 4 + 4].copy_from_slice(&[
                byte(occlusion[o]),
                byte(rough[o]),
                0,
                255,
            ]);
        }
    }
    out
}
