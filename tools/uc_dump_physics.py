#!/usr/bin/env python3
"""Recover the exact Box2D set-up of every Amazing Alex item by running the game's own code.

Runs `libamazingalex.so` under Unicorn (see uc_harness.py), feeds it the real sprite frames from
`GameItems.plist`, lets `InitializePhysicsObjectTemplates` build the template table, creates a real
`b2World` and calls `PhysicsObjectUtils::CreatePhysics` for each item type in both physics modes,
recording every `b2World::CreateBody`, `b2Body::CreateFixture`, `b2World::CreateJoint` and
`b2Body::SetMassData` call with fully decoded definitions.

Usage: uc_dump_physics.py <libamazingalex.so> <GameItems.plist> -o physics_dump.json [--md physics_dump.md]
"""
from __future__ import annotations

import argparse
import json
import logging
import plistlib
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import uc_harness as H  # noqa: E402
from unicorn import UcError  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_PC  # noqa: E402

log = logging.getLogger("uc_dump_physics")

ITEM_NAMES = {
    1: "Shelf", 2: "TennisBall", 3: "BowlingBall", 4: "SoccerBall", 5: "Balloon", 6: "Scissors",
    7: "Bucket", 8: "Hook", 9: "Rope", 10: "CardboardBoxMedium", 11: "CardboardBoxSmall", 12: "FishBowl",
    13: "PiggyBank", 14: "BoxingGlove", 15: "Book", 16: "EightBall", 17: "Pipe", 18: "Pipe90",
    19: "Doll", 20: "Skateboard", 21: "Pulley", 22: "Seesaw", 23: "GoalStar", 24: "Billboard",
    25: "Magnet", 26: "Pinball", 27: "PaperPlane", 28: "Spring", 29: "Dart", 30: "HangingLamp",
    31: "WorldBound", 32: "LaundryBasket", 33: "Bumper", 34: "Slingshot", 35: "RCTruck",
    36: "RCController", 37: "Trapdoor", 38: "TrapdoorLever", 39: "Helicopter", 40: "SelectionArea",
    41: "BouncyBall", 42: "ZipLine",
}
BODY_TYPES = {0: "static", 1: "kinematic", 2: "dynamic"}
SHAPE_TYPES = {0: "circle", 1: "edge", 2: "polygon", 3: "loop"}
JOINT_TYPES = {0: "unknown", 1: "revolute", 2: "prismatic", 3: "distance", 4: "pulley", 5: "mouse",
               6: "gear", 7: "line", 8: "weld", 9: "friction", 10: "rope"}
MODES = {0: "setup", 1: "simulation"}
OBJ_STRIDE = 0xD8
ATT_STRIDE = 0x30
ATT_BASE = 0x2C
FRAME_SIZE = 20
PX_TO_M = 3.41 / 1024.0


def _end_vector(x: float, y: float):
    """Rope and zip line keep the second end as a vector relative to the centre at +8/+0xC of their
    GameItem state (`LevelLayoutUtils::Apply` cases 9 / 0x2a)."""
    def init(e: H.Emu, buf: int) -> None:
        e.wf32(buf + 8, x)
        e.wf32(buf + 0xC, y)
    return init


def _pouch_vector(x: float, y: float):
    """The slingshot keeps its pouch (relative to the frame) at +0xC/+0x10 (Apply case 0x22)."""
    def init(e: H.Emu, buf: int) -> None:
        e.wf32(buf + 0xC, x)
        e.wf32(buf + 0x10, y)
    return init


def _book_colour(i: int):
    def init(e: H.Emu, buf: int) -> None:
        e.w32(buf + 8, i)  # Book+8 = colour index → size table DAT_00282ef4 (Ghidra) / 0x272ef4
    return init


ITEM_INFO_STRIDE = 0x18   # st::ItemInfos entry: +8 item block size, +0xC default block

# Per-type variants of the GameItem state returned by HandleManager::Get (over the ItemInfos default block).
STATE_VARIANTS: dict[int, list[tuple[str, object]]] = {
    9: [("end (0.6, 0)", _end_vector(0.6, 0.0))],
    15: [(f"colour {i}", _book_colour(i)) for i in range(4)],
    34: [("pouch (0, 0.5)", _pouch_vector(0.0, 0.5))],
    42: [("end (0.8, -0.3)", _end_vector(0.8, -0.3))],
}


