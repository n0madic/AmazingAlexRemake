#!/usr/bin/env python3
"""Parse KA3D 'TEXT' localisation bundles (TEXTS_BASIC.dat etc.) into JSON.

Container layout (all integers big-endian):
  "KA3D" u32 size            -- size of everything after these 8 bytes
  "TEXT" u32 size u16 version
    chunks: tag[4] u32 payload_size payload
      LDAT: u16 count, count x (u16 len, utf-8 locale code)
      LIDS: u16 count, count x (u16 len, string id)
      TXGP: one per locale, count x (u16 len, utf-8 text) in LIDS order
"""
from __future__ import annotations

import argparse
import json
import logging
import struct
import sys
from pathlib import Path

log = logging.getLogger("ka3d_text")


def read_pstr(buf: bytes, off: int) -> tuple[str, int]:
    (n,) = struct.unpack_from(">H", buf, off)
    return buf[off + 2 : off + 2 + n].decode("utf-8", "replace"), off + 2 + n


def read_str_list(buf: bytes, count: int | None = None) -> list[str]:
    off = 0
    if count is None:
        (count,) = struct.unpack_from(">H", buf, 0)
        off = 2
    out = []
    for _ in range(count):
        s, off = read_pstr(buf, off)
        out.append(s)
    return out


def parse(data: bytes) -> dict[str, dict[str, str]]:
    if data[:4] != b"KA3D" or data[8:12] != b"TEXT":
        raise ValueError("not a KA3D TEXT bundle")
    off = 0x12  # after KA3D hdr, TEXT hdr and u16 version
    locales: list[str] = []
    ids: list[str] = []
    groups: list[list[str]] = []
    while off < len(data):
        tag = data[off : off + 4]
        (size,) = struct.unpack_from(">I", data, off + 4)
        payload = data[off + 8 : off + 8 + size]
        if tag == b"LDAT":
            locales = read_str_list(payload)
        elif tag == b"LIDS":
            ids = read_str_list(payload)
        elif tag == b"TXGP":
            groups.append(read_str_list(payload, len(ids)))
        else:
            log.warning("unknown chunk %r at %d", tag, off)
        off += 8 + size
    if len(groups) != len(locales):
        raise ValueError(f"{len(groups)} text groups for {len(locales)} locales")
    return {loc: dict(zip(ids, texts)) for loc, texts in zip(locales, groups)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dat", type=Path)
    ap.add_argument("-o", "--out", type=Path, help="write JSON here (default stdout)")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    table = parse(args.dat.read_bytes())
    text = json.dumps(table, ensure_ascii=False, indent=1)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
        log.info("wrote %s (%d locales, %d ids)", args.out, len(table), len(next(iter(table.values()))))
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
