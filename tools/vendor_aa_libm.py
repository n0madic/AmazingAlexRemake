#!/usr/bin/env python3
"""Re-create core/third_party/aa_libm/src from Bionic's libm (FreeBSD msun) at a fixed AOSP tag.

The shipped Android build of Amazing Alex calls the device libm for sin/cos/atan2/pow/... and Bionic's
implementation of those (libm/src, verbatim FreeBSD msun, identical from android-2.3.7 to android-4.2.2)
is what the physics was tuned against. The remake carries that code so every platform computes the same
bits (docs/04-physics.md §1, core/third_party/aa_libm/README.md).

What this script does: downloads the Bionic tarball for TAG, copies the transcendental sources listed in
SOURCES, renames the exported and kernel symbols with an `aa_` prefix (no clash with the platform libm,
no compiler builtin folding) and drops the FreeBSD weak-reference lines. The portable `math_private.h`,
`aa_libm.h`, the self-test and the CMake file are hand-written and left untouched.

Usage: vendor_aa_libm.py [--tag android-4.0.4_r2.1] [--tarball path]  (network access unless --tarball)
"""
from __future__ import annotations

import argparse
import io
import logging
import re
import tarfile
import urllib.request
from pathlib import Path

log = logging.getLogger("vendor_aa_libm")

DEFAULT_TAG = "android-4.0.4_r2.1"
TARBALL_URL = "https://github.com/aosp-mirror/platform_bionic/archive/refs/tags/{tag}.tar.gz"
DEST = Path(__file__).resolve().parent.parent / "core/third_party/aa_libm/src"

# Everything the game imports from libm that is not an exact IEEE operation (sqrt/floor/ceil/fmod/
# frexp/ldexp/modf give the same bits everywhere and stay on the platform libm), plus their kernels.
SOURCES = ("s_sin.c", "s_cos.c", "s_tan.c", "k_sin.c", "k_cos.c", "k_tan.c", "e_rem_pio2.c", "k_rem_pio2.c",
           "e_atan2.c", "s_atan.c", "e_asin.c", "e_acos.c", "e_exp.c", "e_log.c", "e_log10.c", "e_pow.c",
           "e_sinh.c", "e_cosh.c", "s_tanh.c", "s_expm1.c")
EXPORTED = ("sin", "cos", "tan", "asin", "acos", "atan", "atan2", "exp", "log", "log10", "pow",
            "sinh", "cosh", "tanh", "expm1")
RENAMES = {**{name: f"aa_{name}" for name in EXPORTED},
           **{f"__ieee754_{name}": f"aa_{name}" for name in EXPORTED},
           "__ieee754_rem_pio2": "aa_rem_pio2",
           "__kernel_sin": "aa_kernel_sin", "__kernel_cos": "aa_kernel_cos", "__kernel_tan": "aa_kernel_tan",
           "__kernel_rem_pio2": "aa_kernel_rem_pio2"}
CALL_RE = re.compile(r"\b(" + "|".join(sorted(map(re.escape, RENAMES), key=len, reverse=True)) + r")(?=\s*\()")


def convert(text: str) -> str:
    lines = [line for line in text.splitlines() if "__weak_reference" not in line]
    return CALL_RE.sub(lambda m: RENAMES[m.group(1)], "\n".join(lines)) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", default=DEFAULT_TAG)
    parser.add_argument("--tarball", help="already downloaded platform_bionic tarball")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    if args.tarball:
        data = Path(args.tarball).read_bytes()
    else:
        url = TARBALL_URL.format(tag=args.tag)
        log.info("downloading %s", url)
        with urllib.request.urlopen(url) as resp:
            data = resp.read()
    DEST.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        members = {m.name.split("/", 1)[1]: m for m in tar.getmembers() if "/" in m.name}
        for name in SOURCES:
            member = members[f"libm/src/{name}"]
            text = tar.extractfile(member).read().decode("utf-8")
            (DEST / name).write_text(convert(text), encoding="utf-8")
            log.info("wrote %s", name)
        notice = tar.extractfile(members["libm/NOTICE"]).read()
        (DEST.parent / "NOTICE").write_bytes(notice)
    log.info("done: %d sources from %s", len(SOURCES), args.tag)


if __name__ == "__main__":
    main()
