#!/usr/bin/env python3
"""Unicorn-based loader for the Android `libamazingalex.so` (ARM softfp).

Maps the shared object at its link address (base 0), stubs every libc/libm/GLES import with a
Python implementation (libm transcendentals go to the vendored device libm, core/third_party/aa_libm,
compiled on demand into build/aa_libm), runs the static initialisers (`.init_array`) and then lets
callers invoke arbitrary exported functions.  Used by the constant-recovery scripts (`uc_dump_*.py`) to read
`.bss` values that are only computed at start-up and to drive the game's own Box2D setup code.

Calling convention: AAPCS softfp — ints/floats in r0..r3 then the stack, doubles in register pairs.
"""
from __future__ import annotations

import ctypes
import logging
import math
import os
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import lief
from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_MODE_ARM, UC_PROT_ALL,
                     Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC,
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_SP)

log = logging.getLogger("uc_harness")

IMAGE_BASE = 0x0
IMAGE_SIZE = 0x2A0000
STUB_BASE = 0x0F000000
STUB_SIZE = 0x10000
RETURN_MAGIC = STUB_BASE + STUB_SIZE - 0x10  # LR for top-level calls; hitting it stops emulation
DATA_BASE = 0x0F100000  # backing store for imported data symbols
DATA_SIZE = 0x10000
HEAP_BASE = 0x10000000
HEAP_SIZE = 0x08000000
STACK_BASE = 0x7FF00000
STACK_SIZE = 0x00100000
BX_LR = b"\x1e\xff\x2f\xe1"

# BSD/Bionic _ctype_ flags
_U, _L, _N, _S, _P, _C, _X, _B = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80


def _ctype_table() -> bytes:
    tab = bytearray(257)
    for c in range(256):
        f = 0
        if 65 <= c <= 90:
            f |= _U
        if 97 <= c <= 122:
            f |= _L
        if 48 <= c <= 57:
            f |= _N
        if c in (9, 10, 11, 12, 13, 32):
            f |= _S
        if 33 <= c <= 47 or 58 <= c <= 64 or 91 <= c <= 96 or 123 <= c <= 126:
            f |= _P
        if c < 32 or c == 127:
            f |= _C
        if 48 <= c <= 57 or 65 <= c <= 70 or 97 <= c <= 102:
            f |= _X
        if c == 32:
            f |= _B
        tab[c + 1] = f
    return bytes(tab)


def f32(x: int) -> float:
    return struct.unpack("<f", struct.pack("<I", x & 0xFFFFFFFF))[0]


