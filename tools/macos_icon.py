#!/usr/bin/env python3
"""Make a macOS .icns from a square PNG (tools/build_macos_app.sh).

    tools/macos_icon.py <icon.png> <out.icns>

Resizes the image (Lanczos) into the ten sizes of an .iconset — 16 / 32 / 128 / 256 / 512 px at 1x and
2x, the 1024 px one upscaled when the canonical source is smaller — and
runs `iconutil -c icns` on it, so the result is the multi-resolution icon Finder and the Dock expect.
"""

from __future__ import annotations

import argparse
import logging
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

SIZES = (16, 32, 128, 256, 512)


def write_icns(source: Path, out: Path) -> None:
    with Image.open(source) as image:
        icon = image.convert("RGBA")
        if icon.width != icon.height:
            raise ValueError(f"{source}: the icon must be square, got {icon.width}x{icon.height}")
        with tempfile.TemporaryDirectory() as tmp:
            iconset = Path(tmp) / "AppIcon.iconset"
            iconset.mkdir()
            for size in SIZES:
                for scale in (1, 2):
                    px = size * scale
                    suffix = "" if scale == 1 else "@2x"
                    icon.resize((px, px), Image.LANCZOS).save(iconset / f"icon_{size}x{size}{suffix}.png")
            subprocess.run(["iconutil", "-c", "icns", "-o", str(out), str(iconset)], check=True)
    logging.info("icon: %s (%dx%d) -> %s", source, icon.width, icon.height, out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("icon", type=Path, help="a square PNG (normally imported branding/icon.png)")
    parser.add_argument("out", type=Path, help="the .icns to write")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        write_icns(args.icon, args.out)
    except (OSError, ValueError, subprocess.CalledProcessError) as ex:
        logging.error("macos_icon: %s", ex)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
