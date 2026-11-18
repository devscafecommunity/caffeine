//! Texture layers: the images Texture nodes put on a part, summed into one map per slot.
//!
//! Each layer repeats over the part's UVs (`tiling`), is tinted (albedo only) and blends with what
//! the slot already holds: *Replace*, *Add*, *Multiply* or *Overlay*. Normal maps add their slopes
//! instead, so two normal layers combine into one surface. The result is baked once at placement.

use super::mesh::*;

/// A decoded image, RGBA8.
#[derive(Debug, Clone)]
pub struct Image {
    pub width: usize,
    pub height: usize,
    pub rgba: Vec<u8>,
}

/// The combined maps of a material (`None` = no layer wrote that slot).
#[derive(Debug, Clone)]
pub struct Baked {
    pub size: usize,
    pub albedo: Option<Vec<u8>>,
    pub normal: Option<Vec<u8>>,
    pub orm: Option<Vec<u8>>,
    /// The first albedo layer has real transparency: the material should be cut out.
    pub has_alpha: bool,
}

/// Bilinear, wrapping sample in 0..1 (U and V repeat).
fn sample(image: &Image, u: f32, v: f32) -> [f32; 4] {
    let (w, h) = (image.width as f32, image.height as f32);
    let x = u.rem_euclid(1.0) * w - 0.5;
    let y = v.rem_euclid(1.0) * h - 0.5;
    let (x0, y0) = (x.floor(), y.floor());
    let (fx, fy) = (x - x0, y - y0);
    let at = |xi: i32, yi: i32| {
        let xi = xi.rem_euclid(image.width as i32) as usize;
        let yi = yi.rem_euclid(image.height as i32) as usize;
        let i = (yi * image.width + xi) * 4;
        [
            image.rgba[i] as f32 / 255.0,
            image.rgba[i + 1] as f32 / 255.0,
            image.rgba[i + 2] as f32 / 255.0,
            image.rgba[i + 3] as f32 / 255.0,
        ]
    };
    let (ix, iy) = (x0 as i32, y0 as i32);
    let (a, b, c, d) = (
        at(ix, iy),
        at(ix + 1, iy),
        at(ix, iy + 1),
        at(ix + 1, iy + 1),
    );
    let mut out = [0.0; 4];
    for k in 0..4 {
        let top = a[k] + (b[k] - a[k]) * fx;
        let bottom = c[k] + (d[k] - c[k]) * fx;
        out[k] = top + (bottom - top) * fy;
    }
    out
}

fn blend_channel(dst: f32, src: f32, mode: LayerBlend, strength: f32) -> f32 {
    let s = strength.clamp(0.0, 1.0);
    match mode {
        LayerBlend::Replace => dst + (src - dst) * s,
        LayerBlend::Add => dst + src * strength.max(0.0),
        LayerBlend::Multiply => dst + (dst * src - dst) * s,
        LayerBlend::Overlay => {
            let o = if dst < 0.5 {
                2.0 * dst * src
            } else {
                1.0 - 2.0 * (1.0 - dst) * (1.0 - src)
            };
            dst + (o - dst) * s
        }
    }
}

/// Blends two tangent-space normals (stored 0..1): *Replace* mixes them, the others add the slopes.
fn blend_normal(dst: [f32; 3], src: [f32; 3], mode: LayerBlend, strength: f32) -> [f32; 3] {
    let decode = |c: [f32; 3]| [c[0] * 2.0 - 1.0, c[1] * 2.0 - 1.0, c[2] * 2.0 - 1.0];
    let (a, b) = (decode(dst), decode(src));
    let s = strength.max(0.0);
    let n = match mode {
        LayerBlend::Replace => {
            let t = s.min(1.0);
            [
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t,
            ]
        }
        _ => [
            a[0] + b[0] * s,
            a[1] + b[1] * s,
            a[2] * (1.0 + (b[2] - 1.0) * s.min(1.0)),
        ],
    };
    let len = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt().max(1e-6);
    [
        (n[0] / len) * 0.5 + 0.5,
        (n[1] / len) * 0.5 + 0.5,
        (n[2] / len) * 0.5 + 0.5,
    ]
}

/// Sums the layers of `material` into maps of `size` x `size`. `load` turns a project path into an
/// image (`None` when the file is missing: that layer is skipped).
pub fn bake(material: &CustomMaterial, size: usize, load: &dyn Fn(&str) -> Option<Image>) -> Baked {
    let size = size.max(8);
    let mut out = Baked {
        size,
        albedo: None,
        normal: None,
        orm: None,
        has_alpha: false,
    };
    for slot in TextureSlot::ALL {
        let layers: Vec<(&TextureLayer, Image)> = material
            .layers
            .iter()
            .filter(|l| l.slot == slot && !l.path.is_empty())
            .filter_map(|l| {
                load(&l.path)
                    .filter(|i| i.width > 0 && i.height > 0)
                    .map(|i| (l, i))
            })
            .collect();
        if layers.is_empty() {
            continue;
        }
        let mut pixels = vec![255u8; size * size * 4];
        let mut transparent = false;
        for y in 0..size {
            for x in 0..size {
                let (u, v) = (
                    (x as f32 + 0.5) / size as f32,
                    (y as f32 + 0.5) / size as f32,
                );
                let mut px = match slot {
                    TextureSlot::Albedo => material.rgb,
                    TextureSlot::Normal => [0.5, 0.5, 1.0],
                    TextureSlot::Orm => [1.0, material.roughness, material.metallic],
                };
                let mut alpha = 1.0;
                for (n, (layer, image)) in layers.iter().enumerate() {
                    let s = sample(image, u * layer.tiling[0], v * layer.tiling[1]);
                    let mut src = [s[0], s[1], s[2]];
                    if slot == TextureSlot::Albedo {
                        for (c, t) in src.iter_mut().zip(layer.tint) {
                            *c *= t;
                        }
                        if n == 0 {
                            alpha = s[3];
                        }
                    }
                    if slot == TextureSlot::Normal {
                        px = blend_normal(px, src, layer.blend, layer.strength);
                    } else {
                        for k in 0..3 {
                            px[k] = blend_channel(px[k], src[k], layer.blend, layer.strength);
                        }
                    }
                }
                if alpha < 0.98 {
                    transparent = true;
                }
                let o = (y * size + x) * 4;
                for k in 0..3 {
                    pixels[o + k] = (px[k].clamp(0.0, 1.0) * 255.0).round() as u8;
                }
                pixels[o + 3] = (alpha.clamp(0.0, 1.0) * 255.0).round() as u8;
            }
        }
        match slot {
            TextureSlot::Albedo => {
                out.has_alpha = transparent;
                out.albedo = Some(pixels);
            }
            TextureSlot::Normal => out.normal = Some(pixels),
            TextureSlot::Orm => out.orm = Some(pixels),
        }
    }
    out
}
