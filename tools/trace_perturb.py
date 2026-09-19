#!/usr/bin/env python3
"""Nudge one body of a scene file by one float32 ulp (chaos-floor calibration for the physics harness).

Replaying the nudged scene with `trace_native` and comparing it against the un-nudged replay shows how
fast the scenario amplifies a rounding-level difference; original-vs-2.2.1 divergences below that curve
are noise, not solver differences (docs/09-physics-regression.md).

Usage: trace_perturb.py in.scene out.scene [--body N] (default: the first dynamic body, y coordinate)
"""
from __future__ import annotations

import argparse
import logging
import struct
from pathlib import Path

log = logging.getLogger("trace_perturb")


def nudge(bits_text: str) -> str:
    bits = int(bits_text, 16)
    (value,) = struct.unpack("<f", struct.pack("<I", bits))
    nudged = bits + (1 if value >= 0 else -1)   # one ulp away from zero
    return "0x%08x" % nudged


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("src")
    parser.add_argument("dst")
    parser.add_argument("--body", type=int, help="body index to nudge (default: first dynamic body)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    lines = Path(args.src).read_text(encoding="utf-8").splitlines()
    destroyed = {int(line.split()[1]) for line in lines if line.startswith("destroybody ")}
    body_lines = [(i, line.split()) for i, line in enumerate(lines)
                  if line.startswith("body ") and int(line.split()[1]) not in destroyed]
    if args.body is not None:
        chosen = [(i, tok) for i, tok in body_lines if int(tok[1]) == args.body]
    else:
        chosen = [(i, tok) for i, tok in body_lines if int(tok[2]) == 2] or body_lines[:1]   # static-only scene: nudge body 0
    if not chosen:
        raise SystemExit("no matching body found")
    i, tok = chosen[0]
    tok[4] = nudge(tok[4])   # y of the initial position
    lines[i] = " ".join(tok)
    log.info("nudged body %s y by one ulp", tok[1])
    Path(args.dst).write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
