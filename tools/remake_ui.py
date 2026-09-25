"""The remake's own UI sprites (docs/12 §1, docs/06 §4): drawn here at import time into one extra SPRT
container, `ui/<profile>/REMAKE_COMMON.json` + `.png`, next to the imported ones.

`BUTTON_SMALL_MUSIC` / `BUTTON_SMALL_MUSIC_OFF`: the note icon of the music-only switch, in the style of
`BUTTON_SMALL_SOUND` / `_OFF` (white shapes with the dark red outline that thickens to the bottom right like a
drop shadow; for OFF a corner-to-corner slash, outlined thinly along its sides only), sized for
the 2048X1536 sheet and scaled by the profile width for the others. Run directly to preview:

    python3 tools/remake_ui.py <out.png>
"""
from __future__ import annotations

import io
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageDraw, ImageFilter

CONTAINER = "REMAKE_COMMON"
REFERENCE_PROFILE_WIDTH = 2048
SUPERSAMPLE = 4
WHITE = (255, 255, 255, 255)
OUTLINE = (134, 12, 43, 255)   # the MENU_MENU_COMMON icon outline colour

# Icon geometry in 2048X1536 sheet pixels (BUTTON_SMALL_SOUND is 56×85, _OFF 101×88).
NOTE_W, NOTE_H = 64, 85
OFF_W, OFF_H = 101, 88
OUTLINE_PX = 4.0           # the outline on the top / left sides (measured on BUTTON_SMALL_SOUND)
SHADOW_DX, SHADOW_DY = 3.0, 7.0   # the outline's extra thickness to the right / bottom
SLASH_PX = 12.0            # the white band of the slash
SLASH_OUTLINE_PX = 3.0     # the slash's own outline, along its sides only (open ends, no shadow)
SLASH_MARGIN_PX = 0.0      # the slash runs almost corner to corner
HEAD_RX, HEAD_RY = 12.5, 9.5
HEAD_TILT_DEG = -25.0
STEM_PX = 6.0
BEAM_PX = 12.0
BEAM_DROP = 9.0            # the left stem is this much shorter (the beam slopes up to the right)


def _mask(size: tuple[int, int]) -> Image.Image:
    return Image.new("L", size, 0)


def _ellipse_polygon(cx: float, cy: float, rx: float, ry: float, tilt_deg: float, n: int = 48) -> list[tuple[float, float]]:
    t = math.radians(tilt_deg)
    pts = []
    for i in range(n):
        a = 2.0 * math.pi * i / n
        x, y = rx * math.cos(a), ry * math.sin(a)
        pts.append((cx + x * math.cos(t) - y * math.sin(t), cy + x * math.sin(t) + y * math.cos(t)))
    return pts


def _note_mask(scale: float) -> Image.Image:
    """The beamed pair of eighth notes as a white-shape mask, NOTE_W × NOTE_H at `scale` px per sheet px."""
    k = scale * SUPERSAMPLE
    size = (int(round(NOTE_W * k)), int(round(NOTE_H * k)))
    m = _mask(size)
    d = ImageDraw.Draw(m)
    left, top = OUTLINE_PX + 1.0, OUTLINE_PX + 1.0
    right, bottom = OUTLINE_PX + SHADOW_DX + 1.0, OUTLINE_PX + SHADOW_DY + 1.0
    # Heads sit at the bottom corners; the stems rise from their right edges; the beam joins the stem tops.
    left_cx, right_cx = left + HEAD_RX, NOTE_W - right - HEAD_RX
    head_cy = NOTE_H - bottom - HEAD_RY
    for cx, drop in ((left_cx, BEAM_DROP), (right_cx, 0.0)):
        d.polygon([(x * k, y * k) for x, y in _ellipse_polygon(cx, head_cy, HEAD_RX, HEAD_RY, HEAD_TILT_DEG)], fill=255)
        stem_x = cx + HEAD_RX - STEM_PX * 0.5 - 1.0
        d.rectangle([stem_x * k, (top + drop) * k, (stem_x + STEM_PX) * k, head_cy * k], fill=255)
    lx = left_cx + HEAD_RX - STEM_PX - 1.0
    rx = right_cx + HEAD_RX - 1.0
    d.polygon([(lx * k, (top + BEAM_DROP) * k), (rx * k, top * k), (rx * k, (top + BEAM_PX) * k), (lx * k, (top + BEAM_DROP + BEAM_PX) * k)], fill=255)
    return m


