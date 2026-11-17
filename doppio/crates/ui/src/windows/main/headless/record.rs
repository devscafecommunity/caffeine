//! Screen recording for scripts: `record_start` / `record_stop` capture frames while the script
//! runs and ask **ffmpeg** to turn them into a GIF, MP4 or WebM.
//!
//! Frames are PNGs in a scratch folder. While recording, a frame is taken after every step that
//! changes something (so a click shows up) and every few frames during `wait` (so animation and
//! real-time previews play). `fps` sets both the capture rate and the playback rate, so a
//! `wait { ms: 2000, real_time: true }` becomes about two seconds of video.

use std::path::{Path, PathBuf};
use std::process::Command;

pub const FORMATS: [&str; 3] = ["gif", "mp4", "webm"];

pub struct Recording {
    pub dir: PathBuf,
    pub name: String,
    pub format: String,
    pub fps: u32,
    pub scale: f32,
    /// Output width in pixels (height keeps the aspect); 0 = as captured.
    pub width: u32,
    pub frames: u32,
    /// Capture one frame every `every` settled frames while waiting.
    pub every: usize,
    pub tick: usize,
    pub crop: Option<super::protocol::Aim>,
    pub padding: f32,
    pub keep_frames: bool,
}

impl Recording {
    pub fn frame_path(&self, index: u32) -> PathBuf {
        self.dir.join(format!("frame_{index:05}.png"))
    }
}

/// How many 16 ms frames pass between captures at `fps`.
pub fn frames_per_capture(fps: u32) -> usize {
    ((62.5 / fps.max(1) as f32).round() as usize).max(1)
}

pub fn ffmpeg_command() -> String {
    std::env::var("FFMPEG").unwrap_or_else(|_| "ffmpeg".to_owned())
}

/// Whether ffmpeg can be run (used for the hint when it cannot).
pub fn ffmpeg_available(program: &str) -> bool {
    Command::new(program)
        .arg("-version")
        .output()
        .is_ok_and(|o| o.status.success())
}

/// The ffmpeg arguments that encode `frame_%05d.png` into `output`.
pub fn encode_args(rec: &Recording, output: &Path) -> Vec<String> {
    let mut args: Vec<String> = ["-y", "-loglevel", "error", "-framerate"]
        .iter()
        .map(|s| (*s).to_owned())
        .collect();
    args.push(rec.fps.to_string());
    args.push("-i".to_owned());
    args.push(rec.dir.join("frame_%05d.png").display().to_string());
    let scale = if rec.width > 0 {
        // Even dimensions keep H.264 and VP9 happy.
        format!("scale={}:-2:flags=lanczos", rec.width & !1)
    } else {
        "scale=trunc(iw/2)*2:trunc(ih/2)*2".to_owned()
    };
    match rec.format.as_str() {
        "gif" => {
            args.push("-vf".to_owned());
            args.push(format!(
                "{scale},split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4"
            ));
            args.push("-loop".to_owned());
            args.push("0".to_owned());
        }
        "webm" => {
            args.extend(
                [
                    "-vf",
                    &scale,
                    "-c:v",
                    "libvpx-vp9",
                    "-crf",
                    "32",
                    "-b:v",
                    "0",
                    "-pix_fmt",
                    "yuv420p",
                ]
                .iter()
                .map(|s| (*s).to_owned()),
            );
        }
        _ => {
            args.extend(
                [
                    "-vf",
                    &scale,
                    "-c:v",
                    "libx264",
                    "-crf",
                    "20",
                    "-pix_fmt",
                    "yuv420p",
                    "-movflags",
                    "+faststart",
                ]
                .iter()
                .map(|s| (*s).to_owned()),
            );
        }
    }
    args.push(output.display().to_string());
    args
}

/// Encodes the captured frames. Returns the output path.
pub fn encode(rec: &Recording, out_dir: &Path) -> Result<PathBuf, String> {
    let program = ffmpeg_command();
    if !ffmpeg_available(&program) {
        return Err(format!(
            "ffmpeg was not found (`{program}`): install it or set the FFMPEG environment variable to its path. The frames are kept in {}",
            rec.dir.display()
        ));
    }
    std::fs::create_dir_all(out_dir).map_err(|e| format!("{}: {e}", out_dir.display()))?;
    let output = out_dir.join(format!("{}.{}", rec.name, rec.format));
    let result = Command::new(&program)
        .args(encode_args(rec, &output))
        .output()
        .map_err(|e| format!("could not run ffmpeg: {e}"))?;
    if !result.status.success() {
        return Err(format!(
            "ffmpeg failed: {}",
            String::from_utf8_lossy(&result.stderr).trim()
        ));
    }
    Ok(output)
}
