//! Screenshot annotation: numbered boxes over widgets plus a legend footer.
//! This is the "annotated panel" format used by docs and agent hand-offs.

use image::{Rgba, RgbaImage};
use std::sync::OnceLock;

const FONT_BYTES: &[u8] = include_bytes!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../../assets/jetbrains-mono/static/jetbrains-mono-latin-400-normal.ttf"
));

const PALETTE: [[u8; 3]; 8] = [
    [0xff, 0x5c, 0x5c],
    [0x4a, 0xc2, 0xff],
    [0x6b, 0xe3, 0x7a],
    [0xff, 0xc1, 0x3d],
    [0xc3, 0x8c, 0xff],
    [0xff, 0x8c, 0xd1],
    [0x3d, 0xf0, 0xd0],
    [0xff, 0x93, 0x4a],
];

fn font() -> &'static fontdue::Font {
    static FONT: OnceLock<fontdue::Font> = OnceLock::new();
    FONT.get_or_init(|| {
        fontdue::Font::from_bytes(FONT_BYTES, fontdue::FontSettings::default())
            .expect("bundled JetBrains Mono should load")
    })
}

fn blend(image: &mut RgbaImage, x: i64, y: i64, color: [u8; 3], alpha: f32) {
    if x < 0 || y < 0 || x >= image.width() as i64 || y >= image.height() as i64 {
        return;
    }
    let pixel = image.get_pixel_mut(x as u32, y as u32);
    for channel in 0..3 {
        let base = pixel[channel] as f32;
        pixel[channel] = (base + (color[channel] as f32 - base) * alpha).round() as u8;
    }
    pixel[3] = 255;
}

/// Outline of a rectangle (physical pixels), `thickness` wide.
pub fn rect_border(image: &mut RgbaImage, rect: [f32; 4], color: [u8; 3], thickness: i64) {
    let (x0, y0) = (rect[0].round() as i64, rect[1].round() as i64);
    let (x1, y1) = (
        (rect[0] + rect[2]).round() as i64,
        (rect[1] + rect[3]).round() as i64,
    );
    for t in 0..thickness {
        for x in x0..=x1 {
            blend(image, x, y0 + t, color, 1.0);
            blend(image, x, y1 - t, color, 1.0);
        }
        for y in y0..=y1 {
            blend(image, x0 + t, y, color, 1.0);
            blend(image, x1 - t, y, color, 1.0);
        }
    }
}

/// Draws `text` with its top-left at (x, y); returns the advance width.
pub fn text(
    image: &mut RgbaImage,
    x: i64,
    y: i64,
    content: &str,
    size: f32,
    color: [u8; 3],
) -> i64 {
    let font = font();
    let ascent = font
        .horizontal_line_metrics(size)
        .map_or(size, |m| m.ascent);
    let mut cursor = x as f32;
    for character in content.chars() {
        let (metrics, bitmap) = font.rasterize(character, size);
        let top = y as f32 + ascent - metrics.height as f32 - metrics.ymin as f32;
        for row in 0..metrics.height {
            for column in 0..metrics.width {
                let alpha = bitmap[row * metrics.width + column] as f32 / 255.0;
                if alpha > 0.0 {
                    blend(
                        image,
                        cursor.round() as i64 + metrics.xmin as i64 + column as i64,
                        top.round() as i64 + row as i64,
                        color,
                        alpha,
                    );
                }
            }
        }
        cursor += metrics.advance_width;
    }
    (cursor - x as f32).round() as i64
}

/// Numbered badge at the top-left corner of `rect`.
fn badge(image: &mut RgbaImage, rect: [f32; 4], number: usize, color: [u8; 3]) {
    let label = number.to_string();
    let size = 15.0;
    let width = (label.len() as i64) * 9 + 8;
    let height = 18;
    let x = (rect[0].round() as i64).max(0);
    let y = (rect[1].round() as i64 - height).max(0);
    for dy in 0..height {
        for dx in 0..width {
            blend(image, x + dx, y + dy, color, 1.0);
        }
    }
    text(image, x + 4, y + 1, &label, size, [0x10, 0x10, 0x10]);
}

fn wrap(text: &str, max_chars: usize) -> Vec<String> {
    let mut lines = Vec::new();
    let mut line = String::new();
    for word in text.split_whitespace() {
        if !line.is_empty() && line.chars().count() + 1 + word.chars().count() > max_chars {
            lines.push(std::mem::take(&mut line));
        }
        if !line.is_empty() {
            line.push(' ');
        }
        line.push_str(word);
    }
    if !line.is_empty() {
        lines.push(line);
    }
    lines
}

/// One annotated widget: its rectangle in the image's pixel space + a label.
pub struct Mark {
    pub rect: [f32; 4],
    pub label: String,
}

/// Draws every mark and appends the legend below the image.
pub fn annotate(image: RgbaImage, marks: &[Mark], caption: Option<&str>) -> RgbaImage {
    let mut image = image;
    for (index, mark) in marks.iter().enumerate() {
        let color = PALETTE[index % PALETTE.len()];
        rect_border(&mut image, mark.rect, color, 2);
        badge(&mut image, mark.rect, index + 1, color);
    }
    let size = 14.0;
    let advance = font().rasterize('M', size).0.advance_width.max(1.0);
    let max_chars = (((image.width() as f32 - 16.0) / advance).floor() as usize).max(10);
    let mut lines: Vec<(String, [u8; 3])> = Vec::new();
    if let Some(caption) = caption {
        for line in wrap(caption, max_chars) {
            lines.push((line, [0xee, 0xee, 0xee]));
        }
    }
    for (index, mark) in marks.iter().enumerate() {
        let color = PALETTE[index % PALETTE.len()];
        for (n, line) in wrap(&format!("{}. {}", index + 1, mark.label), max_chars)
            .into_iter()
            .enumerate()
        {
            lines.push((if n == 0 { line } else { format!("   {line}") }, color));
        }
    }
    if lines.is_empty() {
        return image;
    }
    let line_height = 19;
    let footer = lines.len() as u32 * line_height + 12;
    let mut out = RgbaImage::from_pixel(
        image.width(),
        image.height() + footer,
        Rgba([0x16, 0x16, 0x16, 255]),
    );
    image::imageops::replace(&mut out, &image, 0, 0);
    let top = image.height() as i64 + 6;
    for (i, (line, color)) in lines.iter().enumerate() {
        text(
            &mut out,
            8,
            top + i as i64 * line_height as i64,
            line,
            size,
            *color,
        );
    }
    out
}
