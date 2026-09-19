#!/usr/bin/env python3
"""Compare physics trajectories (tools/uc_trace.py format) pairwise and summarise the divergence.

For every scenario `<name><a-suffix>` / `<name><b-suffix>` in DIR it reports, over all bodies, the first
step at which the position difference exceeds 1e-6 / 1e-4 / 1e-3 / 1e-2 m, the maximum position and angle
difference over the run, the final-state difference and whether the outcome matches (every body within
`--outcome-tol` metres at the last step and the same awake flag).

Usage: trace_compare.py DIR [--a .uc.traj] [--b .native.traj] [--md report.md] [--outcome-tol 0.01]
                        [--expect-identical N]

`--expect-identical N` turns the script into a gate: the exit code is 1 when fewer than N scenarios are
bit-identical (tools/run_physics_regression.sh passes 55, docs/09).
"""
from __future__ import annotations

import argparse
import logging
import math
import sys
from dataclasses import dataclass
from pathlib import Path

log = logging.getLogger("trace_compare")

THRESHOLDS = (1e-6, 1e-4, 1e-3, 1e-2)


@dataclass
class Trajectory:
    bodies: int
    steps: int
    # states[step-1][body] = (x, y, angle, vx, vy, w, awake)
    states: list[list[tuple[float, ...]]]
    items: list[tuple[int, int, str]]   # (first body, last body, "type name") from "# item" comments


def load(path: Path) -> Trajectory:
    with path.open(encoding="utf-8") as fh:
        header = fh.readline()
        meta = dict(tok.split("=") for tok in header.split() if "=" in tok)
        bodies, steps = int(meta["bodies"]), int(meta["steps"])
        states = [[None] * bodies for _ in range(steps)]
        items: list[tuple[int, int, str]] = []
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "#":
                if parts[1] == "item":   # "# item <type> <name> bodies <first>..<last>"
                    first, last = parts[5].split("..")
                    items.append((int(first), int(last), f"{parts[2]} {parts[3]}"))
                continue
            s, b = int(parts[0]), int(parts[1])
            states[s - 1][b] = tuple(float(v) for v in parts[2:9])
    return Trajectory(bodies, steps, states, items)


def wrap_angle(a: float) -> float:
    return (a + math.pi) % (2 * math.pi) - math.pi


@dataclass
class Result:
    name: str
    bodies: int
    steps: int
    first_exceed: dict[float, int | None]
    max_pos: float
    max_angle: float
    final_pos: float
    final_bodies_off: int
    awake_mismatch: int
    outcome_ok: bool
    first_body: str = ""   # body (and item) that first exceeds the smallest threshold
    note: str = ""


def body_label(t: Trajectory, body: int) -> str:
    for first, last, item in t.items:
        if first <= body <= last:
            return f"{body} ({item})"
    return str(body)


def compare(name: str, a: Trajectory, b: Trajectory, outcome_tol: float) -> Result:
    if a.bodies != b.bodies or a.steps != b.steps:
        return Result(name, a.bodies, a.steps, {t: None for t in THRESHOLDS}, math.nan, math.nan, math.nan, 0, 0, False,
                      note=f"shape mismatch: {a.bodies}x{a.steps} vs {b.bodies}x{b.steps}")
    first: dict[float, int | None] = {t: None for t in THRESHOLDS}
    max_pos = max_angle = 0.0
    final_pos = 0.0
    final_off = awake_mismatch = 0
    first_body = ""
    for s in range(a.steps):
        step_max = 0.0
        for body, (sa, sb) in enumerate(zip(a.states[s], b.states[s])):
            if sa is None or sb is None:
                if sa is not sb:
                    return Result(name, a.bodies, a.steps, {t: None for t in THRESHOLDS}, math.nan, math.nan, math.nan,
                                  0, 0, False, "", f"body {body} destroyed on one side only")
                continue
            dp = math.hypot(sa[0] - sb[0], sa[1] - sb[1])
            da = abs(wrap_angle(sa[2] - sb[2]))
            if not first_body and dp > THRESHOLDS[0]:
                first_body = body_label(a, body)
            step_max = max(step_max, dp)
            max_angle = max(max_angle, da)
            if s == a.steps - 1:
                final_pos = max(final_pos, dp)
                final_off += dp > outcome_tol
                awake_mismatch += sa[6] != sb[6]
        max_pos = max(max_pos, step_max)
        for t in THRESHOLDS:
            if first[t] is None and step_max > t:
                first[t] = s + 1
    return Result(name, a.bodies, a.steps, first, max_pos, max_angle, final_pos, final_off, awake_mismatch,
                  final_off == 0 and awake_mismatch == 0, first_body)


def fmt_step(v: int | None) -> str:
    return "—" if v is None else str(v)


def count_identical(results: list[Result]) -> int:
    return sum(1 for r in results if not r.note and r.max_pos == 0.0 and r.max_angle == 0.0)


def to_markdown(results: list[Result], a_suffix: str, b_suffix: str) -> str:
    lines = [f"| scenario | bodies | first step > 1e-6 | first body | > 1e-4 | > 1e-3 | > 1e-2 | max Δpos (m) "
             f"| max Δangle (rad) | final Δpos (m) | bodies off > tol | awake ≠ | outcome |",
             "|---|--:|--:|--|--:|--:|--:|--:|--:|--:|--:|--:|:--:|"]
    for r in results:
        if r.note:
            lines.append(f"| {r.name} | {r.bodies} | {r.note} ||||||||||")
            continue
        f = r.first_exceed
        lines.append(f"| {r.name} | {r.bodies} | {fmt_step(f[1e-6])} | {r.first_body or '—'} | {fmt_step(f[1e-4])} | "
                     f"{fmt_step(f[1e-3])} | {fmt_step(f[1e-2])} | {r.max_pos:.3g} | {r.max_angle:.3g} | "
                     f"{r.final_pos:.3g} | {r.final_bodies_off} | {r.awake_mismatch} | "
                     f"{'same' if r.outcome_ok else '**differs**'} |")
    identical = count_identical(results)
    same = sum(1 for r in results if r.outcome_ok)
    lines.append("")
    lines.append(f"{len(results)} scenarios (`{a_suffix}` vs `{b_suffix}`): {identical} bit-identical, "
                 f"{same} with the same outcome.")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dir")
    parser.add_argument("--a", default=".uc.traj")
    parser.add_argument("--b", default=".native.traj")
    parser.add_argument("--md")
    parser.add_argument("--outcome-tol", type=float, default=0.01)
    parser.add_argument("--expect-identical", type=int, help="exit 1 when fewer scenarios are bit-identical")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    results = []
    for pa in sorted(Path(args.dir).glob(f"*{args.a}")):
        name = pa.name[: -len(args.a)]
        pb = pa.with_name(name + args.b)
        if not pb.exists():
            log.warning("%s: no counterpart %s", name, pb.name)
            continue
        results.append(compare(name, load(pa), load(pb), args.outcome_tol))
    text = to_markdown(results, args.a, args.b)
    if args.md:
        Path(args.md).write_text(text + "\n", encoding="utf-8")
        log.info("wrote %s", args.md)
    sys.stdout.write(text + "\n")
    if args.expect_identical is not None and count_identical(results) < args.expect_identical:
        log.error("%d of %d scenarios bit-identical, expected %d", count_identical(results), len(results),
                  args.expect_identical)
        sys.exit(1)


if __name__ == "__main__":
    main()
