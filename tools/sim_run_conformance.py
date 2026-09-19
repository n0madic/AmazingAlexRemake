#!/usr/bin/env python3
"""G4 / G6 conformance gate: the core's simulation runs vs the emulated original (docs/10-architecture.md §8).

Every script in tests/sim_scripts (or, with --all-levels, every level of the catalogue for --seconds of
play) is run on the original binary (tools/uc_sim_oracle.py under Unicorn) and on aa_sim
(tests/sim_run_dump); the three dumps are compared:
  .scene   the simulation world's construction (comments dropped) — must be identical
  .traj    the body trajectory per substep (%.9g text) — must be identical; on a mismatch the first
           diverging substep / body is reported through tools/trace_compare.py
  .sim     the per-frame state trace — compared line by line by section: `frame`, `obj`, `lerp`, `item`,
           `goalstate`, `removed` and `action` (--sections selects a subset while a stage is being ported)

Usage: sim_run_conformance.py --dump build/tests/sim_run_dump [--assets build/assets] [--scripts tests/sim_scripts]
                              [--all-levels --seconds 5] [--out build/sim_run_conformance] [--md report.md]
                              [--sections frame,random,obj,lerp,item,goalstate,removed,action]
                              [--known tests/sim_known_divergences.txt]
Exit code 0 when every run is identical (a run listed in --known — `<run name> <reason>` per line — may
differ and is reported as "known"), 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import logging
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace_compare as TC  # noqa: E402

log = logging.getLogger("sim_run_conformance")

ALL_SECTIONS = ("frame", "random", "obj", "lerp", "item", "goalstate", "removed", "action")
FRAME_RATE = 60


def body_lines(path: Path) -> list[str]:
    return [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]


def first_difference(a: list[str], b: list[str]) -> str:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"line {i + 1}: oracle `{x}` vs core `{y}`"
    if len(a) != len(b):
        return f"{len(a)} oracle lines vs {len(b)} core lines"
    return ""


def sim_lines(path: Path, sections: set[str]) -> list[str]:
    return [l for l in body_lines(path) if l.split(" ", 1)[0] in sections]


def traj_report(oracle: Path, core: Path) -> str:
    """Where the trajectories diverge, in trace_compare terms."""
    try:
        r = TC.compare(oracle.stem, TC.load(oracle), TC.load(core), 0.01)
    except (ValueError, KeyError, IndexError) as exc:
        return f"trajectory unreadable: {exc}"
    if r.note:
        return r.note
    step = r.first_exceed[TC.THRESHOLDS[0]]
    return f"first Δ > 1e-6 at substep {step} body {r.first_body}, max Δpos {r.max_pos:.3g}"


def catalogue_scripts(assets: Path, seconds: int, out: Path) -> list[Path]:
    scripts = []
    for chapter in sorted((assets / "levels").iterdir()):
        if not chapter.is_dir():
            continue
        index = json.loads((chapter / "index.json").read_text(encoding="utf-8"))
        for name in index["levels"] + index.get("unlisted", []):
            script = out / "scripts" / f"{chapter.name}_{name}.txt"
            script.parent.mkdir(parents=True, exist_ok=True)
            script.write_text(f"level {chapter.name}/{name}\nframes {seconds * FRAME_RATE}\n", encoding="utf-8")
            scripts.append(script)
    return scripts


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", type=Path, required=True, help="path to the sim_run_dump binary")
    parser.add_argument("--assets", type=Path, default=root / "build/assets")
    parser.add_argument("--scripts", type=Path, default=root / "tests/sim_scripts")
    parser.add_argument("--all-levels", action="store_true", help="every level of the catalogue instead of the scripts")
    parser.add_argument("--seconds", type=int, default=5, help="play time per level with --all-levels")
    parser.add_argument("--out", type=Path, default=root / "build/sim_run_conformance")
    parser.add_argument("--so", type=Path, default=root / "extracted/apk/lib/armeabi-v7a/libamazingalex.so")
    parser.add_argument("--items-plist", type=Path, default=root / "extracted/ios_dec/Common/Game/GameItems.plist")
    parser.add_argument("--sections", default=",".join(ALL_SECTIONS), help="the .sim line kinds to compare")
    parser.add_argument("--md", type=Path, help="write the report table here")
    parser.add_argument("--known", type=Path, help="runs allowed to differ, one `<name> <reason>` per line")
    args = parser.parse_args()
    known: dict[str, str] = {}
    if args.known and args.known.exists():
        for raw in args.known.read_text(encoding="utf-8").splitlines():
            line = raw.split("#", 1)[0].strip()
            if line:
                name, _, reason = line.partition(" ")
                known[name] = reason.strip()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    if not args.items_plist.exists():
        args.items_plist = root / "extracted/android_dec/Common/Game/GameItems.plist"
    frames = args.assets / "atlases/GameItems.json"
    levels = args.assets / "levels"
    for p, what in ((args.dump, "sim_run_dump"), (frames, "imported GameItems.json"), (levels, "imported levels"),
                    (args.so, "Android binary"), (args.items_plist, "GameItems.plist")):
        if not p.exists():
            log.error("%s not found: %s", what, p)
            return 1
    sections = {s.strip() for s in args.sections.split(",") if s.strip()}
    unknown = sections - set(ALL_SECTIONS)
    if unknown:
        log.error("unknown sections: %s", ", ".join(sorted(unknown)))
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    scripts = catalogue_scripts(args.assets, args.seconds, args.out) if args.all_levels else sorted(args.scripts.glob("*.txt"))
    if not scripts:
        log.error("no scripts in %s", args.scripts)
        return 1
    oracle_dir, core_dir = args.out / "oracle", args.out / "core"
    for d in (oracle_dir, core_dir):
        d.mkdir(parents=True, exist_ok=True)

    cmd = [sys.executable, str(root / "tools/uc_sim_oracle.py"), str(args.so), str(args.items_plist),
           "--levels", str(levels), "--out", str(oracle_dir)]
    for s in scripts:
        cmd += ["--script", str(s)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        # The oracle's dumps under <out>/oracle survive between runs: a failed oracle must not be compared
        # against its previous output.
        log.error("oracle failed (%d):\n%s", proc.returncode, proc.stderr.strip())
        return 1

    rows = []
    failures = 0
    for script in scripts:
        name = script.stem
        oracle_scene = oracle_dir / f"{name}.scene"
        core_prefix = core_dir / name
        if not oracle_scene.exists():
            rows.append((name, "oracle failed", ""))
            failures += 1
            continue
        run = subprocess.run([str(args.dump), "--frames", str(frames), "--levels", str(levels), "--script", str(script),
                              str(core_prefix)], capture_output=True, text=True)
        if run.returncode != 0:
            rows.append((name, "core failed", run.stderr.strip().splitlines()[-1] if run.stderr.strip() else ""))
            failures += 1
            continue
        problems = []
        scene_diff = first_difference(body_lines(oracle_scene), body_lines(core_prefix.with_suffix(".scene")))
        if scene_diff:
            problems.append(f"scene: {scene_diff}")
        oracle_traj, core_traj = oracle_dir / f"{name}.uc.traj", core_dir / f"{name}.core.traj"
        if body_lines(oracle_traj) != body_lines(core_traj):
            problems.append(f"traj: {traj_report(oracle_traj, core_traj)}")
        sim_diff = first_difference(sim_lines(oracle_dir / f"{name}.sim", sections), sim_lines(core_prefix.with_suffix(".sim"), sections))
        if sim_diff:
            problems.append(f"sim: {sim_diff}")
        header = (oracle_dir / f"{name}.sim").read_text(encoding="utf-8").splitlines()[0]
        meta = dict(tok.split("=") for tok in header.split() if "=" in tok)
        summary = f"{meta.get('frames', '?')} frames, {meta.get('substeps', '?')} substeps"
        if problems and name in known:
            rows.append((name, "known", summary + "; " + known[name] + "; " + " · ".join(problems)))
        elif problems:
            failures += 1
            rows.append((name, "**differs**", summary + "; " + " · ".join(problems)))
        else:
            rows.append((name, "identical", summary))

    lines = ["| run | result | detail |", "|---|---|---|"]
    lines += [f"| {n} | {r} | {d} |" for n, r, d in rows]
    lines.append("")
    known_rows = sum(1 for r in rows if r[1] == "known")
    lines.append(f"{len(rows) - failures - known_rows} of {len(rows)} runs identical, {known_rows} known divergences "
                 f"(sections: {', '.join(sorted(sections))}).")
    text = "\n".join(lines)
    if args.md:
        args.md.write_text(text + "\n", encoding="utf-8")
    sys.stdout.write(text + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
