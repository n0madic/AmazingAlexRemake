#!/usr/bin/env python3
"""Disassembly helpers for porting from libamazingalex.so (docs/10-architecture.md §11 item 7).

The decompile (extracted/decomp_android*/) hides VFP arguments, literal-pool constants and `strh` filter
writes; every port is checked against the objdump listing. Sub-commands:

  fn <name-substring|0xaddr> [--grep RE]   disassemble one exported function (literal pool decoded);
                                           lists the candidates when the substring is ambiguous
  range <0xstart> <0xstop>                 disassemble a virtual-address range
  rd <0xaddr> [count] [f|i|d|b]            read file-backed words (float / int / double / bytes)
  bss <0xaddr> <count> <f|i> [...]         read .bss/.data words after the static initialisers ran (Unicorn)

Addresses are ELF virtual addresses (Ghidra address − 0x10000). `fn` prints the Ghidra address next to the
symbol so the two views line up. Run from the project root with `.venv/bin/python` (the `bss` command needs
Unicorn; the others only need llvm-objdump).
"""
from __future__ import annotations

import argparse
import logging
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

log = logging.getLogger("decomp_dis")

SO_DEFAULT = Path("extracted/apk/lib/armeabi-v7a/libamazingalex.so")
OBJDUMP_CANDIDATES = ("llvm-objdump", "/Library/Developer/CommandLineTools/usr/bin/llvm-objdump")
GHIDRA_BASE = 0x10000


def find_objdump() -> str:
    for cand in OBJDUMP_CANDIDATES:
        path = shutil.which(cand) or (cand if Path(cand).exists() else None)
        if path:
            return path
    raise SystemExit("llvm-objdump not found (install the Xcode command line tools)")


def segments(data: bytes) -> list[tuple[int, int, int, int]]:
    """PT_LOAD segments as (vaddr, file offset, file size, mem size)."""
    (phoff,) = struct.unpack_from("<I", data, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    segs = []
    for i in range(phnum):
        p_type, off, va, _pa, fsz, msz = struct.unpack_from("<IIIIII", data, phoff + i * phentsize)
        if p_type == 1:
            segs.append((va, off, fsz, msz))
    return segs


def vaddr_to_offset(data: bytes, va: int) -> int | None:
    for sva, off, fsz, msz in segments(data):
        if sva <= va < sva + msz:
            return off + (va - sva) if va < sva + fsz else None
    return None


def disassemble_range(so: Path, start: int, stop: int) -> list[str]:
    data = so.read_bytes()
    out = subprocess.run([find_objdump(), "-d", str(so), f"--start-address={start:#x}", f"--stop-address={stop:#x}"],
                         capture_output=True, text=True, check=True).stdout
    lines = []
    for line in out.splitlines():
        m = re.search(r"@ 0x([0-9a-f]+) <", line)
        if m and "vldr" in line:
            # literal-pool load: decode the constant next to the instruction
            off = vaddr_to_offset(data, int(m.group(1), 16))
            if off is not None:
                (w,) = struct.unpack_from("<I", data, off)
                (f,) = struct.unpack("<f", struct.pack("<I", w))
                is_double = line.split()[2].startswith("d") if len(line.split()) > 2 else False
                line = line.split("@")[0] + f"  ; = {w:#010x} f32={f!r}"
                if is_double:
                    (d,) = struct.unpack_from("<d", data, off)
                    line += f" f64={d!r}"
        line = re.sub(r"<_ZN2st18PhysicsObjectUtils13CreatePhysics[^>]*>", "<CreatePhysics>", line)
        lines.append(line)
    return lines


def exported_functions(so: Path) -> list[tuple[int, int, str]]:
    syms = subprocess.run([find_objdump(), "-T", "--demangle", str(so)], capture_output=True, text=True,
                          check=True).stdout
    funcs: dict[int, tuple[int, int, str]] = {}   # one entry per address (weak + strong duplicates)
    for line in syms.splitlines():
        m = re.match(r"([0-9a-f]{8})\s+\S+\s+DF\s+\.text\s+([0-9a-f]{8})\s+(.*)", line)
        if m:
            funcs.setdefault(int(m.group(1), 16), (int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    return list(funcs.values())


def cmd_fn(args: argparse.Namespace) -> int:
    key = args.key
    funcs = exported_functions(args.so)
    if key.startswith("0x"):
        cands = [f for f in funcs if f[0] == int(key, 16)]
    else:
        cands = [f for f in funcs if key in f[2]]
    if len(cands) != 1:
        for addr, size, name in cands:
            print(f"{addr:#x} {size:#x} {name}")
        if not cands:
            log.error("no exported function matches %r", key)
        return 1 if not cands else 0
    addr, size, name = cands[0]
    print(f"; {name} @ {addr:#x} size {size:#x} (ghidra {addr + GHIDRA_BASE:#x})")
    for line in disassemble_range(args.so, addr, addr + size):
        if args.grep is None or re.search(args.grep, line):
            print(line)
    return 0


def cmd_range(args: argparse.Namespace) -> int:
    for line in disassemble_range(args.so, int(args.start, 16), int(args.stop, 16)):
        print(line)
    return 0


def cmd_rd(args: argparse.Namespace) -> int:
    data = args.so.read_bytes()
    addr = int(args.addr, 16)
    off = vaddr_to_offset(data, addr)
    if off is None:
        log.error("%#x is not file-backed (use the bss command)", addr)
        return 1
    for k in range(args.count):
        if args.kind == "f":
            (w,) = struct.unpack_from("<I", data, off + 4 * k)
            (f,) = struct.unpack("<f", struct.pack("<I", w))
            print(f"{addr + 4 * k:#x}: {w:#010x} {f!r}")
        elif args.kind == "i":
            (w,) = struct.unpack_from("<i", data, off + 4 * k)
            print(f"{addr + 4 * k:#x}: {w} ({w & 0xFFFFFFFF:#x})")
        elif args.kind == "d":
            (d,) = struct.unpack_from("<d", data, off + 8 * k)
            print(f"{addr + 8 * k:#x}: {d!r}")
        else:
            print(data[off:off + args.count].hex())
            break
    return 0


def cmd_bss(args: argparse.Namespace) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import uc_harness  # noqa: E402  (needs Unicorn from .venv)

    emu = uc_harness.make_emu(str(args.so))
    words = args.words
    while words:
        addr, n, kind = int(words[0], 16), int(words[1]), words[2]
        words = words[3:]
        for k in range(n):
            w = emu.u32(addr + 4 * k)
            if kind == "f":
                (f,) = struct.unpack("<f", struct.pack("<I", w))
                print(f"{addr + 4 * k:#x}: {w:#010x} {f!r}")
            else:
                print(f"{addr + 4 * k:#x}: {w:#010x} {w}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--so", type=Path, default=SO_DEFAULT)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("fn"); p.add_argument("key"); p.add_argument("--grep"); p.set_defaults(run=cmd_fn)
    p = sub.add_parser("range"); p.add_argument("start"); p.add_argument("stop"); p.set_defaults(run=cmd_range)
    p = sub.add_parser("rd"); p.add_argument("addr"); p.add_argument("count", type=int, nargs="?", default=4)
    p.add_argument("kind", nargs="?", default="f", choices=("f", "i", "d", "b")); p.set_defaults(run=cmd_rd)
    p = sub.add_parser("bss"); p.add_argument("words", nargs="+"); p.set_defaults(run=cmd_bss)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