def i32(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


@dataclass
class Symbols:
    by_name: dict[str, int] = field(default_factory=dict)      # demangled name → address
    by_addr: dict[int, str] = field(default_factory=dict)
    mangled: dict[str, int] = field(default_factory=dict)      # mangled name → address

    @classmethod
    def load(cls, so_path: str) -> "Symbols":
        syms = cls()
        for flag, target in (("-DC", syms.by_name), ("-D", syms.mangled)):
            out = subprocess.run(["nm", flag, so_path], capture_output=True, text=True, check=True).stdout
            for line in out.splitlines():
                parts = line.split(maxsplit=2)
                if len(parts) == 3 and parts[1] in "TtDdBbWw":
                    addr = int(parts[0], 16)
                    target.setdefault(parts[2], addr)
                    if flag == "-DC":
                        syms.by_addr.setdefault(addr, parts[2])
        return syms

    def find(self, prefix: str) -> list[tuple[str, int]]:
        return sorted(((n, a) for n, a in self.by_name.items() if n.startswith(prefix)), key=lambda x: x[1])


class Emu:
    def __init__(self, so_path: str) -> None:
        self.so_path = so_path
        self.elf = lief.parse(so_path)
        with open(so_path, "rb") as fh:
            self.raw = fh.read()
        self.syms = Symbols.load(so_path)
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.heap_ptr = HEAP_BASE
        self.alloc_sizes: dict[int, int] = {}
        self.import_handlers: dict[str, Callable[["Emu"], None]] = {}
        self.stub_names: list[str] = []
        self.data_ptr = DATA_BASE
        self.tls: dict[int, int] = {}
        self.tls_next = 1
        self.errno_addr = 0
        self.call_hooks: dict[int, Callable[["Emu"], bool]] = {}
        self.printed: list[str] = []
        self._map_memory()
        self._install_imports()
        self._apply_relocations()
        self._enable_vfp()
        self.uc.hook_add(UC_HOOK_CODE, self._on_stub, begin=STUB_BASE, end=STUB_BASE + STUB_SIZE)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_mem_invalid)

    # ---------------------------------------------------------------- setup
    def _map_memory(self) -> None:
        uc = self.uc
        uc.mem_map(IMAGE_BASE, IMAGE_SIZE, UC_PROT_ALL)
        for seg in self.elf.segments:
            if seg.type == lief.ELF.Segment.TYPE.LOAD:
                off, va, fsz = seg.file_offset, seg.virtual_address, seg.physical_size
                uc.mem_write(IMAGE_BASE + va, self.raw[off:off + fsz])
        uc.mem_map(STUB_BASE, STUB_SIZE, UC_PROT_ALL)
        uc.mem_map(DATA_BASE, DATA_SIZE, UC_PROT_ALL)
        uc.mem_map(HEAP_BASE, HEAP_SIZE, UC_PROT_ALL)
        uc.mem_map(STACK_BASE, STACK_SIZE, UC_PROT_ALL)
        uc.mem_write(RETURN_MAGIC, BX_LR)

    def _enable_vfp(self) -> None:
        cpacr = self.uc.reg_read(UC_ARM_REG_C1_C0_2)
        self.uc.reg_write(UC_ARM_REG_C1_C0_2, cpacr | (0xF << 20))
        self.uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

    def _data_symbol(self, name: str) -> int:
        """Allocate backing memory for an imported data object."""
        size = 0x400
        content = b""
        if name == "_ctype_":
            content = _ctype_table()
        elif name in ("_tolower_tab_", "_toupper_tab_"):
            tab = bytearray(257)
            for c in range(256):
                if name == "_tolower_tab_":
                    tab[c + 1] = c + 32 if 65 <= c <= 90 else c
                else:
                    tab[c + 1] = c - 32 if 97 <= c <= 122 else c
            content = bytes(tab)
        addr = self.data_ptr
        self.data_ptr += size
        if content:
            self.uc.mem_write(addr, content)
        if name == "__stack_chk_guard":
            self.uc.mem_write(addr, struct.pack("<I", 0xDEADBEEF))
        return addr

    def _install_imports(self) -> None:
        names = sorted({s.name for s in self.elf.imported_symbols})
        self.stub_addr: dict[str, int] = {}
        self.unimplemented_hits: dict[str, int] = {}
        for i, name in enumerate(names):
            addr = STUB_BASE + i * 4
            self.uc.mem_write(addr, BX_LR)
            self.stub_addr[name] = addr
            self.stub_names.append(name)
        self.data_addr: dict[str, int] = {}
        for name in ("_ctype_", "_tolower_tab_", "_toupper_tab_", "__stack_chk_guard", "__sF"):
            if name in self.stub_addr:
                self.data_addr[name] = self._data_symbol(name)
        self.errno_addr = self._data_symbol("errno")
        self.import_handlers.update(default_handlers())

    def _apply_relocations(self) -> None:
        """Only symbol relocations matter: the image is loaded at its link base (0), so
        R_ARM_RELATIVE addends are already final."""
        for rel in self.elf.relocations:
            t = rel.type
            if t == lief.ELF.Relocation.TYPE.ARM_RELATIVE:
                continue
            name = rel.symbol.name if rel.has_symbol else ""
            if name in self.data_addr:
                value = self.data_addr[name]
            elif name in self.stub_addr:
                value = self.stub_addr[name]
            elif name in self.syms.mangled:  # internal symbol exported via GLOB_DAT/ABS32
                value = self.syms.mangled[name]
            else:
                log.warning("unresolved relocation %s type %s at %#x", name, t, rel.address)
                continue
            if t == lief.ELF.Relocation.TYPE.ARM_ABS32:
                cur, = struct.unpack("<I", bytes(self.uc.mem_read(rel.address, 4)))
                value = (value + cur) & 0xFFFFFFFF
            self.uc.mem_write(rel.address, struct.pack("<I", value))

    # ---------------------------------------------------------------- hooks
    def _on_stub(self, uc: Uc, address: int, size: int, user_data: object) -> None:
        idx = (address - STUB_BASE) // 4
        if address == RETURN_MAGIC:
            uc.emu_stop()
            return
        name = self.stub_names[idx]
        handler = self.import_handlers.get(name)
        if handler is None:
            if name not in self.unimplemented_hits:
                log.warning("unimplemented import %s called → returns 0 (results may be wrong)", name)
            self.unimplemented_hits[name] = self.unimplemented_hits.get(name, 0) + 1
            uc.reg_write(UC_ARM_REG_R0, 0)
            return
        handler(self)

    def _on_mem_invalid(self, uc: Uc, access: int, address: int, size: int, value: int, user_data: object) -> bool:
        pc = uc.reg_read(UC_ARM_REG_PC)
        log.error("invalid memory access at %#x (pc=%#x %s)", address, pc, self.describe(pc))
        return False

    # ---------------------------------------------------------------- helpers
    def describe(self, addr: int) -> str:
        best = None
        for a, n in self.syms.by_addr.items():
            if a <= addr and (best is None or a > best[0]):
                best = (a, n)
        return f"{best[1]}+{addr - best[0]:#x}" if best else "?"

    def malloc(self, size: int, zero: bool = False) -> int:
        size = max(size, 1)
        addr = (self.heap_ptr + 15) & ~15
        self.heap_ptr = addr + size
        if self.heap_ptr > HEAP_BASE + HEAP_SIZE:
            raise MemoryError("emulated heap exhausted")
        self.alloc_sizes[addr] = size
        if zero:
            self.uc.mem_write(addr, bytes(size))
        return addr

    def read(self, addr: int, size: int) -> bytes:
        return bytes(self.uc.mem_read(addr, size))

    def write(self, addr: int, data: bytes) -> None:
        self.uc.mem_write(addr, data)

    def u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def f32_at(self, addr: int) -> float:
        return struct.unpack("<f", self.read(addr, 4))[0]

    def w32(self, addr: int, value: int) -> None:
        self.write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def wf32(self, addr: int, value: float) -> None:
        self.write(addr, struct.pack("<f", value))

    def cstring(self, addr: int) -> str:
        out = bytearray()
        while True:
            b = self.read(addr, 1)
            if b == b"\0":
                return out.decode("latin-1")
            out += b
            addr += 1

    def push_string(self, s: str) -> int:
        data = s.encode() + b"\0"
        addr = self.malloc(len(data))
        self.write(addr, data)
        return addr

    def reg(self, r: int) -> int:
        return self.uc.reg_read(r)

    def args(self, n: int) -> list[int]:
        regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
        out = [self.uc.reg_read(r) for r in regs[:n]]
        sp = self.uc.reg_read(UC_ARM_REG_SP)
        for i in range(4, n):
            out.append(self.u32(sp + (i - 4) * 4))
        return out

    def ret(self, value: int = 0) -> None:
        self.uc.reg_write(UC_ARM_REG_R0, value & 0xFFFFFFFF)

    def ret_double(self, value: float) -> None:
        lo, hi = struct.unpack("<II", struct.pack("<d", value))
        self.uc.reg_write(UC_ARM_REG_R0, lo)
        self.uc.reg_write(UC_ARM_REG_R1, hi)

    def arg_double(self, first_reg: int = 0) -> float:
        a = self.args(first_reg + 2)
        return struct.unpack("<d", struct.pack("<II", a[first_reg], a[first_reg + 1]))[0]

    def call(self, addr: int | str, *args: int, timeout_s: float = 60.0) -> int:
        """Call a function with integer/raw-float arguments (floats already packed as u32)."""
        if isinstance(addr, str):
            addr = self.syms.by_name[addr]
        uc = self.uc
        sp = STACK_BASE + STACK_SIZE - 0x1000
        stack_args = list(args[4:])
        sp -= len(stack_args) * 4
        sp &= ~7
        for i, v in enumerate(stack_args):
            self.w32(sp + i * 4, v)
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(r, v & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, RETURN_MAGIC)
        uc.emu_start(addr, RETURN_MAGIC, timeout=int(timeout_s * 1_000_000))
        return uc.reg_read(UC_ARM_REG_R0)

    def hook_function(self, addr: int | str, cb: Callable[["Emu"], bool]) -> None:
        """Run `cb` whenever execution enters `addr`. If cb returns True the function is skipped
        (r0 is taken as the return value and control goes back to LR)."""
        if isinstance(addr, str):
            addr = self.syms.by_name[addr]
        # Re-hooking an address replaces the callback; one Unicorn hook per address, or the callback
        # would run once per registration.
        installed = addr in self.call_hooks
        self.call_hooks[addr] = cb
        if not installed:
            self.uc.hook_add(UC_HOOK_CODE, self._on_hooked_call, begin=addr, end=addr)

    def _on_hooked_call(self, uc: Uc, address: int, size: int, user_data: object) -> None:
        cb = self.call_hooks.get(address)
        if cb is not None and cb(self):
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def run_static_initializers(self, skip: set[int] | None = None) -> list[int]:
        sec = self.elf.get_section(".init_array")
        n = sec.size // 4
        funcs = struct.unpack_from(f"<{n}I", self.raw, sec.file_offset)
        failed: list[int] = []
        for fn in funcs:
            if fn in (0, 0xFFFFFFFF) or (skip and fn in skip):
                continue
            try:
                self.call(fn)
            except UcError as exc:
                pc = self.uc.reg_read(UC_ARM_REG_PC)
                log.warning("initializer %#x (%s) failed: %s at pc=%#x %s", fn, self.describe(fn), exc, pc, self.describe(pc))
                failed.append(fn)
        return failed


