"""Imported moby classes: a moby of a class from another level, which runs as it does there.

A moby under Game/Mobys with rc1_foreign_level metadata (the level the class is from) and the class
of a moby scene the level does not have; rc1_foreign_class_scale is its class's scale, which the
editor draws it at (its own scale is the node's over that). The port registers the class when the level loads, from its
data as its own level's core has it, and runs its update in its own level's program, swapped in for
the call (port/game/rac1/host/foreign_class.c). Pack writes, beside the level's placements:

    <out>/level_NN/classes.tsv           one "class<TAB>level" a line
    <out>/level_NN/classes/<class>.bin   the class's data from its level's core

and places the moby with a copy of the first moby of that class in its own level: its fields and its
pvar block (its moby links in it name no moby here: -1). Rac1 on the NTSC disc only, for now. Any
level's class can come into any other: only its level's data is swapped in for the call, and every
level's data ends below every level's program end.
"""

from dataclasses import dataclass, field
import struct
from pathlib import Path

import gameplay as gp
from mobys import FIELDS, RECORD, moby_instances
from pack import scene_mobys

LEVELS = 19


@dataclass
class Donor:
    """The first moby of a class in its own level: what a copy of it here takes."""
    level: int
    class_id: int
    record: dict
    pvar: bytes | None = None
    pointers: list[int] = field(default_factory=list)   # pointer fixups in the block (offsets)
    links: list[int] = field(default_factory=list)      # moby links in the block (offsets)
    shared: list[tuple[int, bytes]] = field(default_factory=list)   # (offset in the block, its shared data)


def wanted(text: str) -> tuple[dict[int, int], list[str]]:
    """The scene's imported classes (class -> its level), and what is wrong with the others."""
    out, problems = {}, []
    for node in scene_mobys(text):
        level = node["meta"].get("foreign_level")
        if level is None:
            continue
        if node["class_id"] is None:
            problems.append(f"{node['name']}: rc1_foreign_level on a moby with no class scene; not imported")
            continue
        try:
            level = int(str(level).strip().strip('"'))
        except ValueError:
            problems.append(f"{node['name']}: rc1_foreign_level must be a level number, not {level!r}")
            continue
        if out.setdefault(node["class_id"], level) != level:
            problems.append(f"{node['name']}: class {node['class_id']} is already imported from level "
                            f"{out[node['class_id']]:02}; a class comes from one level, so it is too")
    return out, problems


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
        # Its shared data (a part of the file's blob that its block points at): each piece runs to
        # the next piece any block points at, or the blob's end.
        blob, records = gp.shared(gameplay)
        starts = sorted({into for _, _, into in records} | {len(blob)})
        for pvar, where, into in records:
            if pvar == source and 0 <= into < len(blob):
                end = next(s for s in starts if s > into)
                out.shared.append((where, bytes(blob[into:end])))
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
