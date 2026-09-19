#!/usr/bin/env python3
"""Ground-truth physics traces from the original Android binary (Unicorn).

Builds a scene with the game's own `PhysicsObjectUtils::CreatePhysics` (simulation mode) inside an emulated
`b2World`, records every Box2D construction call as an engine-agnostic *scene file*, then steps the emulated
world with the game's settings (`Step(1/120, 10, 10)` + manual `ClearForces`, auto-clear off) and writes the
per-step body states as a *trajectory file*. `tools/trace_native` replays the same scene on the vendored
Box2D 2.2.1 and `tools/trace_compare.py` diffs the two trajectories (see docs/09-physics-regression.md).

Scene file: one construction call per line in call order, floats as raw float32 hex bits (exact):
  gravity gx gy
  body <idx> <type> x y angle vx vy w linDamp angDamp allowSleep awake fixedRot bullet active
  circle <body> r cx cy density friction restitution sensor cat mask group
  box <body> hx hy density friction restitution sensor cat mask group            (SetAsBox(hx, hy))
  boxc <body> hx hy cx cy angle density ...                                     (SetAsBox(hx, hy, c, angle))
  polygon <body> n x1 y1 ... xn yn density ...                                  (Set(vertices, n))
  mass <body> m cx cy I
  transform <body> x y angle
  revolute a b collide ax ay bx by refAngle enableLimit lower upper enableMotor speed maxTorque
  prismatic a b collide ax ay bx by axX axY refAngle enableLimit lower upper enableMotor maxForce speed
  distance a b collide ax ay bx by length freq damp resistCompression
  wheel a b collide ax ay bx by axX axY enableMotor maxTorque speed freq damp
  polygonfix <body> <fixture> n x1 y1 ... nx1 ny1 ... cx cy                   (game edited the fixture's polygon
                                                                               in place after CreateFixture)
  destroyjoint <joint>                                                         (joints numbered in creation order)
  destroybody <body>                                                           (its joints go with it, implicitly)
  continuous 0                                                                 (optional experiment switch)
  step <count> <dt> <velIters> <posIters>

Destroyed bodies keep their index (the game rebuilds rope links when attachments move their ends) and are
left out of the trajectory.

Trajectory file: `step body x y angle vx vy w awake` per body per step (floats printed with %.9g).

Usage:
  uc_trace.py <lib.so> <GameItems.plist> --out DIR [--drop all | --drop 3,15,39] [--level L.plist ...]
              [--steps 600] [--x 1.7 --y 1.0]
              [--mode simulation|setup] [--flip] [--background N] [--state K]

Physics mode: `--mode simulation` (default) builds every item with `CreatePhysics(mode 1)` and steps the world;
`--mode setup` builds with mode 0 (set-up: dynamic bodies plus selection sensors, 04 §2) and, by default, does
not step (`--steps 0`), because the original never steps the set-up world. `--flip` negates the dropped item's
`scale.x` (the flipped state of 02 §3.2), `--background N` selects the world-bound variant of the dropped item
(type 31; 3 = Treehouse floor hole) and `--state K` the per-type state word (book colour, 03 §15). Scene names:
`drop_<tt>_<Name>` when none of these options is given (the physics regression, 09), otherwise
`drop_<tt>_<Name>_<mode>[_flip][_bg<N>][_s<K>]` (the G3 conformance matrix, tools/sim_conformance.py); level
scenes are `level_<chapter>_<name>[_setup]`.
"""
from __future__ import annotations

import argparse
import logging
import plistlib
import struct
import sys
from pathlib import Path
from typing import Callable

from unicorn import UcError
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uc_harness as H  # noqa: E402
from uc_dump_physics import ITEM_NAMES, OBJ_STRIDE, build_frames  # noqa: E402

log = logging.getLogger("uc_trace")