# -------------------------------------------------------------------- import handlers
def _mem_handlers() -> dict[str, Callable[[Emu], None]]:
    def malloc(e: Emu) -> None:
        e.ret(e.malloc(e.args(1)[0]))

    def calloc(e: Emu) -> None:
        n, m = e.args(2)
        e.ret(e.malloc(n * m, zero=True))

    def realloc(e: Emu) -> None:
        p, n = e.args(2)
        q = e.malloc(n)
        if p:
            old = e.alloc_sizes.get(p, n)
            e.write(q, e.read(p, min(old, n)))
        e.ret(q)

    def free(e: Emu) -> None:
        e.ret(0)

    def memcpy(e: Emu) -> None:
        d, s, n = e.args(3)
        if n:
            e.write(d, e.read(s, n))
        e.ret(d)

    def memmove(e: Emu) -> None:
        # read-then-write, so overlapping ranges are safe (libstdc++ copy_backward relies on it)
        d, s, n = e.args(3)
        if n:
            e.write(d, e.read(s, n))
        e.ret(d)

    def bcopy(e: Emu) -> None:
        s, d, n = e.args(3)
        if n:
            e.write(d, e.read(s, n))
        e.ret(0)

    def memset(e: Emu) -> None:
        d, c, n = e.args(3)
        if n:
            e.write(d, bytes([c & 0xFF]) * n)
        e.ret(d)

    def memcmp(e: Emu) -> None:
        a, b, n = e.args(3)
        x, y = e.read(a, n), e.read(b, n)
        e.ret(0 if x == y else (1 if x > y else -1))

    def memchr(e: Emu) -> None:
        p, c, n = e.args(3)
        i = e.read(p, n).find(bytes([c & 0xFF]))
        e.ret(p + i if i >= 0 else 0)

    def strlen(e: Emu) -> None:
        e.ret(len(e.cstring(e.args(1)[0])))

    def strcmp(e: Emu) -> None:
        a, b = e.args(2)
        x, y = e.cstring(a), e.cstring(b)
        e.ret(0 if x == y else (1 if x > y else -1))

    def strncmp(e: Emu) -> None:
        a, b, n = e.args(3)
        x, y = e.cstring(a)[:n], e.cstring(b)[:n]
        e.ret(0 if x == y else (1 if x > y else -1))

    def strcasecmp(e: Emu) -> None:
        a, b = e.args(2)
        x, y = e.cstring(a).lower(), e.cstring(b).lower()
        e.ret(0 if x == y else (1 if x > y else -1))

    def strcpy(e: Emu) -> None:
        d, s = e.args(2)
        e.write(d, e.cstring(s).encode("latin-1") + b"\0")
        e.ret(d)

    def strncpy(e: Emu) -> None:
        d, s, n = e.args(3)
        data = e.cstring(s).encode("latin-1")[:n]
        e.write(d, data + b"\0" * (n - len(data)))
        e.ret(d)

    def strcat(e: Emu) -> None:
        d, s = e.args(2)
        cur = e.cstring(d)
        e.write(d + len(cur), e.cstring(s).encode("latin-1") + b"\0")
        e.ret(d)

    def strchr(e: Emu) -> None:
        p, c = e.args(2)
        i = e.cstring(p).find(chr(c & 0xFF))
        e.ret(p + i if i >= 0 else 0)

    def strrchr(e: Emu) -> None:
        p, c = e.args(2)
        i = e.cstring(p).rfind(chr(c & 0xFF))
        e.ret(p + i if i >= 0 else 0)

    def strstr(e: Emu) -> None:
        p, q = e.args(2)
        i = e.cstring(p).find(e.cstring(q))
        e.ret(p + i if i >= 0 else 0)

    def strdup(e: Emu) -> None:
        e.ret(e.push_string(e.cstring(e.args(1)[0])))

    def atoi(e: Emu) -> None:
        s = e.cstring(e.args(1)[0]).strip()
        num = ""
        for ch in s:
            if ch.isdigit() or (ch in "+-" and not num):
                num += ch
            else:
                break
        e.ret(int(num) if num not in ("", "+", "-") else 0)

    def strtod(e: Emu) -> None:
        p, endp = e.args(2)
        s = e.cstring(p)
        best, length = 0.0, 0
        for i in range(len(s), 0, -1):
            try:
                best = float(s[:i].strip())
                length = i
                break
            except ValueError:
                continue
        if endp:
            e.w32(endp, p + length)
        e.ret_double(best)

    return {k: v for k, v in locals().items() if callable(v)}


