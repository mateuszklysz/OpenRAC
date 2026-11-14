"""Imported moby classes: a moby of a class from another level, which runs as it does there.

A moby under Game/Mobys with rc1_foreign_level metadata (the level the class is from) and the class
of a moby scene the level does not have. The port registers the class when the level loads, from its
data as its own level's core has it, and runs its update in its own level's program, swapped in for
the call (port/game/rac1/host/foreign_class.c). Pack writes, beside the level's placements:

    <out>/level_NN/classes.tsv           one "class<TAB>level" a line
    <out>/level_NN/classes/<class>.bin   the class's data from its level's core

and places the moby with a copy of the first moby of that class in its own level: its fields and its
pvar block (its moby links in it name no moby here: -1). Rac1 on the NTSC disc only, for now. The
class's own level must not have a bigger program than this level's (its program goes in the same
place).
"""

from dataclasses import dataclass, field
import struct
from pathlib import Path

import gameplay as gp
from mobys import FIELDS, RECORD, moby_instances
from pack import scene_mobys

LEVELS = 19
# Where each level's program ends (it starts at 0x15EF00, the same for every level); NTSC.
PROGRAM_ENDS = (0x2F0CD0, 0x31D208, 0x2FF930, 0x2F0A40, 0x2F4A68, 0x32ED58, 0x319998, 0x32C980,
                0x31A310, 0x318D90, 0x2F9F60, 0x32BF48, 0x319D10, 0x31B6A8, 0x31A1F8, 0x2FC3E8,
                0x2F8E80, 0x303EC8, 0x30A0C0)


@dataclass
class Donor:
    """The first moby of a class in its own level: what a copy of it here takes."""
    level: int
    class_id: int
    record: dict
    pvar: bytes | None = None
    pointers: list[int] = field(default_factory=list)   # pointer fixups in the block (offsets)
    links: list[int] = field(default_factory=list)      # moby links in the block (offsets)


def wanted(text: str) -> dict[int, int]:
    """The scene's imported classes: class -> its level."""
    out = {}
    for node in scene_mobys(text):
        level = node["meta"].get("foreign_level")
        if node["class_id"] is None or level is None:
            continue
        out[node["class_id"]] = int(str(level).strip().strip('"'))
    return out


def donor(level: int, gameplay: bytes, class_id: int) -> Donor | None:
    """The first moby of CLASS_ID in LEVEL's gameplay file, or None."""
    first = next((m for m in moby_instances(gameplay) if m["class_id"] == class_id), None)
    if first is None:
        return None
    record = dict(zip(FIELDS, RECORD.unpack(gp.records(gameplay)[first["index"]])))
    out = Donor(level, class_id, record)
    source = record["pvar_index"]
    links, pointers = gp.fixups(gameplay, gp.LINKS), gp.fixups(gameplay, gp.POINTERS)
    used = [m["fields"]["pvar_index"] for m in moby_instances(gameplay)] + [p for p, _ in links + pointers]
    pvars = gp.pvar_table(gameplay, gp.pvar_count(gameplay, used))
    if 0 <= source < len(pvars):
        data_start, _ = gp.extent(gameplay, gp.DATA)
        at, size = pvars[source]
        block = bytearray(gameplay[data_start + at:data_start + at + size])
        out.links = [where for pvar, where in links if pvar == source]
        out.pointers = [where for pvar, where in pointers if pvar == source]
        for where in out.links:
            if where + 4 <= len(block):
                struct.pack_into("<i", block, where, -1)
        out.pvar = bytes(block)
    return out


def class_blob(level, class_id: int) -> bytes:
    """CLASS_ID's data in LEVEL's core (a level.Level), as the level's loader registers it."""
    for entry in level.table(0x18, 32):
        offset, number = struct.unpack_from("<Ii", entry, 0)
        if number == class_id:
            if offset == 0:
                raise ValueError(f"class {class_id} has no data in level {level.id:02}")
            return level.block(offset)
    raise ValueError(f"level {level.id:02} has no class {class_id}")


def check(here: int, there: int, class_id: int) -> None:
    """ValueError when CLASS_ID cannot come from level THERE into level HERE."""
    if not 0 <= there < LEVELS:
        raise ValueError(f"class {class_id}: there is no level {there}")
    if there == here:
        raise ValueError(f"class {class_id}: rc1_foreign_level is this level")
    if PROGRAM_ENDS[there] > PROGRAM_ENDS[here]:
        raise ValueError(f"class {class_id}: level {there:02}'s program is bigger than this level's, "
                         "so it cannot run here")


def write(out: Path, blobs: dict[int, tuple[int, bytes]]) -> list[str]:
    """Writes classes.tsv and the class blobs into OUT (the packed level's folder): what was done."""
    folder = out / "classes"
    if folder.is_dir():
        for old in folder.glob("*.bin"):
            old.unlink()
    if not blobs:
        if (out / "classes.tsv").exists():
            (out / "classes.tsv").unlink()
        return []
    folder.mkdir(parents=True, exist_ok=True)
    lines = ["# class\tlevel (editor/foreign_classes.py)"]
    for class_id, (level, blob) in sorted(blobs.items()):
        (folder / f"{class_id}.bin").write_bytes(blob)
        lines.append(f"{class_id}\t{level}")
    (out / "classes.tsv").write_text("\n".join(lines) + "\n")
    return [f"class {c}: imported from level {lvl:02}" for c, (lvl, _) in sorted(blobs.items())]