STEP_DT = 1.0 / 120.0
VEL_ITERS = 10
POS_ITERS = 10
WORLD_SIZE = 0x40000
WORLD_FLAGS_OFF = 0x191D4      # b2World::m_flags (trunk layout); bit 2 = e_clearForces
WORLD_CCD_OFF = 0x19251        # b2World::m_continuousPhysics (byte)
BODY_POS = 0xC                 # b2Body::m_xf.position
BODY_FIXTURE_LIST = 0x6C       # b2Body::m_fixtureList (b2Fixture: +4 next, +0xC shape)
POLY_BYTES = 0x90              # b2PolygonShape from m_radius (+8) to the end of m_normals (+0x94 = count)
BODY_ANGLE = 0x40              # b2Body::m_sweep.a
BODY_LINVEL = 0x48
BODY_ANGVEL = 0x50
BODY_FLAGS = 0x4
BODY_AWAKE_FLAG = 0x2
DROP_TYPES_NO_STATE = {24, 31, 40}   # billboard, world bound, editor helper: not dropped as items
ATTACH_BASE, ATTACH_STRIDE = 0x2C, 0x30   # PhysicsObject attachment records (04 §7)
# st::WorldState (GameState+0x2618): the PhysicsObjectCollection, the GameItem HandleManager and the b2World
# pointer at the offsets the set-up functions use (GameItemUtils::ManipulationEnded, TruckUtils::…).
WS_SIZE = 0x2F2C0
WS_COLLECTION = 0x2015C
WS_HANDLE_MANAGER = 0x27288
WS_WORLD = 0x2F2AC
ITEM_INFO_STRIDE = 0x18          # st::ItemInfos entry: +8 item block size, +0xC default block
LEGACY_HELI_CONTROLLER = 40
RC_CONTROLLER = 36
TREEHOUSE_BACKGROUND = 3
MODE_SETUP, MODE_SIMULATION = 0, 1            # st::PhysicsMode::Enum (04 §2)
MODE_NAMES = {"setup": MODE_SETUP, "simulation": MODE_SIMULATION}

StateInit = Callable[[H.Emu, int], None] | None


def fbits(x: float) -> str:
    """float → exact float32 bit pattern as hex text."""
    return "0x%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


