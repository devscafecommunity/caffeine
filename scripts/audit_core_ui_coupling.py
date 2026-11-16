#!/usr/bin/env python3
"""Report ImGui/ImNodes coupling in the core tree.

This is a migration aid for the Rust/Iced frontend. It does not modify files.
Use --fail-on-leaks in CI once the core/editor split is complete.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE_DIRS = (ROOT / "src" / "caffeine", ROOT / "include" / "caffeine")
PATTERNS = ("imgui.h", "ImGui::", "ImNodes::", "CF_HAS_IMGUI")
SKIP_PARTS = {"build", "target", "vendor", ".git"}


def category_for(path: Path, text: str) -> str:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith("include/caffeine/ui/"):
        return "UI kit"
    if "EditorUI" in path.name or "EditorUi" in path.name:
        return "Editor UI"
    if "#ifdef CF_HAS_IMGUI" in text or "#if defined(CF_HAS_IMGUI)" in text:
        return "Conditional core"
    return "Leak"


def scan() -> list[tuple[str, str]]:
    findings: list[tuple[str, str]] = []
    for base in CORE_DIRS:
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix not in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
                continue
            if any(part in SKIP_PARTS for part in path.parts):
                continue
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            if not any(pattern in text for pattern in PATTERNS):
                continue
            findings.append((category_for(path, text), path.relative_to(ROOT).as_posix()))
    return sorted(findings)


def cmake_findings() -> tuple[list[str], list[str]]:
    cmake = (ROOT / "CMakeLists.txt").read_text(errors="replace")
    core: list[str] = []
    frontends: list[str] = []
    if re.search(r"target_link_libraries\(\s*caffeine-core\s+PUBLIC[^)]*ImGui", cmake,
                 re.DOTALL):
        core.append("CMake: caffeine-core links ImGui/ImNodes PUBLIC")
    if re.search(r"target_compile_definitions\(\s*caffeine-core\s+PUBLIC[^)]*CF_HAS_IMGUI",
                 cmake, re.DOTALL):
        core.append("CMake: caffeine-core publishes CF_HAS_IMGUI=1")
    if re.search(r"target_link_libraries\(\s*caffeine-runtime\s+PRIVATE[^)]*ImGui", cmake,
                 re.DOTALL):
        frontends.append("frontend: caffeine-runtime still links ImGui")
    return core, frontends


def library_imgui_symbols() -> int | None:
    library = ROOT / "build" / "libcaffeine-core.a"
    if not library.exists():
        return None
    try:
        result = subprocess.run(
            ["nm", "-C", str(library)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    return len(re.findall(r"ImGui::|ImNodes::", result.stdout))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fail-on-leaks", action="store_true",
                        help="exit 1 when core leaks or the CMake coupling remains")
    args = parser.parse_args()

    findings = scan()
    counts: dict[str, int] = {}
    for category, _ in findings:
        counts[category] = counts.get(category, 0) + 1

    print("Caffeine core/ImGui coupling audit")
    print(f"  core files with ImGui refs : {len(findings)}")
    for category in ("UI kit", "Editor UI", "Conditional core", "Leak"):
        if counts.get(category):
            print(f"  {category:<26}: {counts[category]}")
    for category, rel in findings:
        if category in {"Leak", "Editor UI"}:
            print(f"    [{category}] {rel}")

    core_build, frontend_build = cmake_findings()
    if core_build:
        print("  core build coupling:")
        for line in core_build:
            print(f"    {line}")
    if frontend_build:
        print("  frontend dependencies (not core):")
        for line in frontend_build:
            print(f"    {line}")

    symbols = library_imgui_symbols()
    if symbols is not None:
        print(f"  caffeine-core ImGui/ImNodes symbols: {symbols}")

    leaks = counts.get("Leak", 0)
    failed = leaks > 0 or bool(core_build) or bool(symbols)
    if args.fail_on_leaks and failed:
        print("FAIL: caffeine-core still depends on ImGui")
        return 1
    if failed:
        print("FAIL: caffeine-core still depends on ImGui (report only)")
        return 0
    print("OK: caffeine-core is ImGui-free")
    return 0


if __name__ == "__main__":
    sys.exit(main())
