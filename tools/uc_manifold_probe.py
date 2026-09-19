#!/usr/bin/env python3
"""Dump every polygon-polygon manifold the emulated original computes while stepping a level scene.

A physics-divergence probe (docs/09 §5): the level is built exactly as `uc_trace.py --level` does, then
`b2CollidePolygons` is hooked at entry and at its return address and the manifold it wrote is printed per
call — `step`, the two transforms' positions (to identify the pair), point count, type, local normal /
point and every manifold point (local point, id). With `--body N` the body's velocity is also printed at
the contact-solver stages (after the velocity integration, after every `SolveVelocityConstraints`, at
`StoreImpulses`) together with its constraint after `InitializeVelocityConstraints` (rA / rB, normal and
tangent mass, bias — the trunk's 0xb4-byte `b2ContactConstraint`). To compare, add temporary `fprintf`s at
the same points of the vendored solver (`b2Island::Solve`, `b2ContactSolver::InitializeVelocityConstraints`)
and replay the `.scene` with `trace_native`; the LaunchingRamp investigation is written up in docs/09 §5.

Usage: uc_manifold_probe.py <lib.so> <GameItems.plist> <level.plist> [--steps N] [--body N] [--out FILE]
"""
from __future__ import annotations

import argparse
import logging
import plistlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import uc_harness as H  # noqa: E402
import uc_trace as T  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_SP  # noqa: E402

log = logging.getLogger("uc_manifold_probe")

# The trunk's b2Manifold: points[2] of 24 bytes (localPoint, normalImpulse, tangentImpulse, id, a pad word),
# then localNormal (+0x30), localPoint (+0x38), type (+0x40), pointCount (+0x44).
POINT_STRIDE = 24
NORMAL_OFF, POINT_OFF, TYPE_OFF, COUNT_OFF = 0x30, 0x38, 0x40, 0x44


def fmt_manifold(e: H.Emu, m: int) -> str:
    n = e.u32(m + COUNT_OFF)
    typ = e.u32(m + TYPE_OFF)
    nx, ny = struct.unpack("<ff", e.read(m + NORMAL_OFF, 8))
    px, py = struct.unpack("<ff", e.read(m + POINT_OFF, 8))
    parts = [f"n={n} type={typ} normal={nx:.9g},{ny:.9g} point={px:.9g},{py:.9g}"]
    for i in range(min(n, 2)):
        lx, ly, _ni, _ti, key = struct.unpack("<ffffI", e.read(m + i * POINT_STRIDE, 20))
        parts.append(f"p{i}={lx:.9g},{ly:.9g} id={key:#x}")
    return " ".join(parts)