class SceneRecorder:
    """Records Box2D construction calls made by the emulated game code, exactly (float32 bits)."""

    def __init__(self, emu: H.Emu) -> None:
        self.emu = emu
        self.lines: list[str] = []
        self.bodies: list[int] = []          # body pointers in creation order (destroyed bodies stay listed)
        self.body_index: dict[int, int] = {}
        self.dead: set[int] = set()          # indices of destroyed bodies
        self.fixture_shapes: dict[int, list[bytes]] = {}   # body index → polygon bytes per fixture (creation order)
        self.joints: list[int] = []          # joint pointers in creation order
        self.pending_joint_return: int | None = None
        self.hooked_returns: set[int] = set()   # return addresses that already carry a one-shot hook
        self.in_destroy_body = False
        self.shape_calls: dict[int, tuple] = {}   # shape ptr → last SetAsBox/Set call
        self.raw_polygons = 0
        self.state_init: StateInit = None
        self.item_type = 0             # type of the object whose CreatePhysics runs (ItemInfos lookup)
        # Persistent GameItem blocks per fake handle (Scene.add writes index + 1 into PhysicsObject+8):
        # RopeUtils::AttachmentChanged keeps the end-to-end joint pointer in Rope+0x10 across calls.
        self.items: dict[int, int] = {}
        self.item_meta: dict[int, tuple[int, StateInit]] = {}
        hook = emu.hook_function
        hook("b2Body::b2Body(b2BodyDef const*, b2World*)", self.on_body)
        hook("b2Body::CreateFixture(b2FixtureDef const*)", self.on_fixture)
        hook("b2Body::CreateFixture(b2Shape const*, float)", self.on_fixture_simple)
        hook("b2World::CreateJoint(b2JointDef const*)", self.on_joint)
        hook("b2Body::SetMassData(b2MassData const*)", self.on_mass)
        hook("b2Body::SetTransform(b2Vec2 const&, float)", self.on_transform)
        hook("b2PolygonShape::SetAsBox(float, float)", self.on_set_as_box)
        hook("b2PolygonShape::SetAsBox(float, float, b2Vec2 const&, float)", self.on_set_as_box_c)
        hook("b2PolygonShape::Set(b2Vec2 const*, int)", self.on_set_vertices)
        hook("st::HandleManager::Get(st::Handle) const", self.on_handle_get)
        hook("b2World::DestroyJoint(b2Joint*)", self.on_destroy_joint)
        hook("b2World::DestroyBody(b2Body*)", self.on_destroy_body)

    # -- helpers
    def f(self, addr: int) -> str:
        return fbits(self.emu.f32_at(addr))

    def v(self, addr: int) -> str:
        return f"{self.f(addr)} {self.f(addr + 4)}"

    def b(self, addr: int) -> str:
        return "1" if self.emu.read(addr, 1)[0] else "0"

    def idx(self, ptr: int) -> int:
        return self.body_index[ptr]

    def fixture_tail(self, density: str, friction: str, restitution: str, sensor: str, filt_addr: int | None) -> str:
        if filt_addr is None:
            cat, mask, group = 1, 0xFFFF, 0
        else:
            cat, mask, group = struct.unpack("<HHh", self.emu.read(filt_addr, 6))
        return f"{density} {friction} {restitution} {sensor} {cat} {mask} {group}"

    def shape_line(self, shape: int, tail: str) -> str:
        e = self.emu
        stype = e.u32(shape + 4)
        if stype == 0:
            return f"{self.f(shape + 8)} {self.v(shape + 0xC)} {tail}"          # circle: r, center
        if stype != 2:
            raise RuntimeError(f"unsupported shape type {stype}")
        call = self.polygon_call(shape)
        n = e.u32(shape + 0x94)
        verts = " ".join(self.v(shape + 0x14 + i * 8) for i in range(n))
        if call is not None and call[0] == "box":
            return f"{call[1]} {call[2]} {tail}"
        if call is not None and call[0] == "boxc":
            return f"{call[1]} {call[2]} {call[3]} {call[4]} {tail}"
        if call is None:
            self.raw_polygons += 1
            log.warning("polygon fixture without SetAsBox/Set call (vertices written directly)")
        return f"{n} {verts} {tail}"

    def polygon_call(self, shape: int) -> tuple | None:
        """The SetAsBox/Set call that produced this shape, if its current contents still match it
        (stack shapes are reused, so a stale record must not be trusted)."""
        call = self.shape_calls.get(shape)
        if call is None:
            return None
        n = self.emu.u32(shape + 0x94)
        if call[0] == "box":
            ok = n == 4 and self.v(shape + 0x14 + 16) == f"{call[1]} {call[2]}"       # vertex 2 = (hx, hy)
        elif call[0] == "boxc":
            ok = n == 4 and self.v(shape + 0xC) == call[3]                            # centroid = center
        else:
            ok = True
        return call if ok else None

    def shape_kind(self, shape: int) -> str:
        if self.emu.u32(shape + 4) == 0:
            return "circle"
        call = self.polygon_call(shape)
        return call[0] if call is not None and call[0] in ("box", "boxc") else "polygon"

    # -- hooks (all return False: the real function still runs; on_handle_get replaces the callee)
    def on_body(self, e: H.Emu) -> bool:
        this, d = e.args(2)
        self.body_index[this] = len(self.bodies)
        self.bodies.append(this)
        flags = " ".join(self.b(d + 0x24 + i) for i in range(5))   # allowSleep awake fixedRot bullet active
        self.lines.append(f"body {self.idx(this)} {e.u32(d)} {self.v(d + 4)} {self.f(d + 0xC)} {self.v(d + 0x10)} "
                          f"{self.f(d + 0x18)} {self.f(d + 0x1C)} {self.f(d + 0x20)} {flags}")
        return False

    def on_fixture(self, e: H.Emu) -> bool:
        body, d = e.args(2)
        shape = e.u32(d)
        tail = self.fixture_tail(self.f(d + 0x10), self.f(d + 8), self.f(d + 0xC), self.b(d + 0x14), d + 0x16)
        self.lines.append(f"{self.shape_kind(shape)} {self.idx(body)} {self.shape_line(shape, tail)}")
        self.snapshot_shape(body, shape)
        return False

    def snapshot_shape(self, body: int, shape: int) -> None:
        """Remember the polygon as created so run() can detect in-place edits made afterwards."""
        e = self.emu
        data = e.read(shape + 8, POLY_BYTES) if e.u32(shape + 4) == 2 else b""
        self.fixture_shapes.setdefault(self.idx(body), []).append(data)

    def polygon_fixes(self) -> list[str]:
        """Compare every live polygon fixture with its creation snapshot; the game edits some fixtures'
        vertices directly after CreateFixture (boxing glove arm), which the replay must reproduce."""
        e = self.emu
        out = []
        for idx, body in enumerate(self.bodies):
            if idx in self.dead:
                continue
            live = []
            f = e.u32(body + BODY_FIXTURE_LIST)
            while f:
                live.append(e.u32(f + 0xC))          # b2Fixture::m_shape
                f = e.u32(f + 4)                     # m_next (the list is prepended → reverse creation order)
            live.reverse()
            snaps = self.fixture_shapes.get(idx, [])
            for k, shape in enumerate(live):
                if e.u32(shape + 4) != 2 or k >= len(snaps):
                    continue
                if e.read(shape + 8, POLY_BYTES) != snaps[k]:
                    n = e.u32(shape + 0x94)
                    verts = " ".join(self.v(shape + 0x14 + i * 8) for i in range(n))
                    normals = " ".join(self.v(shape + 0x54 + i * 8) for i in range(n))
                    out.append(f"polygonfix {idx} {k} {n} {verts} {normals} {self.v(shape + 0xC)}")
        return out

    def on_fixture_simple(self, e: H.Emu) -> bool:
        body, shape, density = e.args(3)
        tail = self.fixture_tail(fbits(H.f32(density)), fbits(0.2), fbits(0.0), "0", None)
        self.lines.append(f"{self.shape_kind(shape)} {self.idx(body)} {self.shape_line(shape, tail)}")
        self.snapshot_shape(body, shape)
        return False

    def on_set_as_box(self, e: H.Emu) -> bool:
        this, hx, hy = e.args(3)
        self.shape_calls[this] = ("box", fbits(H.f32(hx)), fbits(H.f32(hy)))
        return False

    def on_set_as_box_c(self, e: H.Emu) -> bool:
        this, hx, hy, center = e.args(4)
        angle_bits = e.u32(e.reg(UC_ARM_REG_SP))   # softfp: r0-r3 = this, hx, hy, &center; angle on the stack
        self.shape_calls[this] = ("boxc", fbits(H.f32(hx)), fbits(H.f32(hy)), self.v(center), fbits(H.f32(angle_bits)))
        return False

    def on_set_vertices(self, e: H.Emu) -> bool:
        this, _verts, _n = e.args(3)
        self.shape_calls[this] = ("polygon",)
        return False

    def on_joint(self, e: H.Emu) -> bool:
        _world, d = e.args(2)
        jtype = e.u32(d)
        a, b_ = self.idx(e.u32(d + 8)), self.idx(e.u32(d + 0xC))
        head = f"{a} {b_} {self.b(d + 0x10)} {self.v(d + 0x14)} {self.v(d + 0x1C)}"
        f, v, b = self.f, self.v, self.b
        if jtype == 1:
            line = (f"revolute {head} {f(d + 0x24)} {b(d + 0x28)} {f(d + 0x2C)} {f(d + 0x30)} "
                    f"{b(d + 0x34)} {f(d + 0x38)} {f(d + 0x3C)}")
        elif jtype == 2:
            line = (f"prismatic {head} {v(d + 0x24)} {f(d + 0x2C)} {b(d + 0x30)} {f(d + 0x34)} {f(d + 0x38)} "
                    f"{b(d + 0x3C)} {f(d + 0x40)} {f(d + 0x44)}")
        elif jtype == 3:
            # The trunk's def carries an extra bool at +0x30 (04 §1): 0 = one-sided "rope" behaviour.
            line = f"distance {head} {f(d + 0x24)} {f(d + 0x28)} {f(d + 0x2C)} {b(d + 0x30)}"
        elif jtype == 7:
            line = f"wheel {head} {v(d + 0x24)} {b(d + 0x2C)} {f(d + 0x30)} {f(d + 0x34)} {f(d + 0x38)} {f(d + 0x3C)}"
        else:
            raise RuntimeError(f"unsupported joint type {jtype}")
        self.lines.append(line)
        # Capture the created joint pointer when CreateJoint returns (one-shot hook at the return address).
        ret = e.reg(UC_ARM_REG_LR)
        if ret not in self.hooked_returns:
            self.hooked_returns.add(ret)
            e.hook_function(ret, self.on_joint_return)
        self.pending_joint_return = ret
        return False

    def on_joint_return(self, e: H.Emu) -> bool:
        if self.pending_joint_return == e.reg(UC_ARM_REG_PC):
            self.joints.append(e.reg(UC_ARM_REG_R0))
            self.pending_joint_return = None
        return False

    def on_destroy_joint(self, e: H.Emu) -> bool:
        if self.in_destroy_body:            # implicit: the replayer's DestroyBody removes the same joints
            return False
        _world, joint = e.args(2)
        # b2BlockAllocator reuses a destroyed joint's block: the pointer names its newest joint.
        index = len(self.joints) - 1 - self.joints[::-1].index(joint)
        self.lines.append(f"destroyjoint {index}")
        return False

    def on_destroy_body(self, e: H.Emu) -> bool:
        _world, body = e.args(2)
        idx = self.idx(body)
        self.lines.append(f"destroybody {idx}")
        self.dead.add(idx)
        self.in_destroy_body = True
        ret = e.reg(UC_ARM_REG_LR)
        if ret not in self.hooked_returns:
            self.hooked_returns.add(ret)
            e.hook_function(ret, self.on_destroy_body_return)
        return False

    def on_destroy_body_return(self, e: H.Emu) -> bool:
        self.in_destroy_body = False
        return False

    def on_mass(self, e: H.Emu) -> bool:
        body, md = e.args(2)
        self.lines.append(f"mass {self.idx(body)} {self.f(md)} {self.v(md + 4)} {self.f(md + 0xC)}")
        return False

    def on_transform(self, e: H.Emu) -> bool:
        body, pos, angle = e.args(3)
        self.lines.append(f"transform {self.idx(body)} {self.v(pos)} {fbits(H.f32(angle))}")
        return False

    def on_handle_get(self, e: H.Emu) -> bool:
        # The item block starts as the per-type default that GameItemCollectionUtils::InsertWithHandle
        # copies from st::ItemInfos (scissors cut angle 15°, glove button height, default end vectors…),
        # then the scene's own state (level end vectors, book colour) is written over it. One block per
        # fake handle, kept for the scene's lifetime (see `items`).
        _mgr, handle = e.args(2)
        if handle in self.items:
            e.ret(self.items[handle])
            return True
        item_type, state_init = self.item_meta.get(handle, (self.item_type, self.state_init))
        buf = e.malloc(0x2000, zero=True)
        info = e.syms.by_name["st::ItemInfos"] + item_type * ITEM_INFO_STRIDE
        e.write(buf, e.read(e.u32(info + 0xC), e.u32(info + 8)))
        if state_init is not None:
            state_init(e, buf)
        if handle != 0:
            self.items[handle] = buf
        e.ret(buf)
        return True


