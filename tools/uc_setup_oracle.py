#!/usr/bin/env python3
"""G5a set-up interaction oracle: runs a manipulation script on the original binary (Unicorn).

A shipped level is built in set-up mode with its attachments (tools/uc_trace.py, `populate_level`) inside a
WorldState-shaped block, then a *setup script* drives the game's own set-up functions on it —
`GameItemUtils::SetPos / UpdatePos / UpdateAngle / Flip / ManipulationStarted / ManipulationEnded`,
`AttachmentUtils::CalculateSnap / Detach / UnsnapAllNotAttached / AttachToNearbyItems`,
`b2World::Step(0, 1, 1)` under `WorldContactListenerSetUp` and `PhysicsObjectUtils::IsColliding[WithAnother]`.
After every command the state of every object is dumped exactly (float32 bits), together with the Box2D
construction / transform calls the command made and the actions it queued. `tests/sim_setup_dump` runs the
same script on aa_sim; `tools/setup_conformance.py` diffs the two dumps.

Script (one command per line, `#` comments; objects and bodies are the level's object / body indices):
  level <chapter>/<name>                           the level (chapter dir + file stem; must come first)
  setpos <obj> <x> <y>                             GameItemUtils::SetPos
  updatepos <obj> <body> <x> <y> [angle [vx vy]]   GameItemUtils::UpdatePos with a TouchState (state 2,
                                                   selectedObject, bodyIndex, targetPos, angleCurrent,
                                                   dragVelocity) and snapping on
  updateangle <obj> <angle>                        GameItemUtils::UpdateAngle
  flip <obj>                                       GameItemUtils::Flip
  started <obj> <body>                             GameItemUtils::ManipulationStarted
  ended <obj>                                      GameItemUtils::ManipulationEnded
  snap <obj> <x> <y> <qx> <qy> <r>                 AttachmentUtils::CalculateSnap (position, query centre, radius)
  detach <obj> <point>                             AttachmentUtils::Detach
  unsnapall <obj>                                  AttachmentUtils::UnsnapAllNotAttached
  attachnearby <obj>                               AttachmentUtils::AttachToNearbyItems
  step0                                            b2World::Step(0, 1, 1) (GameScreen::UpdatePaused)
  colliding <obj>                                  PhysicsObjectUtils::IsColliding
  collidingwith <a> <b>                            PhysicsObjectUtils::IsCollidingWithAnother

Dump (`.setup` file): the construction scene lines, then per command
  > <command line>
  obj <i> <type> <x> <y> <angle> <scale.x> <state byte> <body count>
  att <i> <k> <state> <other object> <other point> <has joint>
  item <i> <end x> <end y>                         (rope / zip line end, slingshot pouch)
  <scene lines made by the command>                (transform / body / joint / destroy…)
  snap <found> <x> <y> <angle> <point> <other> <other point>
  colliding <0|1>
  action <id> <object>                             (ActionQueueUtils::Add, in order)

Usage: uc_setup_oracle.py <lib.so> <GameItems.plist> --levels DIR --script S.txt [--script …] --out DIR
"""
from __future__ import annotations

import argparse
import logging
import plistlib
import struct
import sys
from pathlib import Path

from unicorn import UcError
from unicorn.arm_const import UC_ARM_REG_PC

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uc_harness as H  # noqa: E402
import uc_trace as T  # noqa: E402

log = logging.getLogger("uc_setup_oracle")

TOUCH_STATE_SIZE = 0x70
TOUCH_DRAGGING = 2
SNAP_RESULT_SIZE = 0x1C
LISTENER_SIZE = 0x10
ACTION_QUEUE_SIZE = 0x1000
OBJ_ITEM_HANDLE, OBJ_STATE, OBJ_POS, OBJ_ANGLE, OBJ_SCALE_X, OBJ_ATTACH_COUNT, OBJ_BODY_COUNT = 8, 0xD, 0x10, 0x18, 0x1C, 0x28, 0x94
REC_STATE, REC_OTHER, REC_OTHER_POINT, REC_JOINT = 0x20, 0x24, 0x28, 0x2C
ROPE, SLINGSHOT, ZIP_LINE = 9, 34, 42


