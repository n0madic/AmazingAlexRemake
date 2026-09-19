#!/usr/bin/env python3
"""Import the original Amazing Alex HD assets into the remake's engine-native tree (docs/12-asset-tree.md).

Input: an `.ipa` / `.apk`, an unpacked package directory or an
already decrypted tree (the output of decrypt_assets.py). Output: JSON + PNG + copied media, plus
`manifest.json` (source kind, profile, counts, sha1 of every emitted file). The C++ side never sees AES,
plists, PVR or KA3D containers (docs/10-architecture.md §4).

Usage: import_assets.py <ipa|apk|dir> <out-dir> [--profile 2048X1536] [--border-profile 1024X768] [--force]

Conversions:
  Levels/<chapter>/0_Location.plist, <Level>.plist   -> levels/<chapter>/index.json, <name>.json (version 7)
  GameItems*, LocationBackgrounds/*, <profile>/UIElements  -> atlases/<name>.png + .json (frames in file order)
  <profile>/*.dat (SPRT/COMP/FONT) + .png             -> ui/<profile>/<name>.json + copied PNG
  (drawn, tools/remake_ui.py)                         -> ui/<profile>/REMAKE_COMMON.json + .png (the remake's own sprites)
  <border-profile>/BORDER_BORDER.dat + .png           -> ui/<profile>/BORDER_BORDER.json + .png, resampled to
                                                         the profile's scale (the 2048X1536 profile ships none)
  TEXTS_BASIC.dat, TEXTS_LANGUAGE_SELECTION.dat       -> texts/<locale>.json
  Common/XML/*Scene.xml, Dialogs.xml, <profile>/XML/Fonts.xml, Tips.plist -> ui/scenes/, ui/fonts.json, tips.json
  Sounds_*/*.mp3, Music_*/*.mp3, LevelThumbnails_<largest>/*.jpg -> sounds/, music/, thumbnails/
  iTunesArtwork / the largest packaged launcher icon           -> branding/icon.png

Floats: every level `<real>` becomes float32(float(text)) — the original's own strtod → (float) path
(docs/02-level-format.md §6) — and is emitted with 9 significant digits; the importer checks that
float32(float(emitted)), i.e. what cJSON's strtod + the C++ loader's (float) cast produce, gives the same
float32 back.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import logging
import plistlib
import re
import struct
import sys
import zipfile
from pathlib import Path
from typing import Any, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
import decrypt_assets  # noqa: E402
import ka3d_dat  # noqa: E402
import ka3d_text  # noqa: E402

log = logging.getLogger("import_assets")

MANIFEST_FORMAT = 2
CURRENT_LEVEL_VERSION = 7
LEGACY_HELI_CONTROLLER = 40
RC_CONTROLLER = 36
HELICOPTER = 39
HANDLE_TYPE_SHIFT = 26
HANDLE_TYPE_MASK = 0x3F
MAX_ATTACHMENTS = 2
MAX_GOAL_TARGETS = 9
GAME_ITEMS_FRAME_COUNT = 150
EXPECTED_LEVELS = 116
EXPECTED_CHAPTERS = 4
FREE_CHAPTER_SUFFIX = "_free"   # the lite edition's chapter inside the Android package (bundle_chapters)
EXPECTED_ITEM_TYPES = 39          # distinct types in shipped levels (items + toolbox) after the 40 → 36 conversion: all but 12, 21, 40
EXPECTED_LOCALES = 5
PVR_HEADER = struct.Struct("<11I4sI")
PVR_RGBA8888 = 0x12
PVR_RGB565 = 0x13
PVR_FORMAT_MASK = 0xFF
PROFILE_RE = re.compile(r"^\d+X\d+$")
ATLAS_DIRS = {"GameItems": "Common/Game/GameItems", "GameItems2": "Common/Game/GameItems2"}
BACKGROUND_ATLASES = ["LocationBackground00", "LocationBackground01", "LocationBackground02", "LocationBackground03",
                      "LocationForegrounds"]
TEXT_BUNDLES = ["Common/TEXTS_BASIC.dat", "Common/TEXTS_LANGUAGE_SELECTION.dat"]
SCENE_XML_DIR = "Common/XML"
LEVEL_KEYS = ("version", "title", "description", "authorName", "backgroundIndex", "itemCount", "goal")


class ImportError_(Exception):
    """A missing or malformed input file; the message names it."""


# ---------------------------------------------------------------------------------------------------
# JSON emission with float32 discipline
# ---------------------------------------------------------------------------------------------------
class F32(float):
    """A float32 value that serialises with 9 significant digits."""


def f32(x: float) -> F32:
    return F32(struct.unpack("<f", struct.pack("<f", float(x)))[0])


def format_f32(v: F32) -> str:
    text = "%.9g" % float(v)
    back = struct.unpack("<f", struct.pack("<f", float(text)))[0]
    if back != float(v) and not (back != back and float(v) != float(v)):
        raise ImportError_(f"float32 round-trip failed for {float(v)!r} -> {text}")
    if "." not in text and "e" not in text and "n" not in text:
        text += ".0"   # keep it a JSON float (the loader accepts either, this keeps the schema readable)
    return text


def to_json(value: Any, indent: int = 1, level: int = 0) -> str:
    pad = "\n" + " " * (indent * (level + 1))
    end = "\n" + " " * (indent * level)
    if isinstance(value, F32):
        return format_f32(value)
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        raise ImportError_(f"plain float {value!r} in output: wrap it with f32()")
    if isinstance(value, str):
        out = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
        out = "".join(ch if ch >= " " else "\\u%04x" % ord(ch) for ch in out)
        return '"' + out + '"'
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]"
        return "[" + pad + ("," + pad).join(to_json(v, indent, level + 1) for v in value) + end + "]"
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = (to_json(str(k), indent, level + 1) + ": " + to_json(v, indent, level + 1) for k, v in value.items())
        return "{" + pad + ("," + pad).join(items) + end + "}"
    raise ImportError_(f"cannot serialise {type(value).__name__}")


def plist_to_json_value(value: Any, sort_keys: bool = False) -> Any:
    """Generic plist → JSON conversion (scenes, tips, fonts): reals become float32. Dictionary order is
    kept (the UI view trees are ordered) unless `sort_keys` is set — Tips.plist is a binary plist whose
    key order differs from the decrypted XML copy, and its order carries no meaning."""
    if isinstance(value, bool) or value is None or isinstance(value, (int, str)):
        return value
    if isinstance(value, float):
        return f32(value)
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, list):
        return [plist_to_json_value(v, sort_keys) for v in value]
    if isinstance(value, dict):
        keys = sorted(value) if sort_keys else list(value)
        return {str(k): plist_to_json_value(value[k], sort_keys) for k in keys}
    return str(value)


# ---------------------------------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------------------------------
class Source:
    """Read-only view of a bundle: paths relative to the bundle root, decrypted on read."""

    kind = "?"

    def names(self) -> list[str]:
        raise NotImplementedError

    def raw(self, rel: str) -> bytes:
        raise NotImplementedError

    def package_names(self) -> list[str]:
        """Paths relative to the supplied archive/directory, including files outside the Data root."""
        raise NotImplementedError

    def package_raw(self, rel: str) -> bytes:
        raise NotImplementedError

    def exists(self, rel: str) -> bool:
        return rel in self._name_set()

    def _name_set(self) -> set[str]:
        if not hasattr(self, "_names"):
            self._names = set(self.names())
        return self._names

    def read(self, rel: str) -> bytes:
        """Bytes of a bundle file, decrypted and unzipped where the bundle stores it that way."""
        if not self.exists(rel):
            if self.exists(rel + ".zip"):        # Android wraps textures: GameItems.pvr.zip
                with zipfile.ZipFile(io.BytesIO(self.raw(rel + ".zip"))) as zf:
                    entries = zf.namelist()
                    if len(entries) != 1:
                        raise ImportError_(f"{rel}.zip: expected one entry, found {len(entries)}")
                    return zf.read(entries[0])
            raise ImportError_(f"missing file in the bundle: {rel}")
        data = self.raw(rel)
        if decrypt_assets.is_encrypted(Path(rel), data[:8]):
            data = decrypt_assets.decrypt_bytes(data)
        return data

    def plist(self, rel: str) -> Any:
        try:
            return plistlib.loads(self.read(rel))
        except plistlib.InvalidFileException as exc:
            raise ImportError_(f"{rel}: not a property list ({exc})") from exc

    def listdir(self, rel_dir: str) -> list[str]:
        prefix = rel_dir.rstrip("/") + "/" if rel_dir else ""
        out = set()
        for n in self._name_set():
            if n.startswith(prefix):
                out.add(n[len(prefix):].split("/", 1)[0])
        return sorted(out)


class ZipSource(Source):
    def __init__(self, path: Path, kind: str) -> None:
        self.kind = kind
        self.zf = zipfile.ZipFile(path)
        self._package_all = [n for n in self.zf.namelist() if not n.endswith("/")]
        self.prefix = find_bundle_prefix(self._package_all)
        self._all = [n[len(self.prefix):] for n in self._package_all if n.startswith(self.prefix)]

    def names(self) -> list[str]:
        return self._all

    def raw(self, rel: str) -> bytes:
        return self.zf.read(self.prefix + rel)

    def package_names(self) -> list[str]:
        return self._package_all

    def package_raw(self, rel: str) -> bytes:
        return self.zf.read(rel)


class DirSource(Source):
    def __init__(self, path: Path, kind: str) -> None:
        self.kind = kind
        files = [p for p in path.rglob("*") if p.is_file()]
        rels = [p.relative_to(path).as_posix() for p in files]
        self.package_root = path
        self._package_all = rels
        self.prefix = find_bundle_prefix(rels)
        self.root = path / self.prefix if self.prefix else path
        self._all = [r[len(self.prefix):] for r in rels if r.startswith(self.prefix)]

    def names(self) -> list[str]:
        return self._all

    def raw(self, rel: str) -> bytes:
        return (self.root / rel).read_bytes()

    def package_names(self) -> list[str]:
        return self._package_all

    def package_raw(self, rel: str) -> bytes:
        return (self.package_root / rel).read_bytes()


def find_bundle_prefix(names: Iterable[str]) -> str:
    """The directory (with trailing slash, or '') that contains Common/Game/GameItems.plist."""
    marker = "Common/Game/GameItems.plist"
    candidates = sorted((n[: -len(marker)] for n in names if n.endswith(marker)), key=len)
    if not candidates:
        raise ImportError_(f"no {marker} found: not an Amazing Alex bundle")
    return candidates[0]


def open_source(path: Path) -> Source:
    if path.is_dir():
        src = DirSource(path, "dir")
        # A decrypted tree has plain-text plists; an unpacked bundle has encrypted ones.
        head = src.raw("Common/Game/GameItems.plist")[:8]
        src.kind = "decrypted" if head.startswith(decrypt_assets.PLAIN_MAGICS) else "bundle"
        return src
    suffix = path.suffix.lower()
    if suffix in (".ipa", ".apk", ".zip"):
        return ZipSource(path, "ipa" if suffix == ".ipa" else "apk" if suffix == ".apk" else "zip")
    raise ImportError_(f"{path}: not an .ipa/.apk or a directory")


# ---------------------------------------------------------------------------------------------------
# Output tree
# ---------------------------------------------------------------------------------------------------
class Output:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.files: dict[str, str] = {}   # relative path → sha1

    def write(self, rel: str, data: bytes) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.files[rel] = hashlib.sha1(data).hexdigest()

    def write_json(self, rel: str, value: Any) -> None:
        self.write(rel, (to_json(value) + "\n").encode("utf-8"))

    def remove(self, rel: str) -> bool:
        """Drops a file left by an earlier import (--force overwrites, it never clears the tree)."""
        path = self.root / rel
        if not path.exists():
            return False
        path.unlink()
        return True


# ---------------------------------------------------------------------------------------------------
# Levels (docs/02-level-format.md)
# ---------------------------------------------------------------------------------------------------
def with_type_bits(handle: int, item_type: int) -> int:
    """Rewrite the type bits of a handle (stored as a signed 32-bit int)."""
    value = (handle & ((1 << HANDLE_TYPE_SHIFT) - 1)) | (item_type << HANDLE_TYPE_SHIFT)
    return value - (1 << 32) if value >= (1 << 31) else value


def handle_type(handle: int) -> int:
    return (handle & 0xFFFFFFFF) >> HANDLE_TYPE_SHIFT


def convert_item(raw: dict, index: int, rel: str, legacy: bool) -> dict:
    item_type = int(raw["type"])
    handle = int(raw["handle"])
    item_data = int(raw.get("itemData", 0))
    if legacy and item_type == LEGACY_HELI_CONTROLLER:      # LevelLayoutUtils::LoadPlist (docs/02 §5)
        item_type = RC_CONTROLLER
        handle = with_type_bits(handle, RC_CONTROLLER)
    if legacy and item_type == HELICOPTER and item_data and handle_type(item_data) == LEGACY_HELI_CONTROLLER:
        item_data = with_type_bits(item_data, RC_CONTROLLER)
    if handle_type(handle) != item_type:
        raise ImportError_(f"{rel}: item {index} handle type bits {handle_type(handle)} != type {item_type}")
    count = int(raw.get("attachmentCount", 0))
    if count > MAX_ATTACHMENTS:
        raise ImportError_(f"{rel}: item {index} has {count} attachments (max {MAX_ATTACHMENTS})")
    attachments = []
    for k in range(count):
        a = raw[f"attachments_{k}"]
        attachments.append({"state": int(a["state"]), "objectIndex": int(a["objectIndex"]), "index": int(a["index"])})
    return {
        "type": item_type,
        "handle": handle,
        "center": [f32(raw["center_x"]), f32(raw["center_y"])],
        "angle": f32(raw["angle"]),
        "flags": int(raw.get("flags", 0)),
        "ropeEnd": [f32(raw.get("ropeEndPos_x", 0.0)), f32(raw.get("ropeEndPos_y", 0.0))],
        "itemData": item_data,
        "attachments": attachments,
    }


def convert_goal(raw: dict, version: int, rel: str) -> dict:
    goal_type = int(raw["type"])
    count = int(raw["itemCount"])
    handles = [int(h) for h in raw["itemHandles"]]
    handles2 = [int(h) for h in raw.get("itemHandles2", [0] * len(handles))]
    if len(handles) > MAX_GOAL_TARGETS or len(handles2) > MAX_GOAL_TARGETS:
        raise ImportError_(f"{rel}: goal lists longer than {MAX_GOAL_TARGETS}")
    handles += [0] * (MAX_GOAL_TARGETS - len(handles))
    handles2 += [0] * (MAX_GOAL_TARGETS - len(handles2))
    if version == 4:   # flat lists → pair form, as LevelLayoutUtils::Apply does (docs/02 §5)
        # The decrement is unguarded in the original too (an itemCount of 0 becomes -1); kept verbatim.
        if goal_type == 7:
            container = handles[0]
            for i in range(1, count):
                handles2[i - 1] = handles[i]
                handles[i - 1] = container
            count -= 1
        elif goal_type == 2:
            handles2[0] = handles[1]
            handles[1] = 0
            count -= 1
    return {
        "type": goal_type,
        "itemCount": count,
        "itemHandles": handles,
        "itemHandles2": handles2,
        "timeLimit": int(raw.get("timeLimit", 0)),
        "height": f32(raw["height"]),
        "width": f32(raw["width"]),
        "angle": f32(raw["angle"]),
        "negated": bool(raw.get("negated", False)),
    }


def convert_level(raw: dict, rel: str) -> dict:
    for key in LEVEL_KEYS:
        if key not in raw:
            raise ImportError_(f"{rel}: missing level key '{key}'")
    version = int(raw["version"])
    legacy = version < 8 and version != CURRENT_LEVEL_VERSION
    items = [convert_item(raw[f"itemInfos_{i}"], i, rel, legacy) for i in range(int(raw["itemCount"]))]
    toolbox = [{"type": int(raw[f"toolboxSlots_{i}"]["type"]), "amount": int(raw[f"toolboxSlots_{i}"]["amount"])}
               for i in range(int(raw.get("toolboxSlotCount", 0)))]
    return {
        "version": CURRENT_LEVEL_VERSION,
        "sourceVersion": version,
        "title": str(raw["title"]),
        "description": str(raw["description"]),
        "authorName": str(raw.get("authorName", "")),
        "backgroundIndex": int(raw["backgroundIndex"]),
        "toolbox": toolbox,
        "items": items,
        "goal": convert_goal(raw["goal"], version, rel),
        "rewardId": int(raw.get("rewardId", 0)),
        "tested": bool(raw.get("tested", False)),
    }


def bundle_chapters(src: Source) -> list[str]:
    """The chapters of the full game, sorted — the four books the remake's catalogue (05 §4) is built on.

    The Android package also carries `00_Classroom_free`: the lite edition's own chapter of 16 easy levels
    (`0_Location.plist` with an empty name, no chapter book, no thumbnails), skipped — it adds little to
    the full game — and left out of every count and gate.
    """
    chapters = sorted(c for c in src.listdir("Levels") if src.exists(f"Levels/{c}/0_Location.plist"))
    skipped = [c for c in chapters if c.endswith(FREE_CHAPTER_SUFFIX)]
    for c in skipped:
        log.info("levels: skipping %s (the lite edition's chapter, not part of the full game)", c)
    return [c for c in chapters if c not in skipped]


def import_levels(src: Source, out: Output, counts: dict[str, int]) -> None:
    chapters = bundle_chapters(src)
    if not chapters:
        raise ImportError_("no Levels/<chapter>/0_Location.plist found")
    n_levels = 0
    types: set[int] = set()
    for chapter in chapters:
        index = src.plist(f"Levels/{chapter}/0_Location.plist")
        names = [str(n) for n in index["levels"]]
        # Level files the chapter index does not list (four hidden Treehouse levels) are imported too and
        # recorded separately, so the play order stays the original's.
        unlisted = sorted(e[:-6] for e in src.listdir(f"Levels/{chapter}")
                          if e.endswith(".plist") and e != "0_Location.plist" and e[:-6] not in names)
        out.write_json(f"levels/{chapter}/index.json",
                       {"name": str(index["name"]), "levels": names, "unlisted": unlisted})
        for name in names + unlisted:
            rel = f"Levels/{chapter}/{name}.plist"
            level = convert_level(src.plist(rel), rel)
            out.write_json(f"levels/{chapter}/{name}.json", level)
            n_levels += 1
            types.update(it["type"] for it in level["items"])
            types.update(slot["type"] for slot in level["toolbox"])
    counts["chapters"] = len(chapters)
    counts["levels"] = n_levels
    counts["itemTypes"] = len(types)
    log.info("levels: %d in %d chapters, %d item types", n_levels, len(chapters), len(types))


# ---------------------------------------------------------------------------------------------------
# Atlases: PVR v2 → PNG, TexturePacker plist → frame list (docs/01 §3–§4)
# ---------------------------------------------------------------------------------------------------
def pvr_to_png(data: bytes, rel: str) -> tuple[bytes, int, int]:
    from PIL import Image

    if len(data) < PVR_HEADER.size:
        raise ImportError_(f"{rel}: truncated PVR header")
    header_len, height, width, mipmaps, flags, data_len, bpp, rmask, gmask, bmask, amask, magic, surfaces = \
        PVR_HEADER.unpack_from(data)
    if magic != b"PVR!" or header_len != PVR_HEADER.size:
        raise ImportError_(f"{rel}: not a PVR v2 texture")
    if surfaces != 1 or mipmaps != 0:
        raise ImportError_(f"{rel}: unsupported PVR layout (surfaces {surfaces}, mipmaps {mipmaps})")
    pixels = data[header_len:header_len + data_len]
    fmt = flags & PVR_FORMAT_MASK
    if fmt == PVR_RGBA8888 and bpp == 32:
        if len(pixels) != width * height * 4:
            raise ImportError_(f"{rel}: pixel data size mismatch")
        img = Image.frombytes("RGBA", (width, height), pixels)
    elif fmt == PVR_RGB565 and bpp == 16:
        if len(pixels) != width * height * 2:
            raise ImportError_(f"{rel}: pixel data size mismatch")
        # Explicit unpack (little-endian RRRRRGGGGGGBBBBB), expanding to 8 bits per channel.
        words = struct.unpack("<%dH" % (width * height), pixels)
        rgb = bytearray(width * height * 3)
        for i, w in enumerate(words):
            r = (w >> 11) & 0x1F
            g = (w >> 5) & 0x3F
            b = w & 0x1F
            rgb[3 * i] = (r << 3) | (r >> 2)
            rgb[3 * i + 1] = (g << 2) | (g >> 4)
            rgb[3 * i + 2] = (b << 3) | (b >> 2)
        img = Image.frombytes("RGB", (width, height), bytes(rgb))
    else:
        raise ImportError_(f"{rel}: unsupported PVR pixel format {fmt:#x} ({bpp} bpp) — only RGBA8888 and RGB565 occur "
                           "in the shipped bundles")
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=False)
    return buf.getvalue(), width, height


def parse_rect(text: str) -> tuple[int, ...]:
    return tuple(int(v) for v in re.findall(r"-?\d+", text))


def frame_list(atlas: dict, rel: str) -> list[dict]:
    frames = []
    for name, fr in atlas["frames"].items():          # file order == frame index (docs/01 §3)
        x, y, w, h = parse_rect(fr["frame"])
        ox, oy = parse_rect(fr.get("offset", "{0,0}"))
        sw, sh = parse_rect(fr.get("sourceSize", "{%d,%d}" % (w, h)))
        frames.append({"name": str(name), "x": x, "y": y, "w": w, "h": h, "rotated": bool(fr.get("rotated", False)),
                       "offset": [ox, oy], "sourceSize": [sw, sh]})
    if not frames:
        raise ImportError_(f"{rel}: no frames")
    return frames


def import_atlas(src: Source, out: Output, plist_rel: str, pvr_rel: str, name: str) -> int:
    atlas = src.plist(plist_rel)
    png, width, height = pvr_to_png(src.read(pvr_rel), pvr_rel)
    frames = frame_list(atlas, plist_rel)
    out.write(f"atlases/{name}.png", png)
    out.write_json(f"atlases/{name}.json", {"texture": f"{name}.png", "size": [width, height], "frames": frames})
    return len(frames)


def import_atlases(src: Source, out: Output, profile: str, counts: dict[str, int]) -> None:
    for name, base in ATLAS_DIRS.items():
        counts[f"frames.{name}"] = import_atlas(src, out, base + ".plist", base + ".pvr", name)
    for name in BACKGROUND_ATLASES:
        base = f"Common/Game/LocationBackgrounds/{name}"
        counts[f"frames.{name}"] = import_atlas(src, out, base + ".plist", base + ".pvr", name)
    counts["frames.UIElements"] = import_atlas(src, out, f"{profile}/UIElements.plist", f"{profile}/UIElements.pvr",
                                               "UIElements")
    if counts["frames.GameItems"] != GAME_ITEMS_FRAME_COUNT:
        raise ImportError_(f"GameItems has {counts['frames.GameItems']} frames, expected {GAME_ITEMS_FRAME_COUNT}")


# ---------------------------------------------------------------------------------------------------
# KA3D UI containers, texts, XML scenes, tips, media
# ---------------------------------------------------------------------------------------------------
def import_ui(src: Source, out: Output, profile: str, counts: dict[str, int], border_profile: str | None) -> None:
    n = 0
    names: set[str] = set()
    for entry in src.listdir(profile):
        if not entry.endswith(".dat"):
            continue
        if entry[:-4] in DROPPED_UI_CONTAINERS:
            stale = [ext for ext in (".json", ".png") if out.remove(f"ui/{profile}/{entry[:-4]}{ext}")]
            log.info("ui: %s dropped (the remake has no use for it)%s", entry[:-4],
                     " — stale copy removed" if stale else "")
            continue
        rel = f"{profile}/{entry}"
        parsed = ka3d_dat.parse_bytes(src.read(rel), entry)
        out.write_json(f"ui/{profile}/{entry[:-4]}.json", parsed)
        png = entry[:-4] + ".png"
        if src.exists(f"{profile}/{png}"):
            out.write(f"ui/{profile}/{png}", src.read(f"{profile}/{png}"))
        names.add(entry[:-4])
        n += 1
    if border_profile and BORDER_CONTAINER not in names:
        import_border(src, out, profile, border_profile)
        n += 1
        counts["ui.border"] = 1
    counts["ui.dat"] = n
    import_remake_ui(out, profile)
    fonts_rel = f"{profile}/XML/Fonts.xml"
    out.write_json("ui/fonts.json", plist_to_json_value(src.plist(fonts_rel)))
    log.info("ui: %d KA3D containers from %s", n, profile)


BORDER_CONTAINER = "BORDER_BORDER"

# The book sheets of Level of the Week and World of Contraptions (Rovio's online books, docs/06 §1.1): the
# remake's chapter panel holds the four chapters and My Contraptions only, so the ~1.6 MB stays out of the tree.
# Their BOOK_*_<LANG> composites in BOOKS_COMPOSPRITES stay; a composite whose part sprites are missing is
# never asked for.
DROPPED_UI_CONTAINERS = frozenset({"BOOKS_BOOK_LEVELSOFTHEWEEK", "BOOKS_BOOK_WORLDOFCONTRAPTIONS"})


def profile_width(profile: str) -> int:
    m = re.fullmatch(r"(\d+)X(\d+)", profile)
    if not m:
        raise ImportError_(f"profile name {profile!r} is not <W>X<H>")
    return int(m.group(1))


def import_border(src: Source, out: Output, profile: str, border_profile: str) -> None:
    """The letterbox side frames (GameView's BorderLeft / BorderRight, docs/11 §1) from another profile's
    BORDER_BORDER container, resampled by the ratio of the profile widths (2048X1536 / 1024X768 = 2) so that
    the sheet matches the imported profile's pixel scale; the sprite rects and pivots scale alike."""
    from PIL import Image

    rel = f"{border_profile}/{BORDER_CONTAINER}.dat"
    if not src.exists(rel):
        raise ImportError_(f"no {rel} in the bundle (available profiles: {', '.join(available_profiles(src))})")
    factor = profile_width(profile) / profile_width(border_profile)
    if factor != int(factor):
        raise ImportError_(f"border profile {border_profile} does not scale to {profile} by an integer factor")
    k = int(factor)
    parsed = ka3d_dat.parse_bytes(src.read(rel), f"{BORDER_CONTAINER}.dat")
    for chunk in parsed["chunks"]:
        if chunk.get("tag") != "SPRT":
            raise ImportError_(f"{rel}: unexpected chunk {chunk.get('tag')}")
        for sprite in chunk["sprites"]:
            for key in ("x", "y", "w", "h", "pivotX", "pivotY"):
                sprite[key] = sprite[key] * k
    out.write_json(f"ui/{profile}/{BORDER_CONTAINER}.json", parsed)
    with Image.open(io.BytesIO(src.read(f"{border_profile}/{BORDER_CONTAINER}.png"))) as img:
        resized = img.convert("RGBA").resize((img.width * k, img.height * k), Image.Resampling.LANCZOS)
        buf = io.BytesIO()
        resized.save(buf, format="PNG")
    out.write(f"ui/{profile}/{BORDER_CONTAINER}.png", buf.getvalue())
    log.info("ui: %s from %s resampled x%d for %s", BORDER_CONTAINER, border_profile, k, profile)


def import_remake_ui(out: Output, profile: str) -> None:
    """The remake's own sprites (the music switch's note icons, docs/06 §4), drawn for the profile's scale."""
    import remake_ui   # Pillow only here, as in import_border

    png, container = remake_ui.build(profile_width(profile))
    out.write_json(f"ui/{profile}/{remake_ui.CONTAINER}.json", container)
    out.write(f"ui/{profile}/{remake_ui.CONTAINER}.png", png)
    log.info("ui: %s drawn for %s", remake_ui.CONTAINER, profile)


def import_texts(src: Source, out: Output, counts: dict[str, int]) -> None:
    merged: dict[str, dict[str, str]] = {}
    for rel in TEXT_BUNDLES:
        table = ka3d_text.parse(src.read(rel))
        for locale, texts in table.items():
            merged.setdefault(locale, {}).update(texts)
    for locale, texts in merged.items():
        out.write_json(f"texts/{locale}.json", texts)
    counts["locales"] = len(merged)
    log.info("texts: %d locales", len(merged))


def import_scenes(src: Source, out: Output, counts: dict[str, int]) -> None:
    n = 0
    for entry in src.listdir(SCENE_XML_DIR):
        if entry.endswith("Scene.xml") or entry == "Dialogs.xml":
            out.write_json(f"ui/scenes/{entry[:-4]}.json", plist_to_json_value(src.plist(f"{SCENE_XML_DIR}/{entry}")))
            n += 1
    counts["scenes"] = n
    tips = src.plist("Common/Tips.plist")
    out.write_json("tips.json", plist_to_json_value(tips, sort_keys=True))


def pick_dir(src: Source, candidates: list[str], what: str) -> str:
    for c in candidates:
        if src.listdir(c):
            return c
    raise ImportError_(f"no {what} directory found (tried {', '.join(candidates)})")


def import_media(src: Source, out: Output, counts: dict[str, int]) -> None:
    sounds = pick_dir(src, ["Sounds_high", "Sounds_low"], "sounds")
    music = pick_dir(src, ["Music_high", "Music_low"], "music")
    thumbs_dirs = sorted((d for d in src.listdir("") if d.startswith("LevelThumbnails_")),
                         key=lambda d: int(d.rsplit("_", 1)[1]), reverse=True)
    if not thumbs_dirs:
        raise ImportError_("no LevelThumbnails_<size> directory found")
    thumbs = thumbs_dirs[0]
    for src_dir, dst_dir, suffix in ((sounds, "sounds", ".mp3"), (music, "music", ".mp3"), (thumbs, "thumbnails", ".jpg")):
        n = 0
        for entry in src.listdir(src_dir):
            if entry.lower().endswith(suffix):
                out.write(f"{dst_dir}/{entry}", src.read(f"{src_dir}/{entry}"))
                n += 1
        counts[dst_dir] = n
    log.info("media: %s, %s, %s", sounds, music, thumbs)


def import_branding(src: Source, out: Output) -> None:
    """Import the best launcher artwork available outside or beside the game's Data directory."""
    from PIL import Image

    package_names = set(src.package_names())
    data_parent = src.prefix[:-len("Data/")] if src.prefix.endswith("Data/") else ""
    candidates = [
        "iTunesArtwork",
        data_parent + "Icon-72@2x.png",
        data_parent + "Icon@2x.png",
        data_parent + "Icon-72.png",
        data_parent + "Icon.png",
        "res/drawable-xxxhdpi/icon.png",
        "res/drawable-xxhdpi/icon.png",
        "res/drawable-xhdpi/icon.png",
        "res/drawable-hdpi/icon.png",
        "res/drawable-mdpi/icon.png",
        "res/drawable-ldpi/icon.png",
    ]
    source = next((name for name in candidates if name in package_names), None)
    if source is None:
        # A bare decrypted Data tree carries no launcher artwork; every consumer of branding/icon.png (the
        # platform build scripts, the web favicon) tolerates its absence, so the import is still complete.
        log.warning("branding: no launcher artwork in the supplied package, branding/icon.png not written")
        return
    try:
        with Image.open(io.BytesIO(src.package_raw(source))) as image:
            if image.width != image.height:
                raise ImportError_(f"{source}: launcher artwork is not square ({image.width}x{image.height})")
            converted = image.convert("RGBA")
            buf = io.BytesIO()
            converted.save(buf, "PNG")
    except OSError as exc:
        raise ImportError_(f"{source}: cannot decode launcher artwork ({exc})") from exc
    out.write("branding/icon.png", buf.getvalue())
    log.info("branding: %s (%dx%d) -> branding/icon.png", source, converted.width, converted.height)


# ---------------------------------------------------------------------------------------------------
def available_profiles(src: Source) -> list[str]:
    return sorted((d for d in src.listdir("") if PROFILE_RE.match(d) and src.exists(f"{d}/UIElements.plist")),
                  key=lambda d: int(d.split("X")[0]), reverse=True)


def run(source: Path, out_dir: Path, profile: str | None, force: bool, border_profile: str | None = None) -> dict:
    src = open_source(source)
    profiles = available_profiles(src)
    if not profiles:
        raise ImportError_("no UI profile (<W>X<H>/UIElements.plist) in the bundle")
    if profile is None:
        profile = profiles[0]
    elif profile not in profiles:
        raise ImportError_(f"profile {profile} not in the bundle (available: {', '.join(profiles)})")
    log.info("source: %s (%s), profile %s", source, src.kind, profile)
    if out_dir.exists() and any(out_dir.iterdir()) and not force:
        raise ImportError_(f"{out_dir} is not empty (use --force to overwrite)")
    out = Output(out_dir)
    counts: dict[str, int] = {}
    import_levels(src, out, counts)
    import_atlases(src, out, profile, counts)
    import_ui(src, out, profile, counts, border_profile)
    import_texts(src, out, counts)
    import_scenes(src, out, counts)
    import_media(src, out, counts)
    import_branding(src, out)
    validate(counts)
    chapters = bundle_chapters(src)
    manifest = {
        "format": MANIFEST_FORMAT,
        "source": {"kind": src.kind, "name": source.name, "profiles": profiles},
        "profile": profile,
        "chapters": chapters,
        "counts": dict(sorted(counts.items())),
        "files": dict(sorted(out.files.items())),
    }
    out.write_json("manifest.json", manifest)
    log.info("wrote %d files to %s", len(out.files), out_dir)
    return manifest


def validate(counts: dict[str, int]) -> None:
    checks = {"levels": EXPECTED_LEVELS, "chapters": EXPECTED_CHAPTERS, "itemTypes": EXPECTED_ITEM_TYPES,
              "locales": EXPECTED_LOCALES, "frames.GameItems": GAME_ITEMS_FRAME_COUNT}
    for key, expected in checks.items():
        if counts.get(key) != expected:
            raise ImportError_(f"validation: {key} = {counts.get(key)}, expected {expected} (unknown or patched bundle)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help=".ipa / .apk / bundle directory / decrypted tree")
    parser.add_argument("out", type=Path, help="output directory (the asset root)")
    parser.add_argument("--profile", help="UI profile to import (default: the largest in the bundle)")
    parser.add_argument("--border-profile",
                        help="profile whose BORDER_BORDER sheet (the letterbox side frames) is resampled into the "
                             "imported profile when that one ships none (e.g. 1024X768 for 2048X1536)")
    parser.add_argument("--force", action="store_true", help="write into a non-empty output directory")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    try:
        run(args.source, args.out, args.profile, args.force, args.border_profile)
    except ImportError_ as exc:
        log.error("%s", exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