def end_vector(dx: float, dy: float) -> StateInit:
    """Rope / ZipLine: the second end relative to the item centre (`Rope+8/+0xC`, exactly what
    `LevelLayoutUtils::Apply` copies from the layout's `ropeEndPos`, which is stored relative — 02 §3.4)."""
    def init(e: H.Emu, buf: int) -> None:
        e.wf32(buf + 8, dx)
        e.wf32(buf + 0xC, dy)
    return init


def pouch_vector(dx: float, dy: float) -> StateInit:
    """Slingshot: the pouch relative to the item centre, at `Slingshot+0xC/+0x10` (Apply case 0x22)."""
    def init(e: H.Emu, buf: int) -> None:
        e.wf32(buf + 0xC, dx)
        e.wf32(buf + 0x10, dy)
    return init


def word_state(value: int) -> StateInit:
    def init(e: H.Emu, buf: int) -> None:
        e.w32(buf + 8, value)
    return init


class Scene:
    """An emulated b2World plus the game's PhysicsObject collection."""

    def __init__(self, emu: H.Emu, rec: SceneRecorder, capacity: int, ccd: bool = True,
                 mode: int = MODE_SIMULATION, world_state: bool = False) -> None:
        """`world_state`: lay the collection and the handle manager out inside a WorldState-shaped block
        (`self.world_state`) so the functions taking a `st::WorldState&` (the set-up oracle) can be called."""
        self.emu, self.rec = emu, rec
        self.mode = mode
        rec.lines, rec.bodies, rec.body_index, rec.shape_calls = [], [], {}, {}
        rec.dead, rec.joints, rec.pending_joint_return, rec.in_destroy_body = set(), [], None, False
        rec.fixture_shapes = {}
        rec.items, rec.item_meta = {}, {}
        self.world = emu.malloc(WORLD_SIZE, zero=True)
        gravity = emu.malloc(8)
        emu.wf32(gravity, 0.0)
        emu.wf32(gravity + 4, -9.8)
        emu.call("b2World::b2World(b2Vec2 const&, bool)", self.world, gravity, 1)
        # GamePhysicsUtils::CreateWorld clears e_clearForces (auto clear off, ClearForces called by hand)
        emu.w32(self.world + WORLD_FLAGS_OFF, emu.u32(self.world + WORLD_FLAGS_OFF) & ~4)
        rec.lines.append(f"gravity {fbits(0.0)} {fbits(-9.8)}")
        if not ccd:   # experiment switch: continuous collision off on both sides
            emu.write(self.world + WORLD_CCD_OFF, b"\x00")
            rec.lines.append("continuous 0")
        self.world_state = 0
        if world_state:
            self.world_state = emu.malloc(WS_SIZE, zero=True)
            self.coll = self.world_state + WS_COLLECTION
            self.handle_mgr = self.world_state + WS_HANDLE_MANAGER
            emu.w32(self.world_state + WS_WORLD, self.world)
        else:
            self.coll = emu.malloc(0x10 + capacity * OBJ_STRIDE, zero=True)
            self.handle_mgr = emu.malloc(0x100, zero=True)
        self.objects: list[tuple[int, int]] = []   # (item type, PhysicsObject address) in add order

    def add(self, item_type: int, x: float, y: float, angle: float = 0.0, flipped: bool = False,
            state: StateInit = None) -> str:
        e = self.emu
        idx = e.call("st::PhysicsObjectsUtils::Add(st::PhysicsObjectCollection&, st::ItemType::Enum)", self.coll, item_type)
        obj = self.coll + 8 + idx * OBJ_STRIDE
        e.wf32(obj + 0x10, x)
        e.wf32(obj + 0x14, y)
        e.wf32(obj + 0x18, angle)
        if flipped:
            e.wf32(obj + 0x1C, -e.f32_at(obj + 0x1C))
        e.w32(obj + 8, idx + 1)          # fake handle → the object's persistent GameItem block
        self.objects.append((item_type, obj))
        self.rec.state_init = state
        self.rec.item_type = item_type
        self.rec.item_meta[idx + 1] = (item_type, state)
        first = len(self.rec.bodies)
        try:
            e.call("st::PhysicsObjectUtils::CreatePhysics(st::PhysicsObject&, b2World&, st::HandleManager const&, st::PhysicsMode::Enum)",
                   obj, self.world, self.handle_mgr, self.mode)
            # Body ownership comment: lets trace_compare name the diverging item instead of a body index.
            self.rec.lines.append(f"# item {item_type} {ITEM_NAMES.get(item_type, '?')} bodies {first}..{len(self.rec.bodies) - 1}")
            return "ok"
        except (UcError, RuntimeError) as exc:
            pc = e.uc.reg_read(UC_ARM_REG_PC)
            return f"failed: {exc} at {pc:#x} {e.describe(pc)}"

    def attach(self, records: list[tuple[int, int, int, int, int]], rope_ends: dict[int, tuple[float, float]]) -> None:
        """Re-create the level's attachments the way `GamePhysicsUtils::CreateAttachments` does on load.

        `records` = (object, point, state, other object, other point) for every attachment record of the
        layout (`LevelLayoutUtils::Apply` copies all of them, whatever their state — a snapped record (1)
        changes what `RopeUtils::UpdatePosFromAttachedObjects` does); a joint is created for the records in
        state 2 (attached), in collection order, unless the other side already made it; `rope_ends` = end
        vector per rope object (item +8/+0xC) for every rope of the level, in item order — the original walks
        all its ropes. `GameItemUtils::AttachmentChanged` runs as in the game: a rope attached at both ends
        gets its end-to-end distance joint (FUN_000e2b48) and the mass override (M3 finding — M2 skipped it).
        """
        e = self.emu
        for obj_i, point, state, other_i, other_point in records:
            base = self.objects[obj_i][1] + ATTACH_BASE + point * ATTACH_STRIDE
            e.w32(base + 0x20, state)
            e.w32(base + 0x24, other_i)
            e.w32(base + 0x28, other_point)
            e.w32(base + 0x2C, 0)
        for obj_i, point, state, _other_i, _other_point in records:
            obj = self.objects[obj_i][1]
            if state == 2 and e.u32(obj + ATTACH_BASE + point * ATTACH_STRIDE + 0x2C) == 0:
                e.call("st::AttachmentUtils::CreateJoint(st::PhysicsObject&, int, st::PhysicsObjectCollection&, st::HandleManager const&)",
                       obj, point, self.coll, self.handle_mgr)
        for obj_i, (dx, dy) in rope_ends.items():
            item = self.item_block(obj_i)         # st::Rope: +4 object index, +8/+0xC end vector
            e.w32(item + 4, obj_i)
            e.wf32(item + 8, dx)
            e.wf32(item + 0xC, dy)
            bodies_before = len(self.rec.bodies)
            e.call("st::RopeUtils::UpdatePosFromAttachedObjects(st::Rope&, st::PhysicsObject&, st::PhysicsObjectCollection const&)",
                   item, self.objects[obj_i][1], self.coll)
            if len(self.rec.bodies) != bodies_before:
                log.info("rope object %d rebuilt: %d links now", obj_i, len(self.rec.bodies) - bodies_before)

    def item_block(self, obj_i: int) -> int:
        """The persistent GameItem block of object `obj_i` (created by the HandleManager::Get hook)."""
        handle = obj_i + 1
        if handle not in self.rec.items:
            self.emu.call("st::HandleManager::Get(st::Handle) const", self.handle_mgr, handle)
        return self.rec.items[handle]

    def run(self, steps: int) -> list[str]:
        e = self.emu
        step = e.syms.by_name["b2World::Step(float, int, int)"]
        clear = e.syms.by_name["b2World::ClearForces()"]
        bodies = self.rec.bodies
        self.rec.lines.extend(self.rec.polygon_fixes())
        out = [f"# amazing-alex trajectory v1 bodies={len(bodies)} steps={steps}"]
        out += [l for l in self.rec.lines if l.startswith("# item ")]
        dt = H.i32(STEP_DT)
        for s in range(1, steps + 1):
            e.call(step, self.world, dt, VEL_ITERS, POS_ITERS)
            e.call(clear, self.world)
            for i, b in enumerate(bodies):
                if i in self.rec.dead:
                    continue
                px, py = struct.unpack("<ff", e.read(b + BODY_POS, 8))
                (a,) = struct.unpack("<f", e.read(b + BODY_ANGLE, 4))
                vx, vy = struct.unpack("<ff", e.read(b + BODY_LINVEL, 8))
                (w,) = struct.unpack("<f", e.read(b + BODY_ANGVEL, 4))
                awake = 1 if e.u32(b + BODY_FLAGS) & BODY_AWAKE_FLAG else 0
                out.append(f"{s} {i} {px:.9g} {py:.9g} {a:.9g} {vx:.9g} {vy:.9g} {w:.9g} {awake}")
        self.rec.lines.append(f"step {steps} {fbits(STEP_DT)} {VEL_ITERS} {POS_ITERS}")
        return out