class Probe:
    def __init__(self, emu: H.Emu, out) -> None:
        self.e = emu
        self.out = out
        self.step = 0
        self.pending: list[tuple[int, str]] = []   # (manifold address, pair description)
        self.hooked: set[int] = set()
        emu.hook_function("b2World::Step(float, int, int)", self.on_step)
        emu.hook_function("b2CollidePolygons(b2Manifold*, b2PolygonShape const*, b2Transform const&, b2PolygonShape const*, b2Transform const&)",
                          self.on_collide)
        # The solver stages: the velocities of the probed body at the same points as the native probe.
        self.body = 0
        emu.hook_function("b2ContactSolver::WarmStart()", lambda e: self.on_stage(e, "integrated"))
        emu.hook_function("b2ContactSolver::SolveVelocityConstraints()", self.on_solve)
        emu.hook_function("b2ContactSolver::StoreImpulses()", lambda e: self.on_stage(e, "stored"))
        self.solver = 0
        emu.hook_function("b2ContactSolver::InitializeVelocityConstraints()", self.on_init)

    def on_init(self, e: H.Emu) -> bool:
        # The trunk's b2ContactSolver: +0 allocator, +4 constraints (0xb4 bytes each), +8 count.
        self.solver = e.args(1)[0]
        ret = e.reg(UC_ARM_REG_LR)
        if ret not in self.hooked:
            self.hooked.add(ret)
            e.hook_function(ret, self.on_init_return)
        return False

    def on_init_return(self, e: H.Emu) -> bool:
        if not self.solver or not self.body:
            return False
        base = e.u32(self.solver + 4)
        count = e.u32(self.solver + 8)
        for i in range(count):
            c = base + i * 0xB4
            body_a, body_b = e.u32(c + 0x90), e.u32(c + 0x94)
            if self.body not in (body_a, body_b):
                continue
            n = e.u32(c + 0xAC)
            nx, ny = struct.unpack("<ff", e.read(c + 0x68, 8))
            friction, restitution = struct.unpack("<ff", e.read(c + 0xA4, 8))
            ra_x, ra_y, rb_x, rb_y = struct.unpack("<ffff", e.read(c + 8, 16))
            ni, ti, nm, tm, vb = struct.unpack("<fffff", e.read(c + 0x18, 20))
            print(f"constraint normal={nx:.9g},{ny:.9g} friction={friction:.9g} restitution={restitution:.9g} n={n} "
                  f"rA={ra_x:.9g},{ra_y:.9g} rB={rb_x:.9g},{rb_y:.9g} normalMass={nm:.9g} tangentMass={tm:.9g} bias={vb:.9g} "
                  f"impulses={ni:.9g},{ti:.9g}", file=self.out)
            for name, off in (("sweepB.localCenter", body_b + 0x24), ("sweepB.c", body_b + 0x34), ("posB", body_b + T.BODY_POS)):
                x, y = struct.unpack("<ff", e.read(off, 8))
                print(f"  {name}={x:.9g},{y:.9g}", file=self.out)
            words = struct.unpack("<16f", e.read(body_b + 0x54, 64))
            print("  bodyB+0x54.. " + " ".join(f"{w:.9g}" for w in words), file=self.out)
        return False

    def body_line(self, e: H.Emu) -> str:
        b = self.body
        px, py = struct.unpack("<ff", e.read(b + T.BODY_POS, 8))
        vx, vy = struct.unpack("<ff", e.read(b + T.BODY_LINVEL, 8))
        (w,) = struct.unpack("<f", e.read(b + T.BODY_ANGVEL, 4))
        return f"body@{px:.6g},{py:.6g} v={vx:.9g},{vy:.9g} w={w:.9g}"

    def on_stage(self, e: H.Emu, tag: str) -> bool:
        if self.body:
            print(f"{tag} {self.body_line(e)}", file=self.out)
        return False

    def on_solve(self, e: H.Emu) -> bool:
        if not self.body:
            return False
        ret = e.reg(UC_ARM_REG_LR)
        if ret not in self.hooked:
            self.hooked.add(ret)
            e.hook_function(ret, lambda e2: self.on_stage(e2, "iter"))
        return False

    def on_step(self, e: H.Emu) -> bool:
        self.step += 1
        return False

    def on_collide(self, e: H.Emu) -> bool:
        manifold, _poly_a, xf_a, _poly_b = e.args(4)
        xf_b = e.u32(e.reg(UC_ARM_REG_SP))
        ax, ay = struct.unpack("<ff", e.read(xf_a, 8))
        bx, by = struct.unpack("<ff", e.read(xf_b, 8))
        pair = f"A={ax:.6g},{ay:.6g} B={bx:.6g},{by:.6g}"
        ret = e.reg(UC_ARM_REG_LR)
        if ret not in self.hooked:
            self.hooked.add(ret)
            e.hook_function(ret, self.on_return)
        self.pending.append((manifold, pair))
        return False

    def on_return(self, e: H.Emu) -> bool:
        if self.pending:
            manifold, pair = self.pending.pop()
            print(f"step {self.step} {pair} {fmt_manifold(e, manifold)}", file=self.out)
        return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("so_path")
    parser.add_argument("plist_path")
    parser.add_argument("level")
    parser.add_argument("--steps", type=int, default=2)
    parser.add_argument("--out", help="log file (default stdout)")
    parser.add_argument("--body", type=int, default=-1, help="body index whose velocity is printed at every solver stage")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    emu, rec = T.make_emulator(args.so_path, args.plist_path)
    with Path(args.level).open("rb") as fh:
        level = plistlib.load(fh)
    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    probe = Probe(emu, out)
    scene = T.Scene(emu, rec, capacity=int(level["itemCount"]) + 2)
    statuses = T.populate_level(scene, level)
    if args.body >= 0:
        probe.body = rec.bodies[args.body]
    scene.run(args.steps)
    log.info("%s", "; ".join(statuses))
    if args.out:
        out.close()


if __name__ == "__main__":
    main()
