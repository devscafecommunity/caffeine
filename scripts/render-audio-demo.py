#!/usr/bin/env python3
"""Renders the volumetric-audio demo videos: top view of each scenario with what the mixer is doing, plus the sound.

  scripts/render-audio-demo.py [scenario ...]        (default: all of doppler shots occlusion layers)

Needs: the `caffeine-audio-demo` binary (cmake --build build --target caffeine-audio-demo), ffmpeg, Pillow and numpy.
The sounds come from references/moodist/public/sounds (mp3), decoded with ffmpeg into build/audio-demo/clips.
Output: build/audio-demo/videos/<scenario>.mp4 (and the .wav the audio was rendered to).
"""
import json
import math
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SOUNDS = ROOT / "references/moodist/public/sounds"
OUT = ROOT / "build/audio-demo"
CLIPS = OUT / "clips"
VIDEOS = OUT / "videos"
EXE = ROOT / "build/caffeine-audio-demo"
W, H, SCENE_H = 960, 540, 400

# name -> (path in moodist, channels, seconds to decode)
CLIP_SOURCES = {
    "ambulance-siren": ("urban/ambulance-siren.mp3", 1, 30),
    "airplane": ("transport/airplane.mp3", 1, 30),
    "fireworks": ("urban/fireworks.mp3", 1, 30),
    "tuning-radio": ("things/tuning-radio.mp3", 1, 30),
    "ceiling-fan": ("things/ceiling-fan.mp3", 1, 30),
    "heavy-rain": ("rain/heavy-rain.mp3", 2, 40),
    "wind": ("nature/wind.mp3", 2, 40),
    "campfire": ("nature/campfire.mp3", 1, 30),
    "thunder": ("rain/thunder.mp3", 1, 30),
}
SCENARIOS = {
    "doppler": ["ambulance-siren", "airplane"],
    "shots": ["fireworks"],
    "occlusion": ["tuning-radio", "ceiling-fan"],
    "layers": ["heavy-rain", "wind", "campfire", "thunder"],
}
FONT_PATHS = ["/usr/share/fonts/TTF/DejaVuSansMono.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"]


def font(size):
    for p in FONT_PATHS:
        if Path(p).exists():
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def decode_clips(names):
    CLIPS.mkdir(parents=True, exist_ok=True)
    for name in names:
        src, channels, seconds = CLIP_SOURCES[name]
        out = CLIPS / f"{name}.wav"
        if out.exists():
            continue
        cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", str(SOUNDS / src), "-t", str(seconds), "-ar", "44100",
               "-ac", str(channels), "-c:a", "pcm_s16le", str(out)]
        subprocess.run(cmd, check=True, timeout=120)


def envelope(wav_path, fps):
    with wave.open(str(wav_path)) as w:
        n, rate = w.getnframes(), w.getframerate()
        data = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0
    data = data.reshape(-1, 2)
    step = rate / fps
    count = int(len(data) / step)
    env = np.zeros((count, 2), dtype=np.float32)
    for i in range(count):
        chunk = data[int(i * step):int((i + 1) * step)]
        env[i] = np.sqrt((chunk ** 2).mean(axis=0) + 1e-12)
    return env


def nice_step(scale):
    raw = scale / 6.0
    p = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 5, 10):
        if m * p >= raw:
            return m * p
    return 10 * p