def make_emulator(so_path: str, plist_path: str) -> tuple[H.Emu, SceneRecorder]:
    emu = H.make_emu(so_path)
    frames = build_frames(emu, plist_path)
    emu.call("st::PhysicsObjectsUtils::InitializePhysicsObjectTemplates(st::CountedArray<st::Frame> const&)", frames)
    emu.call("st::CollisionFiltersUtils::Create()")
    return emu, SceneRecorder(emu)


def item_state(item_type: int, item: dict, background_index: int) -> StateInit:
    if item_type in (9, 42):         # rope, zip line: end B relative to end A, as stored in the file
        return end_vector(item["ropeEndPos_x"], item["ropeEndPos_y"])
    if item_type == 34:              # slingshot: the pouch relative to the frame
        return pouch_vector(item["ropeEndPos_x"], item["ropeEndPos_y"])
    if item_type == 15:              # book colour
        return word_state(int(item["itemData"]))
    if item_type == 31:              # world bound: 1 = Treehouse floor with the hole
        return word_state(1 if background_index == TREEHOUSE_BACKGROUND else 0)
    return None


def drop_scene(emu: H.Emu, rec: SceneRecorder, item_type: int, x: float, y: float, steps: int,
               ccd: bool = True, mode: int = MODE_SIMULATION, flipped: bool = False,
               background: int = 0, state: int | None = None) -> tuple[str, list[str], list[str]]:
    """World bound (background 0) as body 0, then the item; `background`/`state` select the item's own
    state word (a type-31 item with background 3 is the Treehouse floor — the body-0 bound stays plain)."""
    scene = Scene(emu, rec, capacity=4, ccd=ccd, mode=mode)
    scene.add(31, 0.0, 0.0, state=word_state(0))
    variants = {9: end_vector(0.6, 0.0), 34: pouch_vector(0.0, 0.5), 42: end_vector(0.8, -0.3), 15: word_state(0)}
    init = variants.get(item_type)
    if item_type == 31:
        init = word_state(1 if background == TREEHOUSE_BACKGROUND else 0)
    elif state is not None:
        init = word_state(state)
    status = scene.add(item_type, x, y, flipped=flipped, state=init)
    traj = scene.run(steps)
    return status, list(rec.lines), traj