def _math_handlers() -> dict[str, Callable[[Emu], None]]:
    def unary(fn: Callable[[float], float]) -> Callable[[Emu], None]:
        def h(e: Emu) -> None:
            try:
                e.ret_double(fn(e.arg_double(0)))
            except (ValueError, OverflowError):
                e.ret_double(float("nan"))
        return h

    def binary(fn: Callable[[float, float], float]) -> Callable[[Emu], None]:
        def h(e: Emu) -> None:
            try:
                e.ret_double(fn(e.arg_double(0), e.arg_double(2)))
            except (ValueError, OverflowError, ZeroDivisionError):
                e.ret_double(float("nan"))
        return h

    def ldexp(e: Emu) -> None:
        e.ret_double(math.ldexp(e.arg_double(0), struct.unpack("<i", struct.pack("<I", e.args(3)[2]))[0]))

    def frexp(e: Emu) -> None:
        m, ex = math.frexp(e.arg_double(0))
        e.w32(e.args(3)[2], ex)
        e.ret_double(m)

    def modf(e: Emu) -> None:
        frac, whole = math.modf(e.arg_double(0))
        e.write(e.args(3)[2], struct.pack("<d", whole))
        e.ret_double(frac)

    # Transcendentals come from the device libm (aa_libm); the rest are exact IEEE operations.
    h: dict[str, Callable[[Emu], None]] = {}
    for name, fn in load_aa_libm().items():
        h[name] = unary(fn) if AA_LIBM_FUNCS[name] == 1 else binary(fn)
    h.update({
        "sqrt": unary(math.sqrt), "floor": unary(math.floor), "ceil": unary(math.ceil),
        "fmod": binary(math.fmod), "ldexp": ldexp, "frexp": frexp, "modf": modf,
    })
    return h