def _slash_mask(size: tuple[int, int], scale: float, extra_half_px: float = 0.0) -> Image.Image:
    """The OFF slash: a band from the top-right to the bottom-left corner, as BUTTON_SMALL_SOUND_OFF has, widened
    by `extra_half_px` on each side but never lengthened."""
    k = scale * SUPERSAMPLE
    w, h = size
    m = _mask(size)
    d = ImageDraw.Draw(m)
    half = (SLASH_PX * 0.5 + extra_half_px) * k
    margin = (SLASH_OUTLINE_PX + SLASH_MARGIN_PX) * k
    # The band's centre line runs corner to corner inside the outline margin.
    x0, y0 = w - margin, margin
    x1, y1 = margin, h - margin
    dx, dy = x1 - x0, y1 - y0
    n = math.hypot(dx, dy)
    ox, oy = -dy / n * half, dx / n * half
    d.polygon([(x0 + ox, y0 + oy), (x1 + ox, y1 + oy), (x1 - ox, y1 - oy), (x0 - ox, y0 - oy)], fill=255)
    return m


def _outlined(shape: Image.Image, scale: float, outline_px: float, shadow: tuple[float, float] = (0.0, 0.0)) -> Image.Image:
    """White shape over its dilated dark outline, the outline stretched by `shadow` to the bottom right; still
    supersampled."""
    k = scale * SUPERSAMPLE
    r = int(round(outline_px * k))
    outline = shape.filter(ImageFilter.MaxFilter(2 * r + 1))
    dx, dy = int(round(shadow[0] * k)), int(round(shadow[1] * k))
    if dx or dy:
        shifted = Image.new("L", outline.size, 0)
        shifted.paste(outline, (dx, dy))
        outline = ImageChops.lighter(outline, shifted)
    img = Image.new("RGBA", shape.size, (0, 0, 0, 0))
    img.paste(OUTLINE, mask=outline)
    img.paste(WHITE, mask=shape)
    return img


def _downsample(img: Image.Image) -> Image.Image:
    return img.resize((img.width // SUPERSAMPLE, img.height // SUPERSAMPLE), Image.Resampling.LANCZOS)


def render_icons(scale: float) -> dict[str, Image.Image]:
    note = _note_mask(scale)
    on = _outlined(note, scale, OUTLINE_PX, (SHADOW_DX, SHADOW_DY))
    k = scale * SUPERSAMPLE
    off_size = (int(round(OFF_W * k)), int(round(OFF_H * k)))
    off = Image.new("RGBA", off_size, (0, 0, 0, 0))
    off.alpha_composite(on, ((off_size[0] - on.width) // 2, (off_size[1] - on.height) // 2))
    # The outline runs along the band's sides only: its white ends lie over the button's ring, as the original's do.
    slash = Image.new("RGBA", off_size, (0, 0, 0, 0))
    slash.paste(OUTLINE, mask=_slash_mask(off_size, scale, SLASH_OUTLINE_PX))
    slash.paste(WHITE, mask=_slash_mask(off_size, scale))
    off.alpha_composite(slash)
    return {"BUTTON_SMALL_MUSIC": _downsample(on), "BUTTON_SMALL_MUSIC_OFF": _downsample(off)}


def build(profile_width: int) -> tuple[bytes, dict[str, Any]]:
    """The sheet PNG and the SPRT container JSON (ka3d_dat.py's shape) for a profile of `profile_width` px."""
    scale = profile_width / REFERENCE_PROFILE_WIDTH
    icons = render_icons(scale)
    gap = 4
    width = sum(i.width for i in icons.values()) + gap * (len(icons) + 1)
    height = max(i.height for i in icons.values()) + gap * 2
    sheet = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    sprites = []
    x = gap
    for name, icon in icons.items():
        sheet.alpha_composite(icon, (x, gap))
        sprites.append({"name": name, "x": x, "y": gap, "w": icon.width, "h": icon.height,
                        "pivotX": icon.width // 2, "pivotY": icon.height // 2})
        x += icon.width + gap
    buf = io.BytesIO()
    sheet.save(buf, format="PNG")
    container = {"file": f"{CONTAINER}.dat", "chunks": [{"version": 1, "texture": f"{CONTAINER}.png", "tag": "SPRT", "sprites": sprites}]}
    return buf.getvalue(), container


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    png, container = build(REFERENCE_PROFILE_WIDTH)
    Path(argv[1]).write_bytes(png)
    for s in container["chunks"][0]["sprites"]:
        print(s)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