def level_scene(emu: H.Emu, rec: SceneRecorder, level: dict, steps: int,
                ccd: bool = True, mode: int = MODE_SIMULATION) -> tuple[list[str], list[str], list[str]]:
    scene = Scene(emu, rec, capacity=int(level["itemCount"]) + 2, ccd=ccd, mode=mode)
    statuses = populate_level(scene, level)
    traj = scene.run(steps)
    return statuses, list(rec.lines), traj


def populate_level(scene: Scene, level: dict) -> list[str]:
    """Adds a level's items (plus a world bound when the level has none) and its attachments to `scene`."""
    count = int(level["itemCount"])
    background = int(level.get("backgroundIndex", 0))
    statuses = []
    items = [level[f"itemInfos_{i}"] for i in range(count)]
    if not any(int(it["type"]) == 31 for it in items):
        scene.add(31, 0.0, 0.0, state=word_state(1 if background == TREEHOUSE_BACKGROUND else 0))
    offset = len(scene.objects)          # object index = itemInfos index + offset
    for it in items:
        t = int(it["type"])
        if t == LEGACY_HELI_CONTROLLER:
            t = RC_CONTROLLER            # LevelLayoutUtils::LoadPlist conversion (02 §5)
        status = scene.add(t, float(it["center_x"]), float(it["center_y"]), float(it["angle"]),
                           flipped=bool(int(it["flags"]) & 2), state=item_state(t, it, background))
        statuses.append(f"{ITEM_NAMES.get(t, t)}: {status}")
    records = []
    rope_ends = {}
    for i, it in enumerate(items):
        for k in range(int(it.get("attachmentCount", 0))):
            a = it[f"attachments_{k}"]
            other = int(a["objectIndex"])
            records.append((i + offset, k, int(a["state"]), other + offset if other >= 0 else -1, int(a["index"])))
        if int(it["type"]) == 9:
            rope_ends[i + offset] = (it["ropeEndPos_x"], it["ropeEndPos_y"])
    if records or rope_ends:
        scene.attach(records, rope_ends)
        statuses.append(f"attachments: {len(records)} records, {len(rope_ends)} ropes")
    return statuses