def _misc_handlers() -> dict[str, Callable[[Emu], None]]:
    def zero(e: Emu) -> None:
        e.ret(0)

    def errno(e: Emu) -> None:
        e.ret(e.errno_addr)

    def pthread_key_create(e: Emu) -> None:
        key_ptr = e.args(1)[0]
        e.w32(key_ptr, e.tls_next)
        e.tls_next += 1
        e.ret(0)

    def pthread_getspecific(e: Emu) -> None:
        e.ret(e.tls.get(e.args(1)[0], 0))

    def pthread_setspecific(e: Emu) -> None:
        k, v = e.args(2)
        e.tls[k] = v
        e.ret(0)

    def printf_like(e: Emu) -> None:
        e.printed.append(e.cstring(e.args(1)[0]))
        e.ret(0)

    def fprintf(e: Emu) -> None:
        e.printed.append(e.cstring(e.args(2)[1]))
        e.ret(0)

    def puts(e: Emu) -> None:
        e.printed.append(e.cstring(e.args(1)[0]))
        e.ret(0)

    def abort(e: Emu) -> None:
        raise RuntimeError("emulated abort()")

    def lrand48(e: Emu) -> None:
        e.ret(0x12345678)

    def getenv(e: Emu) -> None:
        e.ret(0)

    def time_(e: Emu) -> None:
        p = e.args(1)[0]
        if p:
            e.w32(p, 0)
        e.ret(0)

    def gettimeofday(e: Emu) -> None:
        p = e.args(1)[0]
        if p:
            e.write(p, bytes(8))
        e.ret(0)

    def clock_gettime(e: Emu) -> None:
        p = e.args(2)[1]
        if p:
            e.write(p, bytes(8))
        e.ret(0)

    def snprintf(e: Emu) -> None:
        # Minimal: copy the format string; enough for the initialisers we care about.
        buf, n, fmt = e.args(3)
        s = e.cstring(fmt).encode("latin-1")[: max(n - 1, 0)]
        e.write(buf, s + b"\0")
        e.ret(len(s))

    def sprintf(e: Emu) -> None:
        buf, fmt = e.args(2)
        s = e.cstring(fmt).encode("latin-1")
        e.write(buf, s + b"\0")
        e.ret(len(s))

    h: dict[str, Callable[[Emu], None]] = {
        "__cxa_atexit": zero, "__cxa_finalize": zero, "__errno": errno,
        "pthread_key_create": pthread_key_create, "pthread_getspecific": pthread_getspecific,
        "pthread_setspecific": pthread_setspecific,
        "printf": printf_like, "fprintf": fprintf, "puts": puts, "abort": abort,
        "lrand48": lrand48, "getenv": getenv, "time": time_, "gettimeofday": gettimeofday,
        "clock_gettime": clock_gettime, "snprintf": snprintf, "sprintf": sprintf,
    }
    for name in ("pthread_mutex_init", "pthread_mutex_lock", "pthread_mutex_unlock", "pthread_mutex_trylock",
                 "pthread_mutex_destroy", "pthread_mutexattr_init", "pthread_mutexattr_settype",
                 "pthread_mutexattr_destroy", "pthread_once", "pthread_cond_broadcast", "pthread_cond_wait",
                 "pthread_key_delete", "sched_yield", "usleep", "fflush", "fclose", "fsync", "close",
                 "__stack_chk_fail"):
        h.setdefault(name, zero)
    return h


