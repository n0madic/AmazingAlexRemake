#!/usr/bin/env python3
"""G4 / G6 simulation oracle: runs a level's simulation on the original binary (Unicorn).

A real `st::GameState` is constructed (`GameState::GameState`, the localisation calls stubbed), the level is
serialised into a `st::LevelLayout` block and applied with the game's own `LevelLayoutUtils::Apply` — so the
`GameItemCollection`, the `HandleManager` and the `PhysicsObjectCollection` are the real ones the item
`Update` functions read through `GetStartOfType`. The level is then played the way the game does it: the
set-up world is built (`CreateWorld(0)`, `CreateDynamicPhysics`, `CreateAttachments` — the rope ends move
onto their attached objects), `prepareForNewLevel` fixes every item and adds the SelectionArea object, and
`toggleSimulation` takes the play layout with `LevelLayoutUtils::Get`, destroys the world, re-applies the
layout (`PartialReset` + `Apply`) and builds the simulation world with `CreateWorld(gs, queue, 1)`,
`CreateDynamicPhysics(1)` and `CreateAttachments` (recorded as a `.scene`, the G3 format). Every frame then
replays `GameScreenController::doFrame` case 4 and its tail, with
`GameScreen::UpdateSimulation` transcribed instruction by instruction from the disassembly (docs/04 §3):

  processActions (ProcessSimulationAction for the world-side ids; 11 / 0x15 / 0x18 handled here)
  acc += dt · 0.8
  while acc ≥ 1/120: prev ← ws; Balloon…Seesaw ::Update(1/120); Step(1/120, 10, 10); ClearForces;
      GetStateFromPhysics; acc −= 1/120; Scissors / GoalStar ::Update; processActions
  PiggyBank::Update(acc₀); Dart::UpdateSetUpMode(acc₀); GoalStateUtils::Update(acc₀) + IsGoalComplete → 11
  render ← ws; LerpState(render, prev, ws, acc / (1/120))
  the completion countdown (2.05 s after action 11) / play time
  StopRunawayObjects; RemoveInvalidItems; prev ← ws; the 5 s no-motion auto-stop

(`acc₀` is the accumulator *before* the substep loop — `s18` in the listing — not the frame dt.) The
`ActionProcessor` of the original is `GameScreenController::processActions`; here the queue is drained in
Python with the same rules, calling `GameScreen::ProcessSimulationAction` for ids 7 / 12 / 17 / 18 / 19 and
recording 13 (sound) without playing it.

Script (one command per line, `#` comments):
  level <chapter>/<name>        the level (must come first)
  frames <n>                    run n frames of 1/60 s
  tap                           a touch outside every button: stop on the next frame (action 0x15)
  goal                          queue action 11 as a reached goal would (the completion path)
  seed <n>                      GameState Random seed before the run (default: the constructor's 1)
  listener off                  detach the contact listener (WP0 plumbing check)

Dumps: `<script>.scene` (construction), `<script>.uc.traj` (`step body px py a vx vy w awake` per
substep for every live body, substeps numbered globally from 1) and `<script>.sim`:
  frame <n> <acc bits> <play time bits> <goal reached> <stars> <state>
  random <seed>                                                   the GameState Random after the frame
  action <frame> <substep|-> <id> <handle> <payload words…>      as processed, in order
  obj <i> <type> <handle> <x> <y> <angle> <flags> <state> <bodies>   after the frame
  lerp <i> <x> <y> <angle>                                        the render copy (dynamic objects)
  item <type> <handle> <name>=<bits> …                            per-type fields (ITEM_FIELDS)
  goalstate <11 words>
  removed <object index>                                          objects dropped by RemoveInvalidItems

Usage: uc_sim_oracle.py <lib.so> <GameItems.plist> --levels DIR --script S.txt [--script …] --out DIR
"""
from __future__ import annotations

import argparse
import json
import logging
import struct
import sys
from pathlib import Path

from unicorn import UcError
from unicorn.arm_const import UC_ARM_REG_PC

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uc_harness as H  # noqa: E402
import uc_trace as T  # noqa: E402
from uc_dump_physics import ITEM_NAMES  # noqa: E402

log = logging.getLogger("uc_sim_oracle")

