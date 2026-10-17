#!/usr/bin/env python3
"""Capture the Doppio window for documentation.

Each entry in caffeine-website/captures/shots.json is its own process:
launch build/doppio with that shot's args, wait until the window is up,
then grab only the active window (not the whole desktop).

On KDE Wayland this uses Spectacle. grim / ImageMagick are fallbacks.

  python3 scripts/capture_doppio_ui.py
  python3 scripts/capture_doppio_ui.py --id doppio-startup
  DOPPIO_BIN=build/doppio python3 scripts/capture_doppio_ui.py --dry-run

Images: caffeine-website/packages/marketing/public/captures/{id}.png
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "caffeine-website" / "captures" / "shots.json"
OUT_DIR = ROOT / "caffeine-website" / "packages" / "marketing" / "public" / "captures"
DEFAULT_BIN = ROOT / "build" / "doppio"
FOCUS_SCRIPT = Path(__file__).resolve().parent / "kwin_focus_doppio.js"


def resolve_binary() -> Path:
    raw = os.environ.get("DOPPIO_BIN", "")
    return Path(raw) if raw else DEFAULT_BIN


def grabber() -> str | None:
    if shutil.which("spectacle"):
        return "spectacle"
    if shutil.which("grim"):
        return "grim"
    if shutil.which("import"):
        return "import"
    return None


def focus_doppio() -> None:
    """Ask KWin to focus the Doppio window. No-op if this is not KDE."""
    if not shutil.which("qdbus6") or not FOCUS_SCRIPT.exists():
        return
    subprocess.run(
        [
            "qdbus6",
            "org.kde.KWin",
            "/Scripting",
            "org.kde.kwin.Scripting.unloadScript",
            "caffeine-doppio-focus",
        ],
        check=False,
        capture_output=True,
    )
    loaded = subprocess.run(
        [
            "qdbus6",
            "org.kde.KWin",
            "/Scripting",
            "org.kde.kwin.Scripting.loadScript",
            str(FOCUS_SCRIPT),
            "caffeine-doppio-focus",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if loaded.returncode != 0:
        print(loaded.stderr.strip() or loaded.stdout.strip(), file=sys.stderr)
    subprocess.run(
        ["qdbus6", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting.start"],
        check=False,
        capture_output=True,
    )
    time.sleep(0.4)


def wait_for_file(dest: Path, timeout: float = 8.0) -> None:
    deadline = time.time() + timeout
    last = -1
    stable = 0
    while time.time() < deadline:
        if dest.exists():
            size = dest.stat().st_size
            if size > 1000 and size == last:
                stable += 1
                if stable >= 2:
                    return
            else:
                stable = 0
            last = size
        time.sleep(0.25)
    raise RuntimeError(f"capture did not finish: {dest}")


def grab(kind: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        dest.unlink()
    focus_doppio()
    if kind == "spectacle":
        subprocess.run(
            ["spectacle", "-b", "-n", "-a", "-o", str(dest)],
            check=True,
        )
        wait_for_file(dest)
    elif kind == "grim":
        subprocess.run(["grim", str(dest)], check=True)
        wait_for_file(dest)
    else:
        subprocess.run(["import", "-window", "root", str(dest)], check=True)
        wait_for_file(dest)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id", help="Capture only this shot id")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    shots = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if args.id:
        shots = [s for s in shots if s["id"] == args.id]
        if not shots:
            print(f"unknown shot id: {args.id}", file=sys.stderr)
            return 1

    binary = resolve_binary()
    kind = grabber()
    if args.dry_run:
        print(f"binary={binary} exists={binary.exists()}")
        print(f"tool={kind or '(none)'}")
        for shot in shots:
            print(f"  {shot['id']} args={shot.get('args', [])}")
        return 0

    if not binary.exists():
        print(f"Doppio binary not found: {binary}", file=sys.stderr)
        print("Build it, or set DOPPIO_BIN.", file=sys.stderr)
        return 1
    if not kind:
        print("Need spectacle, grim, or ImageMagick import.", file=sys.stderr)
        return 1

    failures = 0
    for shot in shots:
        shot_args = list(shot.get("args") or [])
        delay = float(shot.get("delay") or os.environ.get("DOPPIO_CAPTURE_DELAY", "3"))
        dest = OUT_DIR / f"{shot['id']}.png"
        cmd = [str(binary), *shot_args]
        print(f"launch {shot['id']}: {' '.join(cmd)}")
        proc = subprocess.Popen(cmd, cwd=ROOT)
        try:
            time.sleep(delay)
            if proc.poll() is not None:
                print(f"  doppio exited early ({proc.returncode})", file=sys.stderr)
                failures += 1
                continue
            print(f"  grab -> {dest}")
            grab(kind, dest)
            print(f"  ok {dest.stat().st_size} bytes")
        except Exception as exc:
            print(f"  failed: {exc}", file=sys.stderr)
            failures += 1
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
        time.sleep(0.6)

    print(f"done, failures={failures}, dir={OUT_DIR}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