def write(path: Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("so_path")
    parser.add_argument("plist_path")
    parser.add_argument("--out", required=True)
    parser.add_argument("--drop", help="'all' or comma-separated item types to drop onto the floor")
    parser.add_argument("--level", action="append", default=[], help="decrypted level plist (repeatable)")
    parser.add_argument("--steps", type=int, default=600)
    parser.add_argument("--x", type=float, default=1.7)
    parser.add_argument("--y", type=float, default=1.0)
    parser.add_argument("--no-ccd", action="store_true", help="experiment: disable continuous collision on both sides")
    parser.add_argument("--mode", choices=sorted(MODE_NAMES), help="physics mode (default simulation)")
    parser.add_argument("--flip", action="store_true", help="drop the item flipped (scale.x = -1)")
    parser.add_argument("--background", type=int, default=0, help="world-bound variant of a dropped type 31")
    parser.add_argument("--state", type=int, help="per-type state word of the dropped item (book colour)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    conformance = args.mode is not None
    mode = MODE_NAMES[args.mode or "simulation"]
    steps = args.steps
    if mode == MODE_SETUP and steps == parser.get_default("steps"):
        steps = 0

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    emu, rec = make_emulator(args.so_path, args.plist_path)

    if args.drop:
        types = sorted(set(range(1, 43)) - DROP_TYPES_NO_STATE) if args.drop == "all" else [int(t) for t in args.drop.split(",")]
        for t in types:
            name = f"drop_{t:02d}_{ITEM_NAMES[t]}"
            if conformance:
                name += f"_{args.mode}" + ("_flip" if args.flip else "") + \
                    (f"_bg{args.background}" if t == 31 and args.background else "") + \
                    (f"_s{args.state}" if args.state is not None else "")
            status, scene_lines, traj = drop_scene(emu, rec, t, args.x, args.y, steps, ccd=not args.no_ccd, mode=mode,
                                                   flipped=args.flip, background=args.background, state=args.state)
            scene_lines.insert(0, f"# amazing-alex physics scene v1 {name} ({status})")
            write(out / f"{name}.scene", scene_lines)
            write(out / f"{name}.uc.traj", traj)
            log.info("%s: %s, %d bodies", name, status, len(rec.bodies))

    for lvl in args.level:
        path = Path(lvl)
        with path.open("rb") as fh:
            level = plistlib.load(fh)
        name = f"level_{path.parent.name}_{path.stem}" + ("_setup" if mode == MODE_SETUP else "")
        statuses, scene_lines, traj = level_scene(emu, rec, level, steps, ccd=not args.no_ccd, mode=mode)
        items = [s for s in statuses if not s.startswith("attachments:")]   # the attachment line is a note
        bad = [s for s in items if not s.endswith(": ok")]
        scene_lines.insert(0, f"# amazing-alex physics scene v1 {name} ({len(items)} items, {len(bad)} failed)")
        for s in statuses:
            if s in bad or s.startswith("attachments:"):
                scene_lines.insert(1, f"# {s}")
        write(out / f"{name}.scene", scene_lines)
        write(out / f"{name}.uc.traj", traj)
        log.info("%s: %d items (%d failed), %d bodies", name, len(statuses), len(bad), len(rec.bodies))
    if rec.raw_polygons:
        log.warning("%d polygon fixtures had no SetAsBox/Set call and were emitted as raw vertex lists", rec.raw_polygons)


if __name__ == "__main__":
    main()