# --- st::GameState ------------------------------------------------------------------------------------
GS_SIZE = 0x60000
GS_PLAY_TIME = 0x0
GS_TIME_SCALE = 0x4
GS_GOAL_STATE = 0x808
GS_LEVEL_INFO = 0x23a4
GS_GOAL = 0x25b4
GS_WORLD_STATE = 0x2618
GS_COLLECTION = GS_WORLD_STATE + T.WS_COLLECTION
GS_HANDLE_MANAGER = GS_WORLD_STATE + T.WS_HANDLE_MANAGER
GS_B2WORLD = 0x318c4
GS_LISTENER = 0x318c8
GS_VISUAL = 0x318cc
GS_RANDOM = 0x575e0
GOAL_STATE_WORDS = 11
# --- st::WorldState / GameItemCollection ----------------------------------------------------------------
WS_TYPE_START = 0x4          # + type·4: byte offset of the type's run
WS_TYPE_COUNT = 0xb0         # + type·4: live items of the type
WS_ITEMS = 0x15c
WS_OBJECT_COUNT = 0x20160
WS_OBJECTS = 0x20164
OBJ_TYPE, OBJ_HANDLE, OBJ_FLAGS, OBJ_STATE, OBJ_POS, OBJ_ANGLE, OBJ_BODY_COUNT, OBJ_BODIES = 0, 8, 0xc, 0xd, 0x10, 0x18, 0x94, 0x98
# --- st::LevelLayout (0x2400) ----------------------------------------------------------------------------
LAYOUT_SIZE = 0x2400
LAYOUT_TITLE, LAYOUT_DESCRIPTION, LAYOUT_AUTHOR = 0x4, 0x44, 0x144
LAYOUT_BACKGROUND = 0x204
LAYOUT_TOOLBOX_COUNT, LAYOUT_TOOLBOX = 0x208, 0x20c
LAYOUT_ITEM_COUNT, LAYOUT_ITEMS, LAYOUT_ITEM_STRIDE = 0x40c, 0x410, 0x40
LAYOUT_GOAL = 0x2390
LAYOUT_REWARD, LAYOUT_TESTED = 0x23f4, 0x23f8
ITEM_ATTACHMENTS, ITEM_ATTACHMENT_STRIDE = 0x28, 0xc
# --- st::ActionQueue / st::Action ----------------------------------------------------------------------
QUEUE_SIZE = 0x804
ACTION_STRIDE = 0x20
ACTION_WORDS = 8
SIM_ACTION_IDS = (7, 0xc, 0xd, 0x11, 0x12, 0x13)
ACTION_GOAL_COMPLETE, ACTION_SOUND, ACTION_SIM_TOUCH, ACTION_SKIP_ANIMS = 0xb, 0xd, 0x15, 0x18
SELECTION_AREA = 40
GOAL_STAR = 23
CONTROLLER_SIMULATION, CONTROLLER_COMPLETED = 4, 6

STEP_DT_BITS = H.i32(1.0 / 120.0)            # 0x3c088889
FRAME_DT = 1.0 / 60.0
SPEED = 0.8
COMPLETION_COUNTDOWN = 0x40033333             # 2.05 s (campaign)
IDLE_STOP_SECONDS = 5.0

# Per-type item block fields the core models (name, byte offset, kind: f float bits / i int / b byte);
# extended as the item ports land (docs/03 §2). Sound clip handles (SoundRenderer::Render) are not listed.
ITEM_FIELDS: dict[int, list[tuple[str, int, str]]] = {
    5: [("popped", 0x8, "b"), ("t", 0xc, "f")],                                                        # Balloon
    6: [("state", 0x8, "i"), ("cut", 0xc, "f"), ("step", 0x10, "i"), ("timer", 0x14, "f"), ("snipping", 0x18, "b"),
        ("angle", 0x1c, "f"), ("phase", 0x20, "f"), ("dir", 0x24, "f")],                                  # Scissors
    13: [("t", 0x8, "f")],                                                                             # PiggyBank
    14: [("state", 0x8, "i"), ("button", 0x20, "f"), ("lattice", 0x24, "f")],                          # BoxingGlove
    23: [("state", 0x8, "i"), ("t", 0xc, "f")],                                                        # GoalStar
    25: [("pulling", 0x8, "b"), ("min", 0xc, "f"), ("timer", 0x14, "f"), ("frame", 0x18, "i")],        # Magnet
    29: [("wobbling", 0x8, "b"), ("stuck", 0x9, "b"), ("angle", 0xc, "f"), ("phase", 0x10, "f"), ("dir", 0x14, "f"),
         ("timer", 0x18, "f")],                                                                        # Dart
    33: [("on", 0x8, "i"), ("t", 0xc, "f")],                                                           # Bumper
    34: [("fired", 0x8, "b"), ("pouchx", 0xc, "f"), ("pouchy", 0x10, "f"), ("velx", 0x14, "f"), ("vely", 0x18, "f"),
         ("loaded", 0x20, "i")],                                                                       # Slingshot
    22: [("dir", 0x8, "i")],                                                                            # Seesaw
    36: [("pressed", 0x10, "b")],                                                                      # RadioController
    38: [("unlocked", 0xc, "b"), ("sounded", 0xd, "b")],                                                # TrapdoorLever
    39: [("on", 0x10, "b"), ("throttle", 0x14, "f"), ("rotor", 0x1c, "f"), ("rotorphase", 0x20, "f"), ("tail", 0x24, "f"),
         ("tailphase", 0x28, "f")],                                                                    # Helicopter
}
IDLE_TIMER_TYPES = {6: 0x14, 29: 0x18}   # Scissors+0x14, Dart+0x18: the address-seeded first idle timer
IDLE_TIMER_RANGE = (3.0, 10.0)           # DAT_0029179c.., DAT_00283174..


