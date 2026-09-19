#!/usr/bin/env python3
"""Contact sheet of the viewer's screenshots next to the original level thumbnails (docs/10 §10, M2 exit check).

Usage: render_contact_sheet.py <screenshots-dir> <thumbnails-dir> <out.png> [--columns N] [--cell W]
                               [--only NAME[,NAME...]]

Every `<chapter>_<name>.png` written by `amazing_alex --screenshots` is paired with
`<thumbnails-dir>/<name>_350.jpg` when it exists (the 4 unlisted Treehouse levels have no thumbnail). The
thumbnails are 350×350 crops of the original's own set-up render, so the sheet confirms sprites, colours, flips
and relative placement, not the whole layout. Needs Pillow.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from PIL import Image, ImageDraw

log = logging.getLogger("render_contact_sheet")

CAPTION_HEIGHT = 18
PADDING = 6


def chapter_and_name(shot: Path) -> tuple[str, str]:
    """`00_Classroom_Playtime` → ("00_Classroom", "Playtime"): the chapter keeps its numeric prefix."""
    parts = shot.stem.split("_", 2)
    return f"{parts[0]}_{parts[1]}", parts[2]


def level_name(shot: Path) -> str:
    return chapter_and_name(shot)[1]


def load_font():
    from PIL import ImageFont

    try:
        return ImageFont.truetype("DejaVuSans.ttf", 12)
    except OSError:
        return ImageFont.load_default()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("screenshots", type=Path)
    parser.add_argument("thumbnails", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--columns", type=int, default=4, help="level pairs per row (default 4)")
    parser.add_argument("--cell", type=int, default=320, help="width of one screenshot cell in px (default 320)")
    parser.add_argument("--only", help="comma-separated level names to include")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    shots = sorted(args.screenshots.glob("*.png"))
    if args.only:
        wanted = set(args.only.split(","))
        shots = [s for s in shots if level_name(s) in wanted]
    if not shots:
        log.error("no screenshots in %s", args.screenshots)
        return 1
    cell_w = args.cell
    shot_h = cell_w * 3 // 4
    thumb_w = shot_h                     # square thumbnail scaled to the screenshot height
    pair_w = cell_w + thumb_w + PADDING
    pair_h = shot_h + CAPTION_HEIGHT
    rows = (len(shots) + args.columns - 1) // args.columns
    sheet = Image.new("RGB", (args.columns * (pair_w + PADDING) + PADDING, rows * (pair_h + PADDING) + PADDING), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    font = load_font()
    missing = 0
    for i, shot_path in enumerate(shots):
        col, row = i % args.columns, i // args.columns
        x = PADDING + col * (pair_w + PADDING)
        y = PADDING + row * (pair_h + PADDING)
        with Image.open(shot_path) as shot:
            sheet.paste(shot.convert("RGB").resize((cell_w, shot_h), Image.LANCZOS), (x, y + CAPTION_HEIGHT))
        chapter, name = chapter_and_name(shot_path)
        thumb_path = args.thumbnails / f"{name}_350.jpg"
        if thumb_path.exists():
            with Image.open(thumb_path) as thumb:
                sheet.paste(thumb.convert("RGB").resize((thumb_w, shot_h), Image.LANCZOS), (x + cell_w + PADDING, y + CAPTION_HEIGHT))
        else:
            missing += 1
            draw.rectangle([x + cell_w + PADDING, y + CAPTION_HEIGHT, x + pair_w, y + pair_h], outline=(90, 90, 90))
            draw.text((x + cell_w + PADDING + 8, y + CAPTION_HEIGHT + 8), "no thumbnail", fill=(160, 160, 160), font=font)
        draw.text((x, y + 2), f"{chapter}/{name}", fill=(230, 230, 230), font=font)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out)
    log.info("wrote %s: %d levels, %d without thumbnail", args.out, len(shots), missing)
    return 0


if __name__ == "__main__":
    sys.exit(main())
