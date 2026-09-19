#!/usr/bin/env python3
"""Resize a launcher icon into the Android mipmap densities (tools/build_android.sh).

    tools/android_icon.py <icon.png> <res-dir>

Writes <res-dir>/mipmap-<density>/ic_launcher.png for mdpi 48 / hdpi 72 / xhdpi 96 / xxhdpi 144 /
xxxhdpi 192 px (Lanczos), from any square image — imported branding/icon.png by default — plus
the adaptive icon of API 26+ (mipmap-anydpi-v26/ic_launcher.xml): the artwork full-bleed as the 108 dp
background layer, a transparent foreground, so launchers mask it to their shape instead of shrinking the
legacy icon inside a circle.
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from PIL import Image

DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
ADAPTIVE_SCALE = 108 / 48   # the adaptive layers are 108 dp for a 48 dp icon
ADAPTIVE_XML = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@mipmap/ic_launcher_bg"/>
    <foreground android:drawable="@android:color/transparent"/>
</adaptive-icon>
"""


def write_icons(source: Path, res_dir: Path) -> None:
    with Image.open(source) as image:
        icon = image.convert("RGBA")
        if icon.width != icon.height:
            side = min(icon.size)
            left = (icon.width - side) // 2
            top = (icon.height - side) // 2
            icon = icon.crop((left, top, left + side, top + side))
        for density, size in DENSITIES.items():
            folder = res_dir / f"mipmap-{density}"
            folder.mkdir(parents=True, exist_ok=True)
            icon.resize((size, size), Image.LANCZOS).save(folder / "ic_launcher.png", "PNG")
            layer = round(size * ADAPTIVE_SCALE)
            icon.resize((layer, layer), Image.LANCZOS).save(folder / "ic_launcher_bg.png", "PNG")
            logging.info("%s: %d px, adaptive layer %d px", folder, size, layer)
        adaptive = res_dir / "mipmap-anydpi-v26"
        adaptive.mkdir(parents=True, exist_ok=True)
        (adaptive / "ic_launcher.xml").write_text(ADAPTIVE_XML, encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path, help="a square PNG / JPEG (normally imported branding/icon.png)")
    parser.add_argument("res_dir", type=Path, help="the resource directory to write mipmap-*/ into")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        write_icons(args.source, args.res_dir)
    except OSError as error:
        logging.error("%s: %s", args.source, error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