def r(x: float, n: int = 6) -> float:
    return round(x, n)


def build_frames(emu: H.Emu, plist_path: str) -> int:
    """Create a CountedArray<st::Frame> {+4 count, +8 items} from the atlas plist (file order)."""
    with open(plist_path, "rb") as fh:
        frames = plistlib.load(fh)["frames"]
    items = emu.malloc(len(frames) * FRAME_SIZE, zero=True)
    for i, (_name, fr) in enumerate(frames.items()):
        x, y, w, h = (int(v) for v in re.findall(r"-?\d+", fr["frame"]))
        base = items + i * FRAME_SIZE
        emu.wf32(base + 4, float(y + h))
        emu.wf32(base + 8, float(y))
        emu.wf32(base + 0xC, float(x))
        emu.wf32(base + 0x10, float(x + w))
    arr = emu.malloc(16, zero=True)
    emu.w32(arr + 4, len(frames))
    emu.w32(arr + 8, items)
    return arr


class Recorder:
    def __init__(self, emu: H.Emu) -> None:
        self.emu = emu
        self.events: list[dict] = []
        self.bodies: dict[int, int] = {}  # body pointer → index
        self.state_buffers: list[int] = []
        emu.hook_function("b2Body::b2Body(b2BodyDef const*, b2World*)", self.on_body_ctor)
        emu.hook_function("b2Body::CreateFixture(b2FixtureDef const*)", self.on_create_fixture)
        emu.hook_function("b2Body::CreateFixture(b2Shape const*, float)", self.on_create_fixture_simple)
        emu.hook_function("b2World::CreateJoint(b2JointDef const*)", self.on_create_joint)
        emu.hook_function("b2Body::SetMassData(b2MassData const*)", self.on_set_mass)
        emu.hook_function("b2Body::SetTransform(b2Vec2 const&, float)", self.on_set_transform)
        emu.hook_function("st::HandleManager::Get(st::Handle) const", self.on_handle_get)
        self.state_init = None
        self.item_type = 0

    # -- helpers
    def vec2(self, addr: int) -> list[float]:
        return [r(self.emu.f32_at(addr)), r(self.emu.f32_at(addr + 4))]

    def body_index(self, ptr: int) -> int:
        return self.bodies.get(ptr, -1)

    def shape(self, addr: int) -> dict:
        e = self.emu
        stype = e.u32(addr + 4)
        out = {"type": SHAPE_TYPES.get(stype, stype), "radius": r(e.f32_at(addr + 8))}
        if stype == 2:
            n = e.u32(addr + 0x94)
            out["vertices"] = [self.vec2(addr + 0x14 + i * 8) for i in range(n)]
            out["centroid"] = self.vec2(addr + 0xC)
        elif stype == 0:
            out["center"] = self.vec2(addr + 0xC)
        elif stype == 1:
            out["v1"] = self.vec2(addr + 0xC)
            out["v2"] = self.vec2(addr + 0x14)
        return out

    def filter(self, addr: int) -> dict:
        cat, mask, group = struct.unpack("<HHh", self.emu.read(addr, 6))
        return {"category": cat, "mask": mask, "group": group}

    # -- hooks (all return False → the real function still runs)
    def on_body_ctor(self, e: H.Emu) -> bool:
        this, d = e.args(2)
        idx = len(self.bodies)
        self.bodies[this] = idx
        allow_sleep, awake, fixed_rot, bullet, active = e.read(d + 0x24, 5)
        self.events.append({
            "op": "body", "index": idx,
            "type": BODY_TYPES.get(e.u32(d), e.u32(d)),
            "position": self.vec2(d + 4), "angle": r(e.f32_at(d + 0xC)),
            "linearVelocity": self.vec2(d + 0x10), "angularVelocity": r(e.f32_at(d + 0x18)),
            "linearDamping": r(e.f32_at(d + 0x1C)), "angularDamping": r(e.f32_at(d + 0x20)),
            "allowSleep": bool(allow_sleep), "awake": bool(awake), "fixedRotation": bool(fixed_rot),
            "bullet": bool(bullet), "active": bool(active), "userData": e.u32(d + 0x2C),
            "inertiaScale": r(e.f32_at(d + 0x30)),
        })
        return False

    def on_create_fixture(self, e: H.Emu) -> bool:
        body, d = e.args(2)
        self.events.append({
            "op": "fixture", "body": self.body_index(body),
            "shape": self.shape(e.u32(d)), "userData": e.u32(d + 4),
            "friction": r(e.f32_at(d + 8)), "restitution": r(e.f32_at(d + 0xC)),
            "density": r(e.f32_at(d + 0x10)), "isSensor": bool(e.read(d + 0x14, 1)[0]),
            "filter": self.filter(d + 0x16),
        })
        return False

    def on_create_fixture_simple(self, e: H.Emu) -> bool:
        body, shape, density = e.args(3)
        self.events.append({"op": "fixture", "body": self.body_index(body), "shape": self.shape(shape),
                            "density": r(H.f32(density)), "friction": 0.2, "restitution": 0.0,
                            "isSensor": False, "filter": {"category": 1, "mask": 0xFFFF, "group": 0}})
        return False

    def on_create_joint(self, e: H.Emu) -> bool:
        _world, d = e.args(2)
        jtype = e.u32(d)
        ev = {"op": "joint", "type": JOINT_TYPES.get(jtype, jtype), "bodyA": self.body_index(e.u32(d + 8)),
              "bodyB": self.body_index(e.u32(d + 0xC)), "collideConnected": bool(e.read(d + 0x10, 1)[0])}
        f = e.f32_at
        if jtype == 1:  # revolute
            ev.update(localAnchorA=self.vec2(d + 0x14), localAnchorB=self.vec2(d + 0x1C), referenceAngle=r(f(d + 0x24)),
                      enableLimit=bool(e.read(d + 0x28, 1)[0]), lowerAngle=r(f(d + 0x2C)), upperAngle=r(f(d + 0x30)),
                      enableMotor=bool(e.read(d + 0x34, 1)[0]), motorSpeed=r(f(d + 0x38)), maxMotorTorque=r(f(d + 0x3C)))
        elif jtype == 2:  # prismatic
            ev.update(localAnchorA=self.vec2(d + 0x14), localAnchorB=self.vec2(d + 0x1C), localAxis=self.vec2(d + 0x24),
                      referenceAngle=r(f(d + 0x2C)), enableLimit=bool(e.read(d + 0x30, 1)[0]),
                      lowerTranslation=r(f(d + 0x34)), upperTranslation=r(f(d + 0x38)),
                      enableMotor=bool(e.read(d + 0x3C, 1)[0]), maxMotorForce=r(f(d + 0x40)), motorSpeed=r(f(d + 0x44)))
        elif jtype == 3:  # distance
            ev.update(localAnchorA=self.vec2(d + 0x14), localAnchorB=self.vec2(d + 0x1C), length=r(f(d + 0x24)),
                      frequencyHz=r(f(d + 0x28)), dampingRatio=r(f(d + 0x2C)),
                      resistCompression=bool(e.read(d + 0x30, 1)[0]))   # trunk-only flag (04 §1)
        elif jtype == 8:  # weld
            ev.update(localAnchorA=self.vec2(d + 0x14), localAnchorB=self.vec2(d + 0x1C), referenceAngle=r(f(d + 0x24)))
        elif jtype == 10:  # rope
            ev.update(localAnchorA=self.vec2(d + 0x14), localAnchorB=self.vec2(d + 0x1C), maxLength=r(f(d + 0x24)))
        elif jtype == 7:  # line = the trunk's wheel joint (b2WheelJointDef layout, no limits; see b2LineJoint ctor)
            ev.update(localAnchorA=self.vec2(d + 0x14), localAnchorB=self.vec2(d + 0x1C), localAxisA=self.vec2(d + 0x24),
                      enableMotor=bool(e.read(d + 0x2C, 1)[0]), maxMotorTorque=r(f(d + 0x30)),
                      motorSpeed=r(f(d + 0x34)), frequencyHz=r(f(d + 0x38)), dampingRatio=r(f(d + 0x3C)))
        else:
            ev["raw"] = e.read(d, 0x50).hex()
        self.events.append(ev)
        return False

    def on_set_mass(self, e: H.Emu) -> bool:
        body, md = e.args(2)
        self.events.append({"op": "massData", "body": self.body_index(body), "mass": r(e.f32_at(md)),
                            "center": self.vec2(md + 4), "I": r(e.f32_at(md + 0xC))})
        return False

    def on_set_transform(self, e: H.Emu) -> bool:
        body, pos, angle = e.args(3)
        self.events.append({"op": "transform", "body": self.body_index(body), "position": self.vec2(pos),
                            "angle": r(H.f32(angle))})
        return False

    def on_handle_get(self, e: H.Emu) -> bool:
        # Default item block from st::ItemInfos (what GameItemCollectionUtils::InsertWithHandle copies),
        # then the variant's own state on top.
        buf = e.malloc(0x2000, zero=True)
        self.state_buffers.append(buf)
        info = e.syms.by_name["st::ItemInfos"] + self.item_type * ITEM_INFO_STRIDE
        e.write(buf, e.read(e.u32(info + 0xC), e.u32(info + 8)))
        if self.state_init is not None:
            self.state_init(e, buf)
        e.ret(buf)
        return True