AA_LIBM_SRC = Path(__file__).resolve().parent.parent / "core/third_party/aa_libm/src"
AA_LIBM_BUILD = Path(__file__).resolve().parent.parent / "build/aa_libm"
AA_LIBM_FUNCS = {"sin": 1, "cos": 1, "tan": 1, "asin": 1, "acos": 1, "atan": 1, "atan2": 2, "exp": 1, "log": 1,
                 "log10": 1, "pow": 2, "sinh": 1, "cosh": 1, "tanh": 1, "expm1": 1}   # name → arity


def load_aa_libm() -> dict[str, Callable[..., float]]:
    """The device libm (Bionic/msun, core/third_party/aa_libm) as Python callables.

    The shared library is compiled on demand with the C compiler from `CC` (default `cc`) into
    build/aa_libm and rebuilt when a source is newer; the flags match the CMake target. Raises on
    failure: the host libm differs from Bionic's in the last bit on a few percent of inputs, so falling
    back silently would make the emulated numbers platform-dependent."""
    ext = ".dylib" if sys.platform == "darwin" else ".so"
    lib = AA_LIBM_BUILD / f"libaa_libm{ext}"
    sources = sorted(AA_LIBM_SRC.glob("*.c"))
    newest = max(f.stat().st_mtime for f in sources + list(AA_LIBM_SRC.glob("*.h")))
    if not lib.exists() or lib.stat().st_mtime < newest:
        AA_LIBM_BUILD.mkdir(parents=True, exist_ok=True)
        cmd = [os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off", "-fno-fast-math", "-shared", "-fPIC",
               f"-I{AA_LIBM_SRC}", "-o", str(lib), *map(str, sources)]
        log.info("building %s", lib)
        subprocess.run(cmd, check=True)
    dll = ctypes.CDLL(str(lib))
    funcs: dict[str, Callable[..., float]] = {}
    for name, arity in AA_LIBM_FUNCS.items():
        fn = getattr(dll, f"aa_{name}")
        fn.restype = ctypes.c_double
        fn.argtypes = [ctypes.c_double] * arity
        funcs[name] = fn
    return funcs


def default_handlers() -> dict[str, Callable[[Emu], None]]:
    h: dict[str, Callable[[Emu], None]] = {}
    h.update(_mem_handlers())
    h.update(_math_handlers())
    h.update(_misc_handlers())
    return h


def make_emu(so_path: str, run_init: bool = True) -> Emu:
    emu = Emu(so_path)
    if run_init:
        failed = emu.run_static_initializers()
        log.info("static initialisers done, %d failed", len(failed))
    return emu


if __name__ == "__main__":
    import sys

    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    e = make_emu(sys.argv[1])
    for line in e.printed:
        log.info("printed: %s", line.rstrip())
