#!/usr/bin/env python3
"""
Audit Doppio plugins for coupling to editor internals.

Plugins live in the doppio-plugins submodule and only use the public SDK. The
legacy C++ editor is archived, so this scanner reports:

  * internal editor headers (``editor/...``) included by plugins
  * raw ImGui usage in plugin panels (should use caffeine/ui)

Usage:
    python3 scripts/audit_plugin_coupling.py
    python3 scripts/audit_plugin_coupling.py --json report.json
    python3 scripts/audit_plugin_coupling.py --fail-on-violations
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

# Plugin sources live in the doppio-plugins submodule, not in the engine tree.
PLUGIN_SOURCE_DIR = "doppio-plugins/plugins"

# Components live in the engine ECS (src/caffeine/ecs). A plugin *uses* them; it does not
# own them. The audit only treats types declared inside doppio-plugins/plugins as
# plugin-owned, which avoids flagging the engine's own scene data.
ENGINE_ECS_PREFIX = "src/caffeine/ecs"

# A plugin may use these public headers. Anything under ``editor/`` is coupling.
PUBLIC_INCLUDES = (
    "caffeine/plugin/",
    "caffeine/ui/",
    "caffeine/postprocess/",
    "caffeine/animation/",
    "caffeine/effects/",
    "caffeine/navigation/",
    "caffeine/runtime/",
)


@dataclass
class Finding:
    severity: str
    kind: str
    path: str
    line: int
    message: str


@dataclass
class Report:
    findings: list[Finding] = field(default_factory=list)
    scanned_plugins: int = 0

    def add(self, finding: Finding) -> None:
        self.findings.append(finding)

    def by_severity(self, severity: str) -> list[Finding]:
        return [f for f in self.findings if f.severity == severity]


def iter_sources(root: Path, subdir: str) -> Iterable[Path]:
    base = root / subdir
    if not base.exists():
        return
    for path in base.rglob("*"):
        if path.suffix in {".cpp", ".hpp", ".h", ".cc", ".cxx"}:
            yield path


def plugin_owned_components(root: Path) -> set[str]:
    """Types declared in the plugin submodule, e.g. a plugin-local ECS component."""
    names: set[str] = set()
    declaration = re.compile(r"\b(?:struct|class)\s+([A-Za-z_]\w*)\s*(?::[^{;]+)?\{")
    for path in iter_sources(root, PLUGIN_SOURCE_DIR):
        text = path.read_text(errors="replace")
        # Only types declared in the ECS namespace are scene components. Plugin
        # helper structs (Settings, Tri, BVH nodes) are not engine data.
        if "namespace Caffeine::ECS" not in text:
            continue
        for match in declaration.finditer(text):
            names.add(match.group(1))
    return names


def relative(root: Path, path: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def scan_plugins(root: Path, report: Report) -> None:
    for path in iter_sources(root, PLUGIN_SOURCE_DIR):
        report.scanned_plugins += 1
        rel = relative(root, path)
        lines = path.read_text(errors="replace").splitlines()
        for number, line in enumerate(lines, 1):
            include = re.search(r'#\s*include\s*[<"]([^>"]+)[>"]', line)
            if include:
                header = include.group(1)
                if header.startswith("editor/") and not any(
                    header.startswith(public) for public in PUBLIC_INCLUDES
                ):
                    report.add(
                        Finding(
                            "error",
                            "plugin-includes-editor-internal",
                            rel,
                            number,
                            f"plugin includes Doppio internal header '{header}'",
                        )
                    )
                continue

            if "ImGui::" in line:
                report.add(
                    Finding(
                        "info",
                        "plugin-raw-imgui",
                        rel,
                        number,
                        "raw ImGui call — migrate to Caffeine::UIKit widgets",
                    )
                )


def print_report(report: Report) -> None:
    errors = report.by_severity("error")
    warns = report.by_severity("warn")
    infos = report.by_severity("info")

    print("Caffeine plugin coupling audit")
    print(f"  plugin files : {report.scanned_plugins}")
    print(f"  errors       : {len(errors)}")
    print(f"  warnings     : {len(warns)}")
    print(f"  info         : {len(infos)}")
    print()

    for title, items in (("ERROR", errors), ("WARN", warns), ("INFO", infos)):
        if not items:
            continue
        print(f"{title}")
        for finding in items:
            print(f"  {finding.path}:{finding.line}: {finding.message}")
        print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=Path(__file__).resolve().parents[1], type=Path)
    parser.add_argument("--json", type=Path, help="write machine-readable findings")
    parser.add_argument("--fail-on-violations", action="store_true")
    args = parser.parse_args()

    report = Report()
    scan_plugins(args.root, report)

    if args.json:
        args.json.write_text(
            json.dumps(
                {
                    "scanned_plugins": report.scanned_plugins,
                    "findings": [finding.__dict__ for finding in report.findings],
                },
                indent=2,
            )
        )

    print_report(report)

    if args.fail_on_violations and report.by_severity("error"):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
