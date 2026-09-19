#!/usr/bin/env python3
"""Dump the st::AudioId -> clip filename table from the iOS binary (Mach-O armv7).

`st::AudioFilenames` is an array of `const char*` in `__DATA,__data`; the index is the
`st::AudioId::Enum` value passed to `SoundSystemUtils::Play(id, volume, pos, audio)` and
`BackgroundMusicUtils::Play`. The Android .so keeps the same table, but its pointers are
only materialised by the dynamic linker, so the iOS binary is used instead.
Usage: dump_audio_ids.py "<Amazing Alex HD binary>" [-o table.md]
"""
from __future__ import annotations

import argparse
import logging
import re
import struct
import subprocess
import sys

TABLE_SYMBOL = "st::AudioFilenames"
CLIP_NAME = re.compile(r"[A-Za-z0-9_.]*")


def load_sections(bin_path: str) -> list[tuple[int, int, int]]:
    """Return (vmaddr, file offset, size) for every section listed by otool -l."""
    out = subprocess.run(["otool", "-l", bin_path], capture_output=True, text=True, check=True).stdout
    sections: list[tuple[int, int, int]] = []
    cur: dict[str, int] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        key, value = parts
        if key == "sectname":
            cur = {}
        elif key in ("addr", "size", "offset") and cur is not None:
            cur[key] = int(value, 0)
            if len(cur) == 3:
                sections.append((cur["addr"], cur["offset"], cur["size"]))
                cur = {}
    return sections


def vaddr_to_offset(sections: list[tuple[int, int, int]], addr: int) -> int:
    for vmaddr, off, size in sections:
        if vmaddr <= addr < vmaddr + size:
            return addr - vmaddr + off
    raise ValueError(f"address {addr:#x} is not file-backed")


def symbol_address(bin_path: str, name: str) -> int:
    out = subprocess.run(["nm", "-C", bin_path], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise KeyError(f"symbol {name} not found in {bin_path}")


def read_cstring(data: bytes, off: int) -> str:
    end = data.index(b"\0", off)
    return data[off:end].decode("ascii")


def dump_table(bin_path: str) -> list[str]:
    with open(bin_path, "rb") as fh:
        data = fh.read()
    sections = load_sections(bin_path)
    base = vaddr_to_offset(sections, symbol_address(bin_path, TABLE_SYMBOL))
    names: list[str] = []
    while True:
        ptr, = struct.unpack_from("<I", data, base + len(names) * 4)
        try:
            name = read_cstring(data, vaddr_to_offset(sections, ptr))
        except ValueError:
            break
        # Entry 0 is the empty "no sound" id; clip names are `Name.mp3`-style identifiers.
        if (not name and names) or not CLIP_NAME.fullmatch(name):
            break
        names.append(name)
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin_path")
    parser.add_argument("-o", "--output", help="write a markdown table here instead of stdout")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    names = dump_table(args.bin_path)
    logging.info("read %d audio ids", len(names))
    lines = ["| id | clip |", "|----|------|"] + [f"| {i} (`0x{i:x}`) | `{n}` |" for i, n in enumerate(names)]
    text = "\n".join(lines) + "\n"
    if args.output:
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