def read_object(e: H.Emu, obj: int) -> dict:
    flags, state = e.read(obj + 0xC, 2)
    n_att = e.u32(obj + 0x28)
    atts = []
    for i in range(min(n_att, 4)):
        a = obj + ATT_BASE + i * ATT_STRIDE
        atts.append({"pos": [r(e.f32_at(a)), r(e.f32_at(a + 4))], "dir": [r(e.f32_at(a + 8)), r(e.f32_at(a + 0xC))],
                     "kind": e.u32(a + 0x10), "mask": e.u32(a + 0x14), "body": e.u32(a + 0x18),
                     "positionOnly": e.read(a + 0x1C, 1)[0]})
    # +0x24 is the template's half-size / radius (docs/04 §5); +0x28 is already the attachment count.
    return {"flags": flags, "state": state, "halfSize": r(e.f32_at(obj + 0x24)),
            "attachments": atts, "bodyCount": e.u32(obj + 0x94)}


def dump(so_path: str, plist_path: str) -> dict:
    emu = H.make_emu(so_path)
    frames = build_frames(emu, plist_path)
    emu.call("st::PhysicsObjectsUtils::InitializePhysicsObjectTemplates(st::CountedArray<st::Frame> const&)", frames)
    emu.call("st::CollisionFiltersUtils::Create()")  # fills the CollisionFilters::* pointers (normally from GamePhysicsUtils::CreateWorld)
    rec = Recorder(emu)
    world_ctor = "b2World::b2World(b2Vec2 const&, bool)"
    result: dict = {"items": {}}
    for item_type in range(1, 43):
        entry: dict = {"name": ITEM_NAMES[item_type], "variants": {}}
        for label, state_init in STATE_VARIANTS.get(item_type, [("default", None)]):
            modes: dict = {}
            for mode in (0, 1):
                world = emu.malloc(0x40000, zero=True)
                gravity = emu.malloc(8)
                emu.wf32(gravity, 0.0)
                emu.wf32(gravity + 4, -9.8)
                emu.call(world_ctor, world, gravity, 1)
                coll = emu.malloc(0x10 + 4 * OBJ_STRIDE, zero=True)
                idx = emu.call("st::PhysicsObjectsUtils::Add(st::PhysicsObjectCollection&, st::ItemType::Enum)", coll, item_type)
                obj = coll + 8 + idx * OBJ_STRIDE
                emu.wf32(obj + 0x10, 1.7)
                emu.wf32(obj + 0x14, 1.0)
                emu.wf32(obj + 0x18, 0.0)
                handle_mgr = emu.malloc(0x100, zero=True)
                rec.events = []
                rec.item_type = item_type
                rec.bodies = {}
                rec.state_init = state_init
                try:
                    emu.call("st::PhysicsObjectUtils::CreatePhysics(st::PhysicsObject&, b2World&, st::HandleManager const&, st::PhysicsMode::Enum)",
                             obj, world, handle_mgr, mode)
                    status = "ok"
                except (UcError, RuntimeError) as exc:
                    pc = emu.uc.reg_read(UC_ARM_REG_PC)
                    status = f"failed: {exc} at {pc:#x} {emu.describe(pc)}"
                    log.warning("%s (%s) mode %d: %s", ITEM_NAMES[item_type], label, mode, status)
                modes[MODES[mode]] = {"status": status, "object": read_object(emu, obj), "events": rec.events}
            entry["variants"][label] = modes
        result["items"][item_type] = entry
    return result