def lcg_get_float(seed: int, lo: float, hi: float) -> float:
    """st::Random::GetFloat after SetSeed(seed): CustomRand then lo + (r / 32767) · (hi − lo) in float32."""
    seed = (seed * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
    r = (seed & 0x7FFFFFFF) >> 16
    return f32u(lo + f32u(f32u(float(r) / 32767.0) * f32u(hi - lo)))


def fbits(x: float) -> str:
    return "0x%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def f32u(x: float) -> float:
    """Round a Python float to float32 precision."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def s32(v: int) -> int:
    return v - (1 << 32) if v & 0x80000000 else v


class LayoutBuilder:
    """Serialises a level JSON (tools/import_assets.py, docs/12 §2) into a st::LevelLayout block (the
    loader's output, docs/02)."""

    def __init__(self, level: dict) -> None:
        self.level = level

    def items(self) -> list[dict]:
        return [dict(it) for it in self.level["items"]]

    def build(self) -> bytes:
        lv = self.level
        buf = bytearray(LAYOUT_SIZE)
        struct.pack_into("<i", buf, 0, 7)
        for off, size, key in ((LAYOUT_TITLE, 0x40, "title"), (LAYOUT_DESCRIPTION, 0x100, "description"),
                               (LAYOUT_AUTHOR, 0x40, "authorName")):
            s = str(lv.get(key, "")).encode("latin-1", "replace")[: size - 1]
            buf[off:off + len(s)] = s
        struct.pack_into("<i", buf, LAYOUT_BACKGROUND, int(lv.get("backgroundIndex", 0)))
        toolbox = lv.get("toolbox", [])
        struct.pack_into("<i", buf, LAYOUT_TOOLBOX_COUNT, len(toolbox))
        for i, slot in enumerate(toolbox):
            struct.pack_into("<ii", buf, LAYOUT_TOOLBOX + i * 8, int(slot["type"]), int(slot["amount"]))
        items = self.items()
        struct.pack_into("<i", buf, LAYOUT_ITEM_COUNT, len(items))
        for i, it in enumerate(items):
            base = LAYOUT_ITEMS + i * LAYOUT_ITEM_STRIDE
            struct.pack_into("<iI", buf, base, int(it["type"]), int(it["handle"]) & 0xffffffff)
            struct.pack_into("<ff", buf, base + 0x8, f32u(it["center"][0]), f32u(it["center"][1]))
            struct.pack_into("<f", buf, base + 0x10, f32u(it["angle"]))
            struct.pack_into("<i", buf, base + 0x14, int(it["flags"]))
            struct.pack_into("<ff", buf, base + 0x18, f32u(it["ropeEnd"][0]), f32u(it["ropeEnd"][1]))
            struct.pack_into("<i", buf, base + 0x20, int(it["itemData"]))
            atts = it.get("attachments", [])
            struct.pack_into("<i", buf, base + 0x24, len(atts))
            for k, a in enumerate(atts[:2]):
                struct.pack_into("<iii", buf, base + ITEM_ATTACHMENTS + k * ITEM_ATTACHMENT_STRIDE,
                                 int(a["state"]), int(a["objectIndex"]), int(a["index"]))
        goal = lv["goal"]
        struct.pack_into("<ii", buf, LAYOUT_GOAL, int(goal["type"]), int(goal["itemCount"]))
        for k, key in ((0x8, "itemHandles"), (0x2c, "itemHandles2")):
            for i, h in enumerate(goal.get(key, [])[:9]):
                struct.pack_into("<I", buf, LAYOUT_GOAL + k + i * 4, int(h) & 0xffffffff)
        struct.pack_into("<ifff", buf, LAYOUT_GOAL + 0x50, int(goal.get("timeLimit", 0)), f32u(goal["height"]),
                         f32u(goal["width"]), f32u(goal["angle"]))
        buf[LAYOUT_GOAL + 0x60] = 1 if goal.get("negated") else 0
        struct.pack_into("<i", buf, LAYOUT_REWARD, int(lv.get("rewardId", 0)))
        buf[LAYOUT_TESTED] = 1 if lv.get("tested") else 0
        return bytes(buf)


class SimOracle:
    def __init__(self, emu: H.Emu, rec: T.SceneRecorder, level: dict, listener: bool = True, seed: int | None = None) -> None:
        self.e = emu
        self.rec = rec
        self.level = level
        # The recorder's HandleManager::Get replacement serves the drop / set-up oracles, which have no
        # GameItemCollection; here the real one exists, so the hook must pass through.
        emu.hook_function("st::HandleManager::Get(st::Handle) const", lambda e: False)
        self.reset_recorder()
        self.gs = emu.malloc(GS_SIZE, zero=True)
        self.ws = self.gs + GS_WORLD_STATE
        self.coll = self.gs + GS_COLLECTION
        self.hm = self.gs + GS_HANDLE_MANAGER
        self.goal_state = self.gs + GS_GOAL_STATE
        self.random = self.gs + GS_RANDOM
        self.prev = emu.malloc(T.WS_SIZE, zero=True)
        self.render = emu.malloc(T.WS_SIZE, zero=True)
        self.queue = emu.malloc(QUEUE_SIZE, zero=True)
        self.action_buf = emu.malloc(ACTION_STRIDE, zero=True)
        self.dummy = emu.malloc(0x100, zero=True)
        self._stub_localisation()
        emu.call("st::GameState::GameState()", self.gs)
        if seed is not None:
            emu.w32(self.random, seed)
        layout = emu.malloc(LAYOUT_SIZE, zero=True)
        emu.write(layout, LayoutBuilder(level).build())
        # GameStateUtils::CreateNew / prepareForNewLevel: the set-up world, every item fixed, the SelectionArea.
        self.apply_and_build(layout, T.MODE_SETUP)
        emu.call("st::WorldStateUtils::MarkAllObjectsFixed(st::WorldState&)", self.ws)
        zero = emu.malloc(8, zero=True)
        item = emu.call("st::WorldStateUtils::AddNewItem(st::WorldState&, st::ItemType::Enum, st::Vec2 const&, float, bool)",
                        self.ws, SELECTION_AREA, zero, 0, 0)
        emu.call("st::PhysicsObjectUtils::CreatePhysics(st::PhysicsObject&, b2World&, st::HandleManager const&, st::PhysicsMode::Enum)",
                 self.obj(emu.u32(item + 4)), self.world, self.hm, T.MODE_SETUP)
        # toggleSimulation: the play layout from the live state, then restoreGameState(layout, simulation).
        play_layout = emu.malloc(LAYOUT_SIZE, zero=True)
        emu.call("st::LevelLayout::LevelLayout()", play_layout)
        emu.call("st::LevelLayoutUtils::Get(st::LevelLayout&, st::GameState const&)", play_layout, self.gs)
        self.play_layout = emu.read(play_layout, LAYOUT_SIZE)
        emu.call("st::GamePhysicsUtils::DestroyWorld(st::WorldState&)", self.ws)
        emu.call("st::WorldStateUtils::PartialReset(st::WorldState&)", self.ws)
        self.reset_recorder()
        self.apply_and_build(play_layout, T.MODE_SIMULATION)
        if not listener:
            emu.call("b2World::SetContactListener(b2ContactListener*)", self.world, 0)
        # The construction scene: the fixtures edited in place after CreateFixture are reported at the end,
        # as uc_trace's Scene.run does (the core's recorder emits them the same way).
        self.scene = list(rec.lines) + rec.polygon_fixes()
        self.traj: list[str] = []
        self.sim: list[str] = []
        self.acc = 0.0
        self.substep = 0
        self.frame = 0
        self.controller_state = CONTROLLER_SIMULATION
        self.completing = False
        self.countdown = 0.0
        self.idle_time = 0.0
        self.stop_requested = False
        self.stopped = False
        emu.write(self.prev, emu.read(self.ws, T.WS_SIZE))
        # Effects and audio: presentation only (docs/10 §1); skipped so nothing reads uninitialised
        # resources. VisualWorldState effects started by the item code write outside the physics state.
        for name in ("st::SoundSystemUtils::Play(st::AudioId::Enum, float, st::Vec2 const&, st::AudioSystem&)",
                     "st::SparkleEffectUtils::Start(st::SparkleEffect&, st::Vec2 const&)"):
            for sym, addr in emu.syms.find(name.split("(")[0]):
                emu.hook_function(addr, lambda e: (e.ret(0), True)[1])

    # -- construction helpers
    def reset_recorder(self) -> None:
        rec = self.rec
        rec.lines, rec.bodies, rec.body_index, rec.shape_calls = [], [], {}, {}
        rec.dead, rec.joints, rec.pending_joint_return, rec.in_destroy_body = set(), [], None, False
        rec.fixture_shapes, rec.items, rec.item_meta = {}, {}, {}
        rec.state_init, rec.item_type = None, 0

    def override_idle_timers(self) -> None:
        """GameItemUtils::SetInitialState seeds the dart / scissors idle timer with the item block's address;
        the core seeds it with the handle (docs/10 §11) — the oracle writes the same value."""
        e = self.e
        for t, off in IDLE_TIMER_TYPES.items():
            start, size = self.type_start(t), self.item_size(t)
            for k in range(self.type_count(t)):
                blk = start + k * size
                handle = e.u32(blk)
                e.wf32(blk + off, lcg_get_float(handle, *IDLE_TIMER_RANGE))

    def apply_and_build(self, layout: int, mode: int) -> None:
        """LevelLayoutUtils::Apply + GamePhysicsUtils::CreateWorld / CreateDynamicPhysics / CreateAttachments."""
        emu, rec = self.e, self.rec
        if emu.call("st::LevelLayoutUtils::Apply(st::GameState&, st::LevelLayout const&)", self.gs, layout) & 0xff == 0:
            raise RuntimeError("LevelLayoutUtils::Apply rejected the layout")
        self.override_idle_timers()
        emu.call("st::GamePhysicsUtils::CreateWorld(st::GameState&, st::ActionQueue&, st::PhysicsMode::Enum)",
                 self.gs, self.queue, mode)
        self.world = emu.u32(self.gs + GS_B2WORLD)
        rec.lines.append(f"gravity {fbits(0.0)} {fbits(-9.8)}")
        emu.call("st::GamePhysicsUtils::CreateDynamicPhysics(st::WorldState&, st::PhysicsMode::Enum)", self.ws, mode)
        for i in range(self.object_count()):
            obj = self.obj(i)
            n = emu.u32(obj + OBJ_BODY_COUNT)
            t = emu.u32(obj + OBJ_TYPE)
            if n:
                first = rec.idx(emu.u32(obj + OBJ_BODIES))
                rec.lines.append(f"# item {t} {ITEM_NAMES.get(t, '?')} bodies {first}..{first + n - 1}")
        emu.call("st::GamePhysicsUtils::CreateAttachments(st::WorldState&)", self.ws)

    def _stub_localisation(self) -> None:
        e = self.e

        def dummy(em: H.Emu) -> bool:
            em.ret(self.dummy)
            return True

        e.hook_function("UI::Localization::Instance()", dummy)
        for _sym, addr in e.syms.find("UI::Localization::GetLocalizedString"):
            e.hook_function(addr, dummy)

    # -- state readers
    def object_count(self) -> int:
        return self.e.u32(self.ws + WS_OBJECT_COUNT)

    def obj(self, i: int) -> int:
        return self.ws + WS_OBJECTS + i * T.OBJ_STRIDE

    def type_count(self, t: int) -> int:
        return self.e.u32(self.ws + WS_TYPE_COUNT + t * 4)

    def type_start(self, t: int) -> int:
        return self.ws + self.e.u32(self.ws + WS_TYPE_START + t * 4) + WS_ITEMS

    def item_of(self, handle: int) -> int:
        return self.e.call("st::HandleManager::Get(st::Handle) const", self.hm, handle)

    def item_size(self, t: int) -> int:
        e = self.e
        return e.u32(e.syms.by_name["st::ItemInfos"] + t * T.ITEM_INFO_STRIDE + 8)

    # -- the action queue (GameScreenController::processActions, simulation-side rules)
    def process_actions(self, substep: int | None) -> None:
        e = self.e
        i = 0
        while i < e.u32(self.queue):
            a = self.queue + 4 + i * ACTION_STRIDE
            words = [e.u32(a + k * 4) for k in range(ACTION_WORDS)]
            aid = words[0]
            self.sim.append(f"action {self.frame} {substep if substep is not None else '-'} {aid} {words[1]:#x} "
                            + " ".join(f"{w:#x}" for w in words[2:]))
            if aid == ACTION_SOUND:
                pass                                    # SoundSystemUtils::Play: audio only
            elif aid in SIM_ACTION_IDS:
                e.write(self.action_buf, e.read(a, ACTION_STRIDE))
                e.call("st::GameScreen::ProcessSimulationAction(st::Action const&, st::GameState&, st::GameResources&, "
                       "st::AudioSystem&, st::ActionQueue&)", self.action_buf, self.gs, 0, 0, self.queue)
            elif aid == ACTION_GOAL_COMPLETE:
                # ItemActionsMisc: goalReached = 1, then startLevelCompleteSequence unless the effect runs.
                e.write(self.goal_state, b"\x01")
                self.start_completion()
            elif aid == ACTION_SIM_TOUCH:
                self.stop_requested = True
            elif aid == ACTION_SKIP_ANIMS:
                pass                                    # skipLevelCompletedAnims: the effect only, not the countdown
            i += 1
        e.w32(self.queue, 0)

    def start_completion(self) -> None:
        if self.completing:
            return
        self.completing = True
        self.countdown = H.f32(COMPLETION_COUNTDOWN)

    # -- GameScreen::UpdateSimulation (transcribed from the disassembly, docs/04 §3)
    def update_simulation(self, acc: float) -> float:
        e = self.e
        dt = STEP_DT_BITS
        acc0 = acc
        step_dt = H.f32(STEP_DT_BITS)
        remaining = acc
        coll, hm, q, ws = self.coll, self.hm, self.queue, self.ws

        def start_count(t: int) -> tuple[int, int]:
            return self.type_start(t), self.type_count(t)

        while remaining >= step_dt:
            e.write(self.prev, e.read(ws, T.WS_SIZE))
            s, n = start_count(5)
            e.call("st::BalloonUtils::Update(float, st::Balloon*, int, st::PhysicsObjectCollection&, st::HandleManager&, st::ActionQueue&)", dt, s, n, coll, hm, q)
            s, n = start_count(14)
            e.call("st::BoxingGloveUtils::Update(float, st::BoxingGlove*, int, st::PhysicsObjectCollection&)", dt, s, n, coll)
            s, n = start_count(36)
            e.call("st::RadioControllerUtils::Update(float, st::RadioController*, int, st::PhysicsObjectCollection&, st::HandleManager const&, st::ActionQueue&)", dt, s, n, coll, hm, q)
            s, n = start_count(39)
            e.call("st::HelicopterUtils::Update(float, st::Helicopter*, int, st::PhysicsObjectCollection&, st::HandleManager&, st::ActionQueue&)", dt, s, n, coll, hm, q)
            s, n = start_count(34)
            e.call("st::SlingshotUtils::Update(float, st::Slingshot*, int, st::GameState&, st::ActionQueue&)", dt, s, n, self.gs, q)
            s, n = start_count(25)
            e.call("st::MagnetUtils::Update(float, st::Magnet*, int, st::HandleManager const&, st::PhysicsObjectCollection&, st::ActionQueue&)", dt, s, n, hm, coll, q)
            s, n = start_count(27)
            e.call("st::PaperPlaneUtils::Update(float, st::GameItem*, int, st::HandleManager&, st::PhysicsObjectCollection&, st::ActionQueue&)", dt, s, n, hm, coll, q)
            s, n = start_count(28)
            e.call("st::SpringUtils::Update(float, st::GameItem*, int, st::PhysicsObjectCollection&, st::ActionQueue&)", dt, s, n, coll, q)
            s, n = start_count(33)
            e.call("st::BumperUtils::Update(float, st::Bumper*, int, st::PhysicsObjectCollection const&, st::ActionQueue&)", dt, s, n, coll, q)
            s, n = start_count(38)
            e.call("st::TrapdoorLeverUtils::Update(float, st::TrapdoorLever*, int, st::PhysicsObjectCollection&, st::HandleManager const&, st::ActionQueue&)", dt, s, n, coll, hm, q)
            s, n = start_count(22)
            e.call("st::SeesawUtils::Update(float, st::Seesaw*, int, st::PhysicsObjectCollection&, st::ActionQueue&)", dt, s, n, coll, q)
            e.call("b2World::Step(float, int, int)", self.world, dt, T.VEL_ITERS, T.POS_ITERS)
            e.call("b2World::ClearForces()", self.world)
            e.call("st::GamePhysicsUtils::GetStateFromPhysics(st::WorldState&)", ws)
            remaining = f32u(remaining - step_dt)
            self.substep += 1
            self.dump_bodies()
            s, n = start_count(6)
            e.call("st::ScissorsUtils::Update(float, st::Scissors*, int, st::HandleManager const&, st::PhysicsObjectCollection&, st::Random&, st::ActionQueue&)", dt, s, n, hm, coll, self.random, q)
            s, n = start_count(GOAL_STAR)
            e.call("st::GoalStarUtils::Update(float, st::GoalStar*, int, st::GoalState&, st::HandleManager const&, st::PhysicsObjectCollection&, st::ActionQueue&, st::VisualWorldState&)",
                   dt, s, n, self.goal_state, hm, coll, q, self.gs + GS_VISUAL)
            self.process_actions(self.substep)
        acc0_bits = H.i32(acc0)
        s, n = start_count(13)
        e.call("st::PiggyBankUtils::Update(float, st::PiggyBank*, int, st::PhysicsObjectCollection&)", acc0_bits, s, n, coll)
        s, n = start_count(29)
        e.call("st::DartUtils::UpdateSetUpMode(float, st::Dart*, int, st::Random&)", acc0_bits, s, n, self.random)
        if e.read(self.goal_state, 1)[0] == 0:
            e.call("st::GoalStateUtils::Update(float, st::GoalState&, st::LevelInfo const&, st::WorldState const&)",
                   acc0_bits, self.goal_state, self.gs + GS_LEVEL_INFO, ws)
            if e.call("st::GoalStateUtils::IsGoalComplete(st::GoalState const&, st::LevelInfo const&, st::WorldState const&)",
                      self.goal_state, self.gs + GS_LEVEL_INFO, ws) & 0xff:
                e.call("st::Action::Action(st::ActionType::Enum)", self.action_buf, ACTION_GOAL_COMPLETE)
                e.call("st::ActionQueueUtils::Add(st::ActionQueue&, st::Action const&)", q, self.action_buf)
        e.write(self.render, e.read(ws, T.WS_SIZE))
        alpha = f32u(remaining / step_dt)
        e.call("st::GamePhysicsUtils::LerpState(st::WorldState&, st::WorldState const&, st::WorldState const&, float)",
               self.render, self.prev, ws, H.i32(alpha))
        return remaining

    # -- one doFrame (case 4 + the common tail, UI and effects left out)
    def run_frame(self, dt: float = FRAME_DT) -> None:
        e = self.e
        if self.stop_requested:
            # +0xc99b8: the next doFrame calls toggleSimulation before anything else.
            self.stopped = True
            return
        self.frame += 1
        self.process_actions(None)
        if self.controller_state == CONTROLLER_SIMULATION:
            dt = f32u(dt * e.f32_at(self.gs + GS_TIME_SCALE))
            self.acc = self.update_simulation(f32u(self.acc + f32u(f32u(dt) * f32u(SPEED))))
            # doFrame case 4: three stars end the level like the goal only in game modes 2 / 4 (World of
            # Contraptions, test play) [verified: `getMode() == 2 || == 4` guards the GoalState+4 == 3 test];
            # the campaign needs the goal.
            completed = False
            if not self.completing:
                e.wf32(self.gs + GS_PLAY_TIME, f32u(e.f32_at(self.gs + GS_PLAY_TIME) + dt))
            else:
                self.countdown = f32u(self.countdown - dt)
                if self.countdown <= 0.0:
                    self.controller_state = CONTROLLER_COMPLETED   # setCompletedState
                    self.completing = False
                    completed = True
            if completed:
                # The state changed: doFrame recurses for the same frame (`goto LAB_000cb840`) and the
                # default branch runs instead of this one's tail — the queue drained, the render copy a
                # plain copy of the world state, no StopRunawayObjects.
                self.process_actions(None)
                e.write(self.render, e.read(self.ws, T.WS_SIZE))
            else:
                e.call("st::GamePhysicsUtils::StopRunawayObjects(st::WorldState&)", self.ws)
        else:
            e.write(self.render, e.read(self.ws, T.WS_SIZE))    # the default branch's render copy
        removed_before = self.object_count()
        handles_before = [e.u32(self.obj(i) + OBJ_HANDLE) for i in range(removed_before)]
        e.call("st::WorldStateUtils::RemoveInvalidItems(st::WorldState&)", self.ws)
        e.write(self.prev, e.read(self.ws, T.WS_SIZE))
        if self.controller_state == CONTROLLER_SIMULATION:
            if e.call("st::GamePhysicsUtils::HasMovingObjects(st::WorldState&)", self.ws) & 0xff == 0:
                self.idle_time = f32u(self.idle_time + dt)
                if self.idle_time > IDLE_STOP_SECONDS:
                    self.stopped = True                 # toggleSimulation: back to set-up
            else:
                self.idle_time = 0.0
        self.dump_frame(handles_before)

    # -- dumps
    def dump_bodies(self) -> None:
        e = self.e
        for i, b in enumerate(self.rec.bodies):
            if i in self.rec.dead:
                continue
            px, py = struct.unpack("<ff", e.read(b + T.BODY_POS, 8))
            (a,) = struct.unpack("<f", e.read(b + T.BODY_ANGLE, 4))
            vx, vy = struct.unpack("<ff", e.read(b + T.BODY_LINVEL, 8))
            (w,) = struct.unpack("<f", e.read(b + T.BODY_ANGVEL, 4))
            awake = 1 if e.u32(b + T.BODY_FLAGS) & T.BODY_AWAKE_FLAG else 0
            self.traj.append(f"{self.substep} {i} {px:.9g} {py:.9g} {a:.9g} {vx:.9g} {vy:.9g} {w:.9g} {awake}")

    def dump_frame(self, handles_before: list[int]) -> None:
        e = self.e
        out = self.sim
        if self.stopped:
            out.append(f"frame {self.frame} stopped")     # toggleSimulation ran in this frame's tail
            return
        out.append(f"frame {self.frame} {fbits(self.acc)} {'0x%08x' % e.u32(self.gs + GS_PLAY_TIME)} "
                   f"{e.read(self.goal_state, 1)[0]} {e.u32(self.goal_state + 4)} {self.controller_state}")
        out.append(f"random {e.u32(self.random):#x}")
        live = {e.u32(self.obj(i) + OBJ_HANDLE) for i in range(self.object_count())}
        for h in handles_before:
            if h not in live:
                out.append(f"removed {h:#x}")
        for i in range(self.object_count()):
            obj = self.obj(i)
            out.append(f"obj {i} {e.u32(obj + OBJ_TYPE)} {e.u32(obj + OBJ_HANDLE):#x} {'0x%08x' % e.u32(obj + OBJ_POS)} "
                       f"{'0x%08x' % e.u32(obj + OBJ_POS + 4)} {'0x%08x' % e.u32(obj + OBJ_ANGLE)} "
                       f"{e.read(obj + OBJ_FLAGS, 1)[0]} {e.read(obj + OBJ_STATE, 1)[0]} {e.u32(obj + OBJ_BODY_COUNT)}")
            if e.read(obj + OBJ_FLAGS, 1)[0] & 2:
                # The render copy predates RemoveInvalidItems (renderFrame draws it before the tail):
                # find the object's record in it by handle, as the core keeps its poses across the swap.
                r = self.render + WS_OBJECTS + i * T.OBJ_STRIDE
                handle = e.u32(obj + OBJ_HANDLE)
                if e.u32(r + OBJ_HANDLE) != handle:
                    for k in range(e.u32(self.render + WS_OBJECT_COUNT)):
                        if e.u32(self.render + WS_OBJECTS + k * T.OBJ_STRIDE + OBJ_HANDLE) == handle:
                            r = self.render + WS_OBJECTS + k * T.OBJ_STRIDE
                            break
                out.append(f"lerp {i} {'0x%08x' % e.u32(r + OBJ_POS)} {'0x%08x' % e.u32(r + OBJ_POS + 4)} {'0x%08x' % e.u32(r + OBJ_ANGLE)}")
        for t, fields in sorted(ITEM_FIELDS.items()):
            start = self.type_start(t)
            size = self.item_size(t)
            for k in range(self.type_count(t)):
                blk = start + k * size
                vals = []
                for name, off, kind in fields:
                    if kind == "f":
                        vals.append(f"{name}={'0x%08x' % e.u32(blk + off)}")
                    elif kind == "b":
                        vals.append(f"{name}={e.read(blk + off, 1)[0]}")
                    else:
                        vals.append(f"{name}={s32(e.u32(blk + off))}")
                out.append(f"item {t} {e.u32(blk):#x} " + " ".join(vals))
        # Word 0 is the `reached` byte (the other three bytes are padding Apply copies from a stack local).
        out.append(f"goalstate {e.read(self.goal_state, 1)[0]:#x} "
                   + " ".join(f"{e.u32(self.goal_state + k * 4):#x}" for k in range(1, GOAL_STATE_WORDS)))

    def tap(self) -> None:
        # GameSimulationTouchHandler::Process: a touch beginning outside every button queues 0x15
        # (0x18 once the level-complete effect runs) — the handler itself needs the UI buttons, so the
        # action is queued directly.
        self.queue_action(ACTION_SKIP_ANIMS if self.completing else ACTION_SIM_TOUCH)

    def queue_action(self, aid: int) -> None:
        e = self.e
        e.call("st::Action::Action(st::ActionType::Enum)", self.action_buf, aid)
        e.call("st::ActionQueueUtils::Add(st::ActionQueue&, st::Action const&)", self.queue, self.action_buf)


def read_script(path: Path) -> tuple[str, list[tuple[str, list[str]]]]:
    level = ""
    commands: list[tuple[str, list[str]]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if parts[0] == "level":
            level = parts[1]
        else:
            commands.append((parts[0], parts[1:]))
    if not level:
        raise ValueError(f"{path}: no `level` line")
    return level, commands


def run_script(emu: H.Emu, rec: T.SceneRecorder, script: Path, levels_dir: Path) -> tuple[list[str], list[str], list[str]]:
    level_ref, commands = read_script(script)
    chapter, name = level_ref.split("/", 1)
    with open(levels_dir / chapter / f"{name}.json", encoding="utf-8") as f:
        level = json.load(f)
    listener = not any(c == "listener" and a[:1] == ["off"] for c, a in commands)
    seed = next((int(a[0]) for c, a in commands if c == "seed"), None)
    oracle = SimOracle(emu, rec, level, listener=listener, seed=seed)
    frames = 0
    for cmd, args in commands:
        try:
            if cmd == "frames":
                for _ in range(int(args[0])):
                    if oracle.stopped:
                        break
                    oracle.run_frame()
                    frames += 1
            elif cmd == "tap":
                oracle.tap()
            elif cmd == "goal":
                oracle.queue_action(ACTION_GOAL_COMPLETE)
            elif cmd in ("listener", "seed"):
                pass
            else:
                raise ValueError(f"unknown sim command: {cmd}")
        except (UcError, RuntimeError) as exc:
            pc = emu.uc.reg_read(UC_ARM_REG_PC)
            raise RuntimeError(f"{script.name}: `{cmd}` failed at frame {oracle.frame}: {exc} at {pc:#x} {emu.describe(pc)}") from exc
    if emu.unimplemented_hits:
        raise RuntimeError(f"{script.name}: unimplemented imports called: {emu.unimplemented_hits}")
    scene = [f"# amazing-alex physics scene v1 sim_{script.stem} ({oracle.object_count()} objects)"] + oracle.scene
    scene.append(f"step {oracle.substep} {fbits(H.f32(STEP_DT_BITS))} {T.VEL_ITERS} {T.POS_ITERS}")
    traj = [f"# amazing-alex trajectory v1 bodies={len(rec.bodies)} steps={oracle.substep}"]
    traj += [l for l in oracle.scene if l.startswith("# item ")]
    traj += oracle.traj
    sim = [f"# amazing-alex sim trace v1 level={level_ref} frames={frames} substeps={oracle.substep} stopped={int(oracle.stopped)}"]
    sim += oracle.sim
    return scene, traj, sim


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("so")
    parser.add_argument("items_plist")
    parser.add_argument("--levels", type=Path, required=True, help="imported level JSON tree (chapter dirs)")
    parser.add_argument("--script", type=Path, action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args.out.mkdir(parents=True, exist_ok=True)
    emu, rec = T.make_emulator(args.so, args.items_plist)
    failures = 0
    for script in args.script:
        try:
            scene, traj, sim = run_script(emu, rec, script, args.levels)
        except (RuntimeError, ValueError, FileNotFoundError) as exc:
            log.error("%s", exc)
            failures += 1
            continue
        T.write(args.out / f"{script.stem}.scene", scene)
        T.write(args.out / f"{script.stem}.uc.traj", traj)
        T.write(args.out / f"{script.stem}.sim", sim)
        log.info("%s: %d scene lines, %d traj lines, %d sim lines", script.stem, len(scene), len(traj), len(sim))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
