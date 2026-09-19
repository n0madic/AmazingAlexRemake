#!/usr/bin/env python3
"""Data-mine decrypted Amazing Alex level plists.

Prints distinct values of enum-like fields across all levels, checks the
handle/type relationship, and lists per-level summaries.
"""
from __future__ import annotations

import argparse
import logging
import plistlib
import sys
from collections import Counter, defaultdict
from pathlib import Path

log = logging.getLogger("dump_levels")

ITEM_FIELDS = ("type", "flags", "itemData", "attachmentCount")
LEVEL_FIELDS = ("version", "backgroundIndex", "toolboxSlotCount", "rewardId", "tested", "sharedPublicly", "authorName")


def load_levels(root: Path) -> dict[str, dict]:
    levels = {}
    for chapter in sorted(root.iterdir()):
        if not chapter.is_dir():
            continue
        for path in sorted(chapter.glob("*.plist")):
            if path.name == "0_Location.plist":
                continue
            with path.open("rb") as fh:
                levels[f"{chapter.name}/{path.stem}"] = plistlib.load(fh)
    return levels


def items_of(level: dict) -> list[dict]:
    return [level[f"itemInfos_{i}"] for i in range(level["itemCount"])]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("levels_dir", type=Path)
    ap.add_argument("--show", help="print full plist of one level (chapter/name)")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    levels = load_levels(args.levels_dir)
    if args.show:
        print(plistlib.dumps(levels[args.show], fmt=plistlib.FMT_XML).decode())
        return 0

    level_vals = {f: Counter() for f in LEVEL_FIELDS}
    item_vals = {f: Counter() for f in ITEM_FIELDS}
    goal_types = Counter()
    goal_keys = Counter()
    level_keys = Counter()
    item_keys = Counter()
    handle_mismatch = []
    type_by_flag = defaultdict(Counter)
    itemdata_by_type = defaultdict(Counter)
    flags_by_type = defaultdict(Counter)
    attach_examples = {}
    toolbox_examples = {}
    rope_examples = {}

    for name, lv in levels.items():
        level_keys.update(k for k in lv if not k.startswith("itemInfos_"))
        for f in LEVEL_FIELDS:
            level_vals[f][repr(lv.get(f))] += 1
        goal = lv.get("goal", {})
        goal_types[goal.get("type")] += 1
        goal_keys.update(goal.keys())
        for it in items_of(lv):
            item_keys.update(it.keys())
            for f in ITEM_FIELDS:
                item_vals[f][it.get(f)] += 1
            t = it["type"]
            flags_by_type[t][it["flags"]] += 1
            itemdata_by_type[t][it["itemData"]] += 1
            h = it["handle"] & 0xFFFFFFFF
            if (h >> 26) != t:
                handle_mismatch.append((name, t, hex(h)))
            if it["attachmentCount"] > 0 and t not in attach_examples:
                attach_examples[t] = (name, it)
            if (it["ropeEndPos_x"] or it["ropeEndPos_y"]) and t not in rope_examples:
                rope_examples[t] = (name, it)
        if lv.get("toolboxSlotCount", 0) > 0 and len(toolbox_examples) < 3:
            toolbox_examples[name] = {k: v for k, v in lv.items() if "toolbox" in k.lower() or "slot" in k.lower()}

    print(f"levels: {len(levels)}")
    print("level keys:", dict(level_keys))
    print("item keys:", dict(item_keys))
    print("goal keys:", dict(goal_keys))
    for f, c in level_vals.items():
        print(f"level.{f}:", dict(c))
    for f, c in item_vals.items():
        print(f"item.{f}:", dict(sorted(c.items(), key=lambda kv: str(kv[0]))))
    print("goal.type:", dict(goal_types))
    print("handle>>26 == type mismatches:", len(handle_mismatch), handle_mismatch[:5])
    print("flags by type:", {t: dict(c) for t, c in sorted(flags_by_type.items())})
    print("itemData by type:", {t: dict(c) for t, c in sorted(itemdata_by_type.items()) if len(c) > 1 or 0 not in c})
    print("\nattachment examples:")
    for t, (name, it) in sorted(attach_examples.items()):
        print(f"  type {t} in {name}: {it}")
    print("\nropeEndPos examples:")
    for t, (name, it) in sorted(rope_examples.items()):
        print(f"  type {t} in {name}: {it}")
    print("\ntoolbox examples:")
    for name, d in toolbox_examples.items():
        print(f"  {name}: {d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