def fbits_at(e: H.Emu, addr: int) -> str:
    return "0x%08x" % e.u32(addr)


def fpack(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def s32(v: int) -> int:
    return v - (1 << 32) if v & 0x80000000 else v


class Oracle:
    def __init__(self, emu: H.Emu, rec: T.SceneRecorder, level: dict) -> None:
        self.e = emu
        self.rec = rec
        self.scene = T.Scene(emu, rec, capacity=int(level["itemCount"]) + 2, mode=T.MODE_SETUP, world_state=True)
        self.statuses = T.populate_level(self.scene, level)
        # Every GameItem block knows its object (GameItem+4), as GameItemCollectionUtils::InsertWithHandle sets it.
        for i in range(len(self.scene.objects)):
            emu.w32(self.scene.item_block(i) + 4, i)
        self.actions: list[tuple[int, int]] = []
        emu.hook_function("st::ActionQueueUtils::Add(st::ActionQueue&, st::Action const&)", self.on_action)
        self.queue = emu.malloc(ACTION_QUEUE_SIZE, zero=True)
        self.touch = emu.malloc(TOUCH_STATE_SIZE, zero=True)
        self.snap_result = emu.malloc(SNAP_RESULT_SIZE, zero=True)
        self.vec = emu.malloc(16)
        listener = emu.malloc(LISTENER_SIZE, zero=True)
        emu.call("st::WorldContactListenerSetUp::WorldContactListenerSetUp(st::GameState const&, st::ActionQueue&)",
                 listener, 0, self.queue)
        emu.call("b2World::SetContactListener(b2ContactListener*)", self.scene.world, listener)
        self.out: list[str] = list(rec.lines)

    # -- hooks
    def on_action(self, e: H.Emu) -> bool:
        _queue, action = e.args(2)
        handle = e.u32(action + 4)
        self.actions.append((e.u32(action), handle - 1 if handle else -1))
        return True

    # -- helpers
    def obj(self, i: int) -> int:
        return self.scene.objects[i][1]

    def item(self, i: int) -> int:
        return self.scene.item_block(i)

    def vec2(self, x: float, y: float, slot: int = 0) -> int:
        p = self.vec + slot * 8
        self.e.wf32(p, x)
        self.e.wf32(p + 4, y)
        return p

    def dump_state(self) -> None:
        e = self.e
        for i, (item_type, obj) in enumerate(self.scene.objects):
            self.out.append(f"obj {i} {e.u32(obj)} {fbits_at(e, obj + OBJ_POS)} {fbits_at(e, obj + OBJ_POS + 4)} "
                            f"{fbits_at(e, obj + OBJ_ANGLE)} {fbits_at(e, obj + OBJ_SCALE_X)} "
                            f"{e.read(obj + OBJ_STATE, 1)[0]} {e.u32(obj + OBJ_BODY_COUNT)}")
            for k in range(e.u32(obj + OBJ_ATTACH_COUNT)):
                r = obj + T.ATTACH_BASE + k * T.ATTACH_STRIDE
                self.out.append(f"att {i} {k} {e.u32(r + REC_STATE)} {s32(e.u32(r + REC_OTHER))} "
                                f"{s32(e.u32(r + REC_OTHER_POINT))} {1 if e.u32(r + REC_JOINT) else 0}")
            if item_type in (ROPE, ZIP_LINE):
                self.out.append(f"item {i} {fbits_at(e, self.item(i) + 8)} {fbits_at(e, self.item(i) + 0xC)}")
            elif item_type == SLINGSHOT:
                self.out.append(f"item {i} {fbits_at(e, self.item(i) + 0xC)} {fbits_at(e, self.item(i) + 0x10)}")

    # -- commands
    def run(self, line: str) -> None:
        parts = line.split()
        cmd, args = parts[0], parts[1:]
        e = self.e
        s = self.scene
        before = len(self.rec.lines)
        self.actions = []
        self.out.append(f"> {line}")
        extra: list[str] = []
        if cmd == "setpos":
            i = int(args[0])
            e.call("st::GameItemUtils::SetPos(st::GameItem&, st::PhysicsObject&, st::Vec2 const&)",
                   self.item(i), self.obj(i), self.vec2(float(args[1]), float(args[2])))
        elif cmd == "updatepos":
            i, body = int(args[0]), int(args[1])
            x, y = float(args[2]), float(args[3])
            angle = float(args[4]) if len(args) > 4 else 0.0
            vx, vy = (float(args[5]), float(args[6])) if len(args) > 6 else (0.0, 0.0)
            t = self.touch
            e.write(t, b"\x00" * TOUCH_STATE_SIZE)
            e.w32(t, TOUCH_DRAGGING)
            for off in (4, 8, 0xC, 0x54, 0x68, 0x6C):
                e.w32(t + off, 0xFFFFFFFF)
            e.w32(t + 0x18, i)
            e.w32(t + 0x1C, body)
            e.wf32(t + 0x28, x)
            e.wf32(t + 0x2C, y)
            e.wf32(t + 0x30, vx)
            e.wf32(t + 0x34, vy)
            e.wf32(t + 0x3C, angle)
            e.call("st::GameItemUtils::UpdatePos(st::GameItem&, st::PhysicsObject&, st::TouchState const&, bool, "
                   "st::HandleManager const&, st::PhysicsObjectCollection&, st::ActionQueue&)",
                   self.item(i), self.obj(i), t, 1, s.handle_mgr, s.coll, self.queue)
        elif cmd == "updateangle":
            i = int(args[0])
            e.call("st::GameItemUtils::UpdateAngle(st::GameItem&, st::PhysicsObject&, float, st::HandleManager const&, "
                   "st::PhysicsObjectCollection&)", self.item(i), self.obj(i), fpack(float(args[1])), s.handle_mgr, s.coll)
        elif cmd == "flip":
            i = int(args[0])
            self.rec.item_type = self.scene.objects[i][0]
            e.call("st::GameItemUtils::Flip(st::GameItem&, st::PhysicsObject&, st::HandleManager const&, "
                   "st::PhysicsObjectCollection&, st::ActionQueue&)", self.item(i), self.obj(i), s.handle_mgr, s.coll, self.queue)
        elif cmd == "started":
            i = int(args[0])
            e.call("st::GameItemUtils::ManipulationStarted(st::GameItem&, st::PhysicsObject&, int, st::HandleManager const&, "
                   "st::PhysicsObjectCollection&)", self.item(i), self.obj(i), int(args[1]), s.handle_mgr, s.coll)
        elif cmd == "ended":
            i = int(args[0])
            self.rec.item_type = self.scene.objects[i][0]
            e.call("st::GameItemUtils::ManipulationEnded(st::GameItem&, st::PhysicsObject&, st::WorldState&)",
                   self.item(i), self.obj(i), s.world_state)
        elif cmd == "snap":
            i = int(args[0])
            r = self.snap_result
            e.write(r, b"\x00" * SNAP_RESULT_SIZE)
            e.call("st::AttachmentUtils::CalculateSnap(st::SnapResult&, st::PhysicsObject const&, st::Vec2 const&, "
                   "st::Vec2 const&, float)", r, self.obj(i), self.vec2(float(args[1]), float(args[2])),
                   self.vec2(float(args[3]), float(args[4]), slot=1), fpack(float(args[5])))
            found = e.read(r, 1)[0]
            # SnapResult+0x14 holds the other PhysicsObject's address; the dump names it by index.
            other = (e.u32(r + 0x14) - s.coll - 8) // T.OBJ_STRIDE if found else -1
            extra.append(f"snap {found} {fbits_at(e, r + 4)} {fbits_at(e, r + 8)} {fbits_at(e, r + 0xC)} "
                         f"{e.u32(r + 0x10) if found else -1} {other} {e.u32(r + 0x18) if found else -1}")
        elif cmd == "detach":
            i = int(args[0])
            e.call("st::AttachmentUtils::Detach(st::PhysicsObject&, int, st::PhysicsObjectCollection&, st::HandleManager const&)",
                   self.obj(i), int(args[1]), s.coll, s.handle_mgr)
        elif cmd == "unsnapall":
            i = int(args[0])
            e.call("st::AttachmentUtils::UnsnapAllNotAttached(st::PhysicsObject&, st::PhysicsObjectCollection&)",
                   self.obj(i), s.coll)
        elif cmd == "attachnearby":
            i = int(args[0])
            e.call("st::AttachmentUtils::AttachToNearbyItems(st::PhysicsObject&, st::PhysicsObjectCollection&, st::HandleManager const&)",
                   self.obj(i), s.coll, s.handle_mgr)
        elif cmd == "step0":
            e.call("b2World::Step(float, int, int)", s.world, fpack(0.0), 1, 1)
        elif cmd == "colliding":
            hit = e.call("st::PhysicsObjectUtils::IsColliding(st::PhysicsObject const&)", self.obj(int(args[0])))
            extra.append(f"colliding {hit & 0xFF}")
        elif cmd == "collidingwith":
            hit = e.call("st::PhysicsObjectUtils::IsCollidingWithAnother(st::PhysicsObject const&, st::PhysicsObject const&)",
                         self.obj(int(args[0])), self.obj(int(args[1])))
            extra.append(f"colliding {hit & 0xFF}")
        else:
            raise ValueError(f"unknown setup command: {line}")
        self.dump_state()
        self.out.extend(l for l in self.rec.lines[before:] if not l.startswith("#"))
        self.out.extend(extra)
        self.out.extend(f"action {aid} {obj}" for aid, obj in self.actions)


def read_script(path: Path) -> tuple[str, list[str]]:
    level = ""
    commands = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("level "):
            level = line.split(None, 1)[1].strip()
        else:
            commands.append(line)
    if not level:
        raise ValueError(f"{path}: no `level` line")
    return level, commands


def run_script(emu: H.Emu, rec: T.SceneRecorder, script: Path, levels_dir: Path) -> list[str]:
    level_ref, commands = read_script(script)
    chapter, name = level_ref.split("/", 1)
    with open(levels_dir / chapter / f"{name}.plist", "rb") as f:
        level = plistlib.load(f)
    oracle = Oracle(emu, rec, level)
    for st in oracle.statuses:
        if "failed" in st:
            raise RuntimeError(f"{script.name}: {st}")
    for cmd in commands:
        try:
            oracle.run(cmd)
        except (UcError, RuntimeError) as exc:
            pc = emu.uc.reg_read(UC_ARM_REG_PC)
            raise RuntimeError(f"{script.name}: `{cmd}` failed: {exc} at {pc:#x} {emu.describe(pc)}") from exc
    return [f"# amazing-alex setup trace v1 level={level_ref} commands={len(commands)}"] + oracle.out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("so")
    parser.add_argument("items_plist")
    parser.add_argument("--levels", type=Path, required=True, help="decrypted level plists (chapter dirs)")
    parser.add_argument("--script", type=Path, action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args.out.mkdir(parents=True, exist_ok=True)
    emu, rec = T.make_emulator(args.so, args.items_plist)
    failures = 0
    for script in args.script:
        try:
            lines = run_script(emu, rec, script, args.levels)
        except (RuntimeError, ValueError, FileNotFoundError) as exc:
            log.error("%s", exc)
            failures += 1
            continue
        T.write(args.out / f"{script.stem}.setup", lines)
        log.info("%s: %d lines", script.stem, len(lines))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
