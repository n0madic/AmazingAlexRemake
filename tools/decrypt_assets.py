#!/usr/bin/env python3
"""Decrypt Amazing Alex HD asset files (levels, GameItems, UI XML scenes).

The game encrypts text assets with AES-256-CBC, zero IV, PKCS#7 padding.
The packer appends a NUL terminator ("\n\0" on iOS) before padding.
Key is assembled byte-by-byte in GameApp::GameApp() (st::GameParams::CryptingKey).
"""
from __future__ import annotations

import argparse
import logging
import plistlib
import sys
from pathlib import Path

from Crypto.Cipher import AES

KEY = b"m1sOWGxsS23AoseNsM5Lsp9S21YtMxks"
IV = bytes(16)
ENCRYPTED_SUFFIXES = {".plist", ".xml"}
PLAIN_MAGICS = (b"<?xml", b"bplist", b"<!DOC", b"KA3D")

log = logging.getLogger("decrypt")


def decrypt_bytes(data: bytes) -> bytes:
    if len(data) % 16:
        raise ValueError(f"ciphertext length {len(data)} is not a multiple of 16")
    plain = AES.new(KEY, AES.MODE_CBC, IV).decrypt(data)
    pad = plain[-1]
    if not 1 <= pad <= 16 or plain[-pad:] != bytes([pad]) * pad:
        raise ValueError("bad PKCS#7 padding — wrong key or not encrypted")
    plain = plain[:-pad]
    # the packer appends a NUL terminator (iOS build: "\n\0") before padding
    if plain.endswith(b"\x00"):
        plain = plain[:-1]
    if plain.startswith(b"bplist"):
        try:
            plistlib.loads(plain)
        except plistlib.InvalidFileException:
            plain = plain.removesuffix(b"\n")
    return plain


def is_encrypted(path: Path, head: bytes) -> bool:
    return path.suffix.lower() in ENCRYPTED_SUFFIXES and not head.startswith(PLAIN_MAGICS)


def convert_plist(data: bytes) -> bytes:
    """Return XML plist bytes (binary plists are converted)."""
    if data.startswith(b"bplist"):
        return plistlib.dumps(plistlib.loads(data), fmt=plistlib.FMT_XML)
    return data


def process(src_root: Path, dst_root: Path) -> None:
    n_dec = n_copy = 0
    for src in sorted(src_root.rglob("*")):
        if not src.is_file():
            continue
        rel = src.relative_to(src_root)
        dst = dst_root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        data = src.read_bytes()
        if is_encrypted(src, data[:8]):
            try:
                data = decrypt_bytes(data)
                n_dec += 1
            except ValueError as exc:
                log.warning("%s: %s", rel, exc)
        if src.suffix.lower() == ".plist":
            data = convert_plist(data)
        dst.write_bytes(data)
        n_copy += 1
    log.info("processed %d files, decrypted %d", n_copy, n_dec)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", type=Path, help="app bundle / assets directory")
    ap.add_argument("dst", type=Path, help="output directory")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    process(args.src, args.dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
