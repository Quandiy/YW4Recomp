"""Built-in changes to the game's data, made from the player's own files at build time.

    python -m tools.yw4.game_tweaks

Writes changed copies of a few of the game's files to <work_dir>/builtin/romfs/ (the RomFS layout); the runtime lays
that folder over the game's RomFS before the player's mods (runtime/src/romfs.c apply_mods), so a mod that brings the
same file still wins. Nothing of the game is in this repository: each change checks the original values first and is
left out, with a message, when they aren't what it expects (another version of the game).

Soul Steal (2026-11-12): the chance that a defeated Yo-kai leaves its soul, by rank, in battle_common.cfg_3.00.04.bin
(an RDBN table; six floats, percent, ranks E..S). The game's 1.5 .. 1.0 % made souls a long grind; a fan mod's 100 %
made them certain. These are in between: high, and harder as the rank goes up.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from . import config, cpk

BATTLE_COMMON = "data/common/gamedata/battle/battle_common.cfg_3.00.04.bin"
SOUL_OFFSETS = (0xC34, 0xC5C, 0xC84, 0xCAC, 0xCD4, 0xCFC)  # ranks E, D, C, B, A, S
SOUL_ORIGINAL = (1.5, 1.4, 1.3, 1.2, 1.1, 1.0)
SOUL_CHANCE = (80.0, 75.0, 70.0, 65.0, 60.0, 55.0)


def read_archived(romfs: Path, path: str) -> bytes | None:
    """A file of the game's RomFS: loose, or from the CPK archive data/common/<first folder>.cpk."""
    loose = romfs / path
    if loose.is_file():
        return loose.read_bytes()
    for arc in sorted((romfs / "data" / "common").glob("*.cpk")):
        for e in cpk.list_files(arc).files:
            if e.path == path:
                return cpk.read(arc, e)
    return None


def soul_steal(data: bytes) -> bytes | None:
    if len(data) < SOUL_OFFSETS[-1] + 4:
        return None
    vals = [struct.unpack_from("<f", data, o)[0] for o in SOUL_OFFSETS]
    if any(abs(v - w) > 1e-4 for v, w in zip(vals, SOUL_ORIGINAL)):
        return None
    out = bytearray(data)
    for o, v in zip(SOUL_OFFSETS, SOUL_CHANCE):
        struct.pack_into("<f", out, o, v)
    return bytes(out)


def main() -> int:
    cfg = config.load(require_exefs=False)
    out_root = cfg.work_dir / "builtin" / "romfs"
    data = read_archived(cfg.romfs_dir, BATTLE_COMMON)
    changed = soul_steal(data) if data else None
    target = out_root / BATTLE_COMMON
    if changed:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(changed)
        print("built-in: Soul Steal chances by rank E..S: " + ", ".join(f"{v:g}%" for v in SOUL_CHANCE))
    else:
        target.unlink(missing_ok=True)
        print("built-in: Soul Steal left as the game has it (battle_common isn't the expected version)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
