#!/usr/bin/env python3
"""Create a multi-resolution Windows ICO from imported branding/icon.png."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

SIZES = (16, 24, 32, 48, 64, 128, 256)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="square source image")
    parser.add_argument("output", type=Path, help="ICO file to create")
    args = parser.parse_args(argv)
    try:
        with Image.open(args.source) as image:
            icon = image.convert("RGBA")
            if icon.width != icon.height:
                raise ValueError(f"icon must be square, got {icon.width}x{icon.height}")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            icon.save(args.output, format="ICO", sizes=[(size, size) for size in SIZES])
    except (OSError, ValueError) as error:
        print(f"{args.source}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