def to_markdown(res: dict) -> str:
    lines = ["# Physics dump (generated by `tools/uc_dump_physics.py`)", "",
             "Bodies, fixtures and joints created by the original `CreatePhysics` code for an item placed at (1.7, 1.0), "
             "angle 0, scale (1, 1). Polygon vertices are in body-local metres. Filter = category/mask/group.", "",
             "Assumptions: `PhysicsMode` 0 = set-up, 1 = simulation (04 §2). The per-item `GameItem` state handed to "
             "`CreatePhysics` is the type's default block from `st::ItemInfos` (03 §2: scissors cut angle 15°, glove button "
             "height 36 px, default rope/slingshot/zip-line end vectors) except where a section title names a variant: "
             "Rope/Slingshot/ZipLine get the end vector shown, Book the colour index 0–3; `WorldBound` therefore uses "
             "`backgroundIndex` 0 (the Treehouse floor hole is described in 04 §2). The `fixed` flag is clear. Every `b2World::CreateBody`, "
             "`b2Body::CreateFixture`, `b2World::CreateJoint`, `b2Body::SetMassData` and `b2Body::SetTransform` call is "
             "listed in call order, so a body created at (0, 0) and moved by a later `setTransform` row is positioned by that "
             "row. Values depend on the sprite frame sizes of `GameItems.plist` (1024-px atlas) through the template table.", ""]
    for t, entry in res["items"].items():
        lines.append(f"## {t} {entry['name']}")
        for label, modes in entry["variants"].items():
          for mode, m in modes.items():
            o = m["object"]
            variant = "" if label == "default" else f" [{label}]"
            lines.append(f"\n### {mode}{variant} — {m['status']}; halfSize {o['halfSize']}, flags {o['flags']:#04x}, bodies {o['bodyCount']}")
            if o["attachments"]:
                lines.append("attachments: " + "; ".join(
                    f"pos {a['pos']} dir {a['dir']} kind {a['kind']} mask {a['mask']} body {a['body']} posOnly {a['positionOnly']}"
                    for a in o["attachments"]))
            lines.append("")
            lines.append("| op | body | details |")
            lines.append("|----|------|---------|")
            for ev in m["events"]:
                if ev["op"] == "body":
                    det = (f"{ev['type']} pos {ev['position']} angle {ev['angle']} damping {ev['linearDamping']}/{ev['angularDamping']}"
                           f" bullet {ev['bullet']} fixedRot {ev['fixedRotation']} sleep {ev['allowSleep']} active {ev['active']}")
                    lines.append(f"| body #{ev['index']} | | {det} |")
                elif ev["op"] == "fixture":
                    s = ev["shape"]
                    if s["type"] == "polygon":
                        sh = f"polygon {s['vertices']}"
                    elif s["type"] == "circle":
                        sh = f"circle r={s['radius']} at {s['center']}"
                    else:
                        sh = json.dumps(s)
                    f = ev["filter"]
                    det = (f"{sh}; d={ev['density']} μ={ev['friction']} e={ev['restitution']} sensor={ev['isSensor']}"
                           f" filter {f['category']:#x}/{f['mask']:#x}/{f['group']}")
                    lines.append(f"| fixture | {ev['body']} | {det} |")
                elif ev["op"] == "joint":
                    rest = {k: v for k, v in ev.items() if k not in ("op", "type", "bodyA", "bodyB")}
                    lines.append(f"| joint {ev['type']} | {ev['bodyA']}→{ev['bodyB']} | {json.dumps(rest)} |")
                elif ev["op"] == "massData":
                    lines.append(f"| massData | {ev['body']} | mass {ev['mass']} center {ev['center']} I {ev['I']} |")
                elif ev["op"] == "transform":
                    lines.append(f"| setTransform | {ev['body']} | pos {ev['position']} angle {ev['angle']} |")
        lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("so_path")
    parser.add_argument("plist_path")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--md")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    res = dump(args.so_path, args.plist_path)
    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(res, fh, indent=1)
    if args.md:
        with open(args.md, "w", encoding="utf-8") as fh:
            fh.write(to_markdown(res))
    log.info("wrote %s", args.output)


if __name__ == "__main__":
    main()
