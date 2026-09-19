#!/usr/bin/env python3
"""G3 conformance gate: the core's CreatePhysics vs the emulated original (docs/10-architecture.md §8).

For every implemented item type × physics mode × flip state (plus the per-type state variants: Book
colours 1–3, WorldBound backgrounds 1–3) the scene recorded from the original binary (tools/uc_trace.py under
Unicorn) is compared line by line with the scene written by the core (tests/sim_scene_dump). The comparison
is textual after dropping `#` comments and the `step` line: same construction calls, same order, exact
float32 bits. Shipped levels whose item types are all implemented are compared the same way (set-up and
simulation), levels with unimplemented types are reported as skipped.

Usage: sim_conformance.py --dump build/tests/sim_scene_dump [--assets build/assets] [--out build/sim_conformance]
                          [--so extracted/apk/lib/armeabi-v7a/libamazingalex.so] [--levels extracted/ios_dec/Levels]
Exit code 0 when every compared scene is identical, 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import logging
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from uc_dump_physics import ITEM_NAMES  # noqa: E402

log = logging.getLogger("sim_conformance")

MODES = ("simulation", "setup")
BOOK = 15
WORLD_BOUND = 31
BOOK_COLOURS = (1, 2, 3)          # colour 0 is the plain drop
BACKGROUNDS = (1, 2, 3)           # background 0 is the plain drop; 3 = Treehouse floor hole


@dataclass
class Case:
    name: str
    kind: str                      # "drop" | "level"
    item_type: int = 0
    mode: str = "simulation"
    flip: bool = False
    background: int = 0
    state: int | None = None
    level_plist: Path | None = None
    level_json: Path | None = None


def scene_body(path: Path) -> list[str]:
    """The comparable lines of a scene file: no comments, no `step` line."""
    lines = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or line.startswith("step "):
            continue
        lines.append(line)
    return lines


def drop_name(c: Case) -> str:
    return (f"drop_{c.item_type:02d}_{ITEM_NAMES[c.item_type]}_{c.mode}" + ("_flip" if c.flip else "") +
            (f"_bg{c.background}" if c.item_type == WORLD_BOUND and c.background else "") +
            (f"_s{c.state}" if c.state is not None else ""))


def implemented_types(dump: Path) -> list[int]:
    out = subprocess.run([str(dump), "--list-implemented"], check=True, capture_output=True, text=True).stdout
    return [int(line.split()[0]) for line in out.splitlines() if line.strip()]


def build_cases(types: list[int], levels_dir: Path, assets: Path) -> list[Case]:
    cases: list[Case] = []
    for t in types:
        for mode in MODES:
            for flip in (False, True):
                cases.append(Case("", "drop", t, mode, flip))
            if t == BOOK:
                for s in BOOK_COLOURS:
                    for flip in (False, True):
                        cases.append(Case("", "drop", t, mode, flip, state=s))
            if t == WORLD_BOUND:
                for b in BACKGROUNDS:
                    cases.append(Case("", "drop", t, mode, background=b))
    for c in cases:
        c.name = drop_name(c)
    implemented = set(types)
    skipped = []
    for chapter in sorted(p for p in (assets / "levels").iterdir() if p.is_dir()):
        index = json.loads((chapter / "index.json").read_text(encoding="utf-8"))
        for name in index["levels"] + index.get("unlisted", []):
            level_json = chapter / f"{name}.json"
            level = json.loads(level_json.read_text(encoding="utf-8"))
            used = {it["type"] for it in level["items"]}
            if not used <= implemented:
                skipped.append(f"{chapter.name}/{name}")
                continue
            plist = levels_dir / chapter.name / f"{name}.plist"
            if not plist.is_file():
                raise FileNotFoundError(f"decrypted level plist missing for the oracle: {plist}")
            for mode in MODES:
                cases.append(Case(f"level_{chapter.name}_{name}" + ("_setup" if mode == "setup" else ""), "level",
                                  mode=mode, level_plist=plist, level_json=level_json))
    log.info("%d level(s) skipped (unimplemented item types)", len(skipped))
    return cases


def run_checked(cmd: list[str]) -> None:
    """subprocess.run with the child's stderr in the failure message (the oracle's own diagnostics)."""
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        log.error("command failed (%d): %s\n%s", proc.returncode, " ".join(cmd), proc.stderr.strip())
        raise RuntimeError(f"{Path(cmd[1]).name} exited with {proc.returncode}")


def run_oracle(cases: list[Case], so: Path, items_plist: Path, out: Path) -> None:
    """One uc_trace.py run per option combination (the emulator load dominates the cost)."""
    py = sys.executable
    script = Path(__file__).resolve().parent / "uc_trace.py"
    groups: dict[tuple, list[int]] = {}
    for c in cases:
        if c.kind == "drop":
            groups.setdefault((c.mode, c.flip, c.background, c.state), []).append(c.item_type)
    for (mode, flip, background, state), types in groups.items():
        cmd = [py, str(script), str(so), str(items_plist), "--out", str(out), "--drop", ",".join(map(str, types)),
               "--mode", mode, "--steps", "0"]
        if flip:
            cmd.append("--flip")
        if background:
            cmd += ["--background", str(background)]
        if state is not None:
            cmd += ["--state", str(state)]
        run_checked(cmd)
    for mode in MODES:
        plists = [str(c.level_plist) for c in cases if c.kind == "level" and c.mode == mode]
        if plists:
            cmd = [py, str(script), str(so), str(items_plist), "--out", str(out), "--mode", mode, "--steps", "0"]
            for p in plists:
                cmd += ["--level", p]
            run_checked(cmd)


def run_core(c: Case, dump: Path, frames: Path, out: Path) -> tuple[int, str]:
    cmd = [str(dump), "--frames", str(frames), "--mode", c.mode]
    if c.kind == "drop":
        cmd += ["--type", str(c.item_type)]
        if c.flip:
            cmd.append("--flip")
        if c.background:
            cmd += ["--background", str(c.background)]
        if c.state is not None:
            cmd += ["--state", str(c.state)]
    else:
        cmd += ["--level", str(c.level_json)]
    cmd.append(str(out / f"{c.name}.scene"))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.returncode, proc.stderr.strip()


def first_difference(a: list[str], b: list[str]) -> str:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"line {i + 1}: oracle `{x}` vs core `{y}`"
    if len(a) != len(b):
        return f"{len(a)} oracle lines vs {len(b)} core lines"
    return ""


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", type=Path, required=True, help="path to the sim_scene_dump binary")
    parser.add_argument("--assets", type=Path, default=root / "build/assets", help="imported asset tree")
    parser.add_argument("--out", type=Path, default=root / "build/sim_conformance")
    parser.add_argument("--so", type=Path, default=root / "extracted/apk/lib/armeabi-v7a/libamazingalex.so")
    parser.add_argument("--items-plist", type=Path, default=root / "extracted/ios_dec/Common/Game/GameItems.plist")
    parser.add_argument("--levels", type=Path, help="decrypted level plists (default: extracted/ios_dec/Levels, "
                                                       "else extracted/android_dec/Levels — the trees are identical)")
    parser.add_argument("--md", type=Path, help="write the report table here")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    if args.levels is None:
        args.levels = next((root / d / "Levels" for d in ("extracted/ios_dec", "extracted/android_dec")
                            if (root / d / "Levels").is_dir()), root / "extracted/ios_dec/Levels")
    if not args.items_plist.exists():
        args.items_plist = root / "extracted/android_dec/Common/Game/GameItems.plist"
    frames = args.assets / "atlases/GameItems.json"
    for p, what in ((args.dump, "sim_scene_dump"), (frames, "imported GameItems.json"), (args.so, "Android binary"),
                    (args.items_plist, "GameItems.plist"), (args.levels, "decrypted levels")):
        if not p.exists():
            log.error("%s not found: %s", what, p)
            return 1
    oracle_dir, core_dir = args.out / "oracle", args.out / "core"
    for d in (oracle_dir, core_dir):
        d.mkdir(parents=True, exist_ok=True)

    types = implemented_types(args.dump)
    try:
        cases = build_cases(types, args.levels, args.assets)
        log.info("%d implemented types, %d scenes", len(types), len(cases))
        run_oracle(cases, args.so, args.items_plist, oracle_dir)
    except (FileNotFoundError, RuntimeError) as exc:
        log.error("%s", exc)
        return 1

    rows = []
    failures = 0
    for c in cases:
        code, err = run_core(c, args.dump, frames, core_dir)
        oracle = oracle_dir / f"{c.name}.scene"
        if code != 0:
            rows.append((c.name, "**error**", err or f"exit {code}"))
            failures += 1
            continue
        if not oracle.exists():
            rows.append((c.name, "**error**", "no oracle scene"))
            failures += 1
            continue
        a, b = scene_body(oracle), scene_body(core_dir / f"{c.name}.scene")
        diff = first_difference(a, b)
        if diff:
            failures += 1
            rows.append((c.name, "**differs**", diff))
        else:
            rows.append((c.name, "identical", f"{len(a)} lines"))
    lines = ["| scene | result | detail |", "|---|:--:|---|"] + [f"| {n} | {r} | {d} |" for n, r, d in rows]
    lines.append("")
    lines.append(f"{len(rows)} scenes: {len(rows) - failures} identical, {failures} differing.")
    text = "\n".join(lines)
    if args.md:
        args.md.write_text(text + "\n", encoding="utf-8")
    sys.stdout.write(text + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
