#!/usr/bin/env python3
"""Parse KA3D `.dat` containers (SPRT / COMP / FONT chunks) of the ka-core UI assets to JSON.

Layouts recovered from `game::SpriteSet`, `game::CompoSpriteSet` and `game::BitmapFont` loaders
(see docs/01-asset-formats.md §5). All integers are big-endian; strings are `u16 length + UTF-8`.
`TEXT` bundles are handled by ka3d_text.py.

Usage: ka3d_dat.py <file.dat> [more.dat ...] [-o out.json]
"""
from __future__ import annotations

import argparse
import json
import logging
import struct
import sys
from pathlib import Path

log = logging.getLogger("ka3d_dat")

MAGIC = b"KA3D"


class Reader:
    def __init__(self, buf: bytes, off: int = 0) -> None:
        self.buf = buf
        self.off = off

    def u16(self) -> int:
        (v,) = struct.unpack_from(">H", self.buf, self.off)
        self.off += 2
        return v

    def s16(self) -> int:
        (v,) = struct.unpack_from(">h", self.buf, self.off)
        self.off += 2
        return v

    def u32(self) -> int:
        (v,) = struct.unpack_from(">I", self.buf, self.off)
        self.off += 4
        return v

    def tag(self) -> str:
        v = self.buf[self.off:self.off + 4].decode("ascii")
        self.off += 4
        return v

    def pstr(self) -> str:
        n = self.u16()
        s = self.buf[self.off:self.off + n].decode("utf-8")
        self.off += n
        return s


def parse_sprt(r: Reader) -> dict:
    version = r.u16()
    png = r.pstr()
    count = r.u16()
    sprites = [
        {"name": r.pstr(), "x": r.u16(), "y": r.u16(), "w": r.u16(), "h": r.u16(), "pivotX": r.u16(), "pivotY": r.u16()}
        for _ in range(count)
    ]
    return {"version": version, "texture": png, "sprites": sprites}


def parse_comp(r: Reader) -> dict:
    version = r.u16()
    count = r.u16()
    compos = []
    for _ in range(count):
        name = r.pstr()
        parts = [{"sprite": r.pstr(), "dx": r.s16(), "dy": r.s16()} for _ in range(r.u16())]
        extra = []
        if version == 2:
            # Read and ignored by the loader (CompoSpriteSet ctor); empty in every shipped file.
            extra = [{"name": r.pstr(), "a": r.s16(), "b": r.s16()} for _ in range(r.u16())]
        compos.append({"name": name, "parts": parts, "extra": extra})
    return {"version": version, "compoSprites": compos}


def parse_font(r: Reader) -> dict:
    version = r.u16()
    png = r.pstr()
    leading = r.s16()   # BitmapFont::getLeading  (line height; readShort: signed)
    tracking = r.s16()  # BitmapFont::getTracking (extra advance between glyphs; the outline fonts are negative)
    count = r.u16()
    glyphs = []
    for _ in range(count):
        code = r.u16()
        x, y, w, h, ascent = r.s16(), r.s16(), r.s16(), r.s16(), r.s16()
        # createSprite(name=str(code), x, y, w, h, pivotX=0, pivotY=ascent);
        # maxAscending = max(ascent), maxDescending = max(h - ascent).
        glyphs.append({"char": chr(code), "code": code, "x": x, "y": y, "w": w, "h": h, "ascent": ascent})
    return {"version": version, "texture": png, "leading": leading, "tracking": tracking, "glyphs": glyphs}


PARSERS = {"SPRT": parse_sprt, "COMP": parse_comp, "FONT": parse_font}


def parse_file(path: Path) -> dict:
    return parse_bytes(path.read_bytes(), path.name)


def parse_bytes(buf: bytes, name: str) -> dict:
    r = Reader(buf)
    if r.tag() != MAGIC.decode():
        raise ValueError(f"{name}: not a KA3D container")
    total = r.u32()
    chunks = []
    while r.off < 8 + total:
        tag = r.tag()
        size = r.u32()
        start = r.off
        parser = PARSERS.get(tag)
        if parser is None:
            log.warning("%s: skipping unknown chunk %s (%d bytes)", name, tag, size)
            chunks.append({"tag": tag, "size": size})
        else:
            chunk = parser(Reader(buf, start))
            chunk["tag"] = tag
            chunks.append(chunk)
        r.off = start + size
    return {"file": name, "chunks": chunks}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+")
    parser.add_argument("-o", "--output")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    result = [parse_file(Path(p)) for p in args.paths]
    text = json.dumps(result if len(result) > 1 else result[0], indent=1, ensure_ascii=False)
    if args.output:
        Path(args.output).write_text(text, encoding="utf-8")
        log.info("wrote %s", args.output)
    else:
        sys.stdout.write(text + "\n")


if __name__ == "__main__":
    main()
