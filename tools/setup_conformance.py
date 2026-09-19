#!/usr/bin/env python3
"""G5a conformance gate: the core's set-up manipulations vs the emulated original (docs/10-architecture.md §8).

Every script in tests/setup_scripts is run on the original binary (tools/uc_setup_oracle.py under Unicorn)
and on aa_sim (tests/sim_setup_dump); the two `.setup` dumps — object poses, attachment records, rope ends,
the Box2D calls each command made, snap results, collision queries and queued actions, floats as exact
float32 bits — are compared line by line after dropping `#` comments.

Usage: setup_conformance.py --dump build/tests/sim_setup_dump [--assets build/assets] [--scripts tests/setup_scripts]
                            [--out build/setup_conformance] [--so …] [--levels …] [--md report.md]
Exit code 0 when every script is identical, 1 otherwise.
"""
from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from pathlib import Path

log = logging.getLogger("setup_conformance")


def dump_body(path: Path) -> list[str]:
    return [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]


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
    parser.add_argument("--dump", type=Path, required=True, help="path to the sim_setup_dump binary")
    parser.add_argument("--assets", type=Path, default=root / "build/assets")
    parser.add_argument("--scripts", type=Path, default=root / "tests/setup_scripts")
    parser.add_argument("--out", type=Path, default=root / "build/setup_conformance")
    parser.add_argument("--so", type=Path, default=root / "extracted/apk/lib/armeabi-v7a/libamazingalex.so")
    parser.add_argument("--items-plist", type=Path, default=root / "extracted/ios_dec/Common/Game/GameItems.plist")
    parser.add_argument("--levels", type=Path, help="decrypted level plists (default: extracted/ios_dec/Levels, "
                                                       "else extracted/android_dec/Levels)")
    parser.add_argument("--md", type=Path, help="write the report table here")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    if args.levels is None:
        args.levels = next((root / d / "Levels" for d in ("extracted/ios_dec", "extracted/android_dec")
                            if (root / d / "Levels").is_dir()), root / "extracted/ios_dec/Levels")
    if not args.items_plist.exists():
        args.items_plist = root / "extracted/android_dec/Common/Game/GameItems.plist"
    frames = args.assets / "atlases/GameItems.json"
    for p, what in ((args.dump, "sim_setup_dump"), (frames, "imported GameItems.json"), (args.so, "Android binary"),
                    (args.items_plist, "GameItems.plist"), (args.levels, "decrypted levels"), (args.scripts, "scripts")):
        if not p.exists():
            log.error("%s not found: %s", what, p)
            return 1
    scripts = sorted(args.scripts.glob("*.txt"))
    if not scripts:
        log.error("no scripts in %s", args.scripts)
        return 1
    oracle_dir, core_dir = args.out / "oracle", args.out / "core"
    for d in (oracle_dir, core_dir):
        d.mkdir(parents=True, exist_ok=True)

    cmd = [sys.executable, str(root / "tools/uc_setup_oracle.py"), str(args.so), str(args.items_plist),
           "--levels", str(args.levels), "--out", str(oracle_dir)]
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
    for s in scripts:
        core_out = core_dir / f"{s.stem}.setup"
        proc = subprocess.run([str(args.dump), "--frames", str(frames), "--levels", str(args.assets / "levels"),
                               "--script", str(s), str(core_out)], capture_output=True, text=True)
        oracle_out = oracle_dir / f"{s.stem}.setup"
        if proc.returncode != 0:
            rows.append((s.stem, "**error**", proc.stderr.strip() or f"exit {proc.returncode}"))
            failures += 1
            continue
        if not oracle_out.exists():
            rows.append((s.stem, "**error**", "no oracle dump"))
            failures += 1
            continue
        a, b = dump_body(oracle_out), dump_body(core_out)
        diff = first_difference(a, b)
        if diff:
            failures += 1
            rows.append((s.stem, "**differs**", diff))
        else:
            commands = sum(1 for l in a if l.startswith("> "))
            rows.append((s.stem, "identical", f"{commands} commands, {len(a)} lines"))
    lines = ["| script | result | detail |", "|---|:--:|---|"] + [f"| {n} | {r} | {d} |" for n, r, d in rows]
    lines.append("")
    lines.append(f"{len(rows)} scripts: {len(rows) - failures} identical, {failures} differing.")
    text = "\n".join(lines)
    if args.md:
        args.md.write_text(text + "\n", encoding="utf-8")
    sys.stdout.write(text + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
