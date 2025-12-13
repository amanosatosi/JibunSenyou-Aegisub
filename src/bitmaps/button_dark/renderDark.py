#!/usr/bin/env python3

"""
Generate dark-mode button bitmaps from the edited SVG sources.

Source SVGs:
    docs/art-sources/buttons_dark/*.svg

Output bitmaps (created/overwritten):
    src/bitmaps/button_dark/<name>_16.png
    src/bitmaps/button_dark/<name>_24.png
    src/bitmaps/button_dark/<name>_32.png
    src/bitmaps/button_dark/<name>_48.png
    src/bitmaps/button_dark/<name>_64.png

This mirrors the naming scheme in src/bitmaps/button, so the only
difference is that files live under button_dark instead of button
and use your dark-mode SVG artwork as the source.

Simpler approach:
  - Use Inkscape's command-line exporter to rasterize SVGs.
  - Requirements:
      * Python 3
      * Inkscape installed and available on PATH
"""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
from typing import Optional


SIZES = (16, 24, 32, 48, 64)


def find_repo_root() -> Path:
    """Try to locate the Aegisub repo root from this script location.

    This lets the script live either inside the repo (e.g. tools/)
    or outside it while still targeting the same project tree.
    """
    here = Path(__file__).resolve()

    # Walk up looking for the expected docs/src structure.
    for base in (here.parent, *here.parents):
        svg_dir = base / "docs" / "art-sources" / "buttons_dark"
        bitmaps_dir = base / "src" / "bitmaps"
        if svg_dir.is_dir() and bitmaps_dir.is_dir():
            return base

    # Fallback to the path you've been using on this machine.
    fallback = Path(r"C:\Users\HP USER\jibun aegisub\JibunSenyou-Aegisub")
    if (fallback / "docs" / "art-sources" / "buttons_dark").is_dir():
        return fallback

    # Last resort: just use the script directory.
    return here.parent


def find_inkscape() -> Optional[str]:
    """Locate the Inkscape CLI executable."""
    for candidate in ("inkscape", "inkscape.com"):
        path = shutil.which(candidate)
        if path:
            return path

    # Common Windows install locations.
    candidates = [
        r"C:\Program Files\Inkscape\bin\inkscape.com",
        r"C:\Program Files\Inkscape\bin\inkscape.exe",
        r"C:\Program Files (x86)\Inkscape\bin\inkscape.com",
        r"C:\Program Files (x86)\Inkscape\bin\inkscape.exe",
    ]
    for c in candidates:
        p = Path(c)
        if p.is_file():
            return str(p)

    return None


def run_inkscape(inkscape: str, svg: Path, out: Path, size: int) -> None:
    """Call Inkscape to render one SVG at a given size."""
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        inkscape,
        str(svg),
        "--export-type=png",
        f"--export-filename={out}",
        f"--export-width={size}",
        f"--export-height={size}",
    ]
    subprocess.run(cmd, check=True)


def main() -> int:
    repo_root = find_repo_root()
    svg_dir = repo_root / "docs" / "art-sources" / "buttons_dark"
    out_dir = repo_root / "src" / "bitmaps" / "button_dark"

    if not svg_dir.is_dir():
        sys.stderr.write(f"Source SVG directory not found: {svg_dir}\n")
        return 1

    inkscape = find_inkscape()
    if not inkscape:
        sys.stderr.write(
            "Inkscape not found.\n"
            "Install Inkscape from https://inkscape.org and make sure\n"
            "its 'bin' folder is on your PATH, then re-run this script.\n"
        )
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    svgs = sorted(svg_dir.glob("*.svg"))
    if not svgs:
        sys.stderr.write(f"No SVG files found in {svg_dir}\n")
        return 1

    for svg_path in svgs:
        name = svg_path.stem  # e.g. "about_menu"
        for size in SIZES:
            out_path = out_dir / f"{name}_{size}.png"
            run_inkscape(inkscape, svg_path, out_path, size)
            print(out_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
