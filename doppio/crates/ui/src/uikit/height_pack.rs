//! Packing an external height map into a normal map's alpha channel.
//!
//! The renderer's parallax occlusion mapping reads depth from the normal map's
//! alpha (1 = surface, 0 = deepest), so artists with a separate height image
//! need it merged: resample the height to the normal's size, take its
//! luminance and write it into alpha. Pure functions so they can be tested
//! without an engine.

/// Rec.709 luma of one RGBA pixel, as 0..=255.
pub fn luma(pixel: &[u8]) -> u8 {
    let value = 0.2126 * pixel[0] as f32 + 0.7152 * pixel[1] as f32 + 0.0722 * pixel[2] as f32;
    value.round().clamp(0.0, 255.0) as u8
}

/// Bilinear resample of the luminance of an RGBA image to `dw × dh`.
pub fn resample_luma(src: &[u8], sw: usize, sh: usize, dw: usize, dh: usize) -> Vec<u8> {
    if sw == 0 || sh == 0 || dw == 0 || dh == 0 || src.len() < sw * sh * 4 {
        return vec![255; dw * dh];
    }
    let lumas: Vec<f32> = src
        .as_chunks::<4>()
        .0
        .iter()
        .take(sw * sh)
        .map(|p| luma(p) as f32)
        .collect();
    let at = |x: usize, y: usize| lumas[y.min(sh - 1) * sw + x.min(sw - 1)];
    let mut out = Vec::with_capacity(dw * dh);
    for y in 0..dh {
        let fy = ((y as f32 + 0.5) * sh as f32 / dh as f32 - 0.5).max(0.0);
        let y0 = fy.floor() as usize;
        let ty = fy - y0 as f32;
        for x in 0..dw {
            let fx = ((x as f32 + 0.5) * sw as f32 / dw as f32 - 0.5).max(0.0);
            let x0 = fx.floor() as usize;
            let tx = fx - x0 as f32;
            let top = at(x0, y0) * (1.0 - tx) + at(x0 + 1, y0) * tx;
            let bottom = at(x0, y0 + 1) * (1.0 - tx) + at(x0 + 1, y0 + 1) * tx;
            out.push((top * (1.0 - ty) + bottom * ty).round().clamp(0.0, 255.0) as u8);
        }
    }
    out
}

/// Writes `height` (one byte per pixel) into the alpha of an RGBA normal map.
/// Returns false when the sizes do not match.
pub fn write_alpha(normal_rgba: &mut [u8], height: &[u8]) -> bool {
    if normal_rgba.len() != height.len() * 4 {
        return false;
    }
    for (pixel, value) in normal_rgba.as_chunks_mut::<4>().0.iter_mut().zip(height) {
        pixel[3] = *value;
    }
    true
}

/// Merges `height_rgba` (any size) into `normal_rgba` (`nw × nh`) in place.
pub fn pack(
    normal_rgba: &mut [u8],
    nw: usize,
    nh: usize,
    height_rgba: &[u8],
    hw: usize,
    hh: usize,
) -> bool {
    let height = if hw == nw && hh == nh {
        height_rgba
            .as_chunks::<4>()
            .0
            .iter()
            .map(|p| luma(p))
            .collect()
    } else {
        resample_luma(height_rgba, hw, hh, nw, nh)
    };
    write_alpha(normal_rgba, &height)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn luma_weights_green_most() {
        assert_eq!(luma(&[255, 255, 255, 255]), 255);
        assert_eq!(luma(&[0, 0, 0, 255]), 0);
        assert!(luma(&[0, 255, 0, 255]) > luma(&[255, 0, 0, 255]));
    }

    #[test]
    fn resampling_keeps_gradients_and_size() {
        // 2×1 black→white stretched to 8×2.
        let src = [0, 0, 0, 255, 255, 255, 255, 255];
        let out = resample_luma(&src, 2, 1, 8, 2);
        assert_eq!(out.len(), 16);
        assert!(out[0] < 40 && out[7] > 215);
        assert!(
            out.windows(2).take(7).all(|w| w[0] <= w[1]),
            "monotonic ramp"
        );
    }

    #[test]
    fn packing_replaces_only_alpha_and_checks_sizes() {
        let mut normal = vec![128, 128, 255, 255, 10, 20, 30, 255];
        let height = [200, 200, 200, 255, 50, 50, 50, 255];
        assert!(pack(&mut normal, 2, 1, &height, 2, 1));
        assert_eq!(&normal[..3], &[128, 128, 255]);
        assert_eq!(normal[3], 200);
        assert_eq!(normal[7], 50);
        // A height of another size is resampled to the normal's size.
        let mut bigger = vec![0u8; 4 * 4 * 4];
        assert!(pack(&mut bigger, 4, 4, &height, 2, 1));
        assert!(bigger.as_chunks::<4>().0.iter().all(|p| p[3] > 0));
        assert!(!write_alpha(&mut normal, &[1, 2, 3]));
    }
}