def draw_frame(data, frame, env, f_small, f_mid, f_big):
    img = Image.new("RGB", (W, H), (17, 19, 25))
    d = ImageDraw.Draw(img, "RGBA")
    v = frame["view"]
    half_w = v["scale"]
    half_h = half_w * (SCENE_H / 2) / (W / 2)

    def to_screen(x, z):
        return (W / 2 + (x - v["cx"]) / half_w * (W / 2), SCENE_H / 2 - (z - v["cz"]) / half_h * (SCENE_H / 2))

    # grid
    step = nice_step(half_w)
    gx = math.floor((v["cx"] - half_w) / step) * step
    while gx < v["cx"] + half_w:
        x, _ = to_screen(gx, 0)
        d.line([(x, 0), (x, SCENE_H)], fill=(34, 38, 48, 255))
        gx += step
    gz = math.floor((v["cz"] - half_h) / step) * step
    while gz < v["cz"] + half_h:
        _, y = to_screen(0, gz)
        d.line([(0, y), (W, y)], fill=(34, 38, 48, 255))
        gz += step
    d.text((6, SCENE_H - 18), f"grid {step:g} m", font=f_small, fill=(110, 118, 135))
    # walls
    for wall in data["walls"]:
        if wall["kind"] != "wall":
            continue
        x0, y0 = to_screen(wall["x0"], wall["z1"])
        x1, y1 = to_screen(wall["x1"], wall["z0"])
        if x1 < -20 or x0 > W + 20 or y1 < -20 or y0 > SCENE_H + 20:
            continue
        d.rectangle([x0, y0, max(x1, x0 + 2), max(y1, y0 + 2)], fill=(105, 112, 128, 255))
    lx, ly = to_screen(frame["listener"]["x"], frame["listener"]["z"])
    yaw = frame["listener"]["yaw"]
    fx, fy = math.sin(yaw), -math.cos(yaw)
    # sources and the lines to them
    for s in frame["sources"]:
        if s["layer"] != 0:
            continue
        sx, sy = to_screen(s["x"], s["z"])
        if s["blocked"] and not s["diffracted"]:
            color = (235, 90, 80, 230)
        elif s["diffracted"]:
            color = (240, 190, 60, 230)
        else:
            color = (90, 210, 130, 230)
        d.line([(lx, ly), (sx, sy)], fill=color, width=2)
        if s["blocked"]:
            ax, ay = lx + s["dx"] * 46, ly - s["dz"] * 46
            d.line([(lx, ly), (ax, ay)], fill=(90, 200, 255, 255), width=3)
            d.ellipse([ax - 4, ay - 4, ax + 4, ay + 4], fill=(90, 200, 255, 255))
        r = 9
        d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=(240, 140, 50, 255), outline=(255, 220, 160, 255))
        label = s["name"]
        d.text((min(max(sx + 12, 4), W - 220), min(max(sy - 8, 2), SCENE_H - 16)), label, font=f_small, fill=(240, 200, 150))
    d.ellipse([lx - 8, ly - 8, lx + 8, ly + 8], fill=(80, 150, 255, 255), outline=(200, 225, 255, 255))
    d.polygon([(lx + fx * 18, ly + fy * 18), (lx - fy * 7 + fx * 6, ly + fx * 7 + fy * 6), (lx + fy * 7 + fx * 6, ly - fx * 7 + fy * 6)],
              fill=(200, 225, 255, 255))
    # HUD
    d.rectangle([0, SCENE_H, W, H], fill=(24, 27, 34, 255))
    d.text((12, SCENE_H + 8), frame["caption"], font=f_big, fill=(240, 240, 245))
    y = SCENE_H + 34
    for s in frame["sources"]:
        tag = "ambient" if s["layer"] == 1 else ("diffracted" if s["diffracted"] else ("BLOCKED" if s["blocked"] else "clear"))
        pitch = f"  pitch x{s['pitch']:.2f}" if s["layer"] == 0 and abs(s["pitch"] - 1.0) > 0.003 else ""
        dist = f"{s['dist']:6.0f} m" if s["layer"] == 0 else "   cover"
        line = f"{s['name'][:26]:26s} {dist}  {s['gain_db']:6.1f} dB  low-pass {s['cutoff'] / 1000:5.1f} kHz  {tag}{pitch}"
        d.text((12, y), line, font=f_small, fill=(205, 210, 220))
        y += 17
    room = frame["room"]
    d.text((12, H - 22), f"room: open {room['open']:.2f}  sky {room['sky']:.2f}  RT60 {room['rt60']:.2f} s  echoes {room['echoes']}   "
                          f"layers: world {frame['layers']['world']:.1f}  ambient {frame['layers']['ambient']:.1f}   t = {frame['t']:5.1f} s",
           font=f_small, fill=(140, 200, 170))
    # level meter: the whole timeline's envelope with a playhead
    top, bottom = H - 62, H - 28
    n = len(env)
    peak = max(float(env.max()), 1e-3)
    for i in range(0, n, max(1, n // (W - 24))):
        x = 12 + i / n * (W - 24)
        h = min(1.0, float(env[i].mean()) / peak) * (bottom - top)
        d.line([(x, bottom), (x, bottom - h)], fill=(70, 100, 140, 255))
    px = 12 + frame["_i"] / n * (W - 24)
    d.line([(px, top), (px, bottom)], fill=(255, 255, 255, 255), width=2)
    return img


def make_video(name):
    wav, js, mp4 = OUT / f"{name}.wav", OUT / f"{name}.json", VIDEOS / f"{name}.mp4"
    data = json.loads(js.read_text())
    env = envelope(wav, data["fps"])
    f_small, f_mid, f_big = font(13), font(15), font(18)
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(data["fps"]),
           "-i", "-", "-i", str(wav), "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", "-preset", "veryfast",
           "-c:a", "aac", "-b:a", "160k", "-shortest", str(mp4)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for i, frame in enumerate(data["frames"]):
        frame["_i"] = min(i, len(env) - 1)
        proc.stdin.write(draw_frame(data, frame, env, f_small, f_mid, f_big).tobytes())
    proc.stdin.close()
    if proc.wait(timeout=300) != 0:
        raise SystemExit(f"ffmpeg failed for {name}")
    print(f"wrote {mp4} ({len(data['frames'])} frames)")


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    if not EXE.exists():
        raise SystemExit(f"{EXE} is missing: cmake --build build --target caffeine-audio-demo")
    OUT.mkdir(parents=True, exist_ok=True)
    VIDEOS.mkdir(parents=True, exist_ok=True)
    for name in names:
        decode_clips(SCENARIOS[name])
        subprocess.run([str(EXE), name, str(CLIPS), str(OUT)], check=True, timeout=300)
        make_video(name)


if __name__ == "__main__":
    main()
