"""Effects the game's code draws for a moby, which no class model holds (Ratchet & Clank).

The nanotech crate (class 501) makes a nanotech ball (class 806) when it starts
(crate_update, create_linked_class_0326_moby), and the ball's draw callback
(draw_nanotech_ball) draws it from tables in the level program, not from a class model:

- inside a whole crate, four faint quads on the crate's sides whose texture slides with
  the view (the "shimmer"),
- once the crate is broken, a small sphere in effect texture 21 turned to face the camera,
  bobbing, ringed by a pulsing added glow (effect texture 11).

nanotech() reads those tables from the level's program, where they sit together (the
sphere's 290 vertices, texture coordinates and colours, the glow ring's 96 vertices and 32
quads, the shimmer's 16 vertices and 4 quads); rc1/nanotech.gd draws them.

Rilgar's electric gates (class 838) hide their model and draw five beams instead
(draw_electric_gate_beams): each beam is two added quads, a thin outer one and a thicker inner
one, 10 long across the gate at heights 1 to 5 in its frame, in effect texture 19, their
colours pulsing between two reds over 64 frames, each beam a fifth of the way on.
electric_gate() reads its tables and values from the level program by the disc's addresses.

Its sparks: eight points circle the ball, each about an axis of its own, and leave a trail
of fading particles. The trail's particle texture is the one the level's particle type list
gives type 62, mode 2 (spark_texture; Rilgar: 33). Each point also carries a bigger particle;
that one is not drawn.
"""

from formats import FormatError, Texture, overlay_sections, unpack
from level import Level, decoded

# The sphere's vertex count and its three strips (first vertex, count), as draw_nanotech_ball
# draws them (draw_env_overlay_mesh 0x94, 0x6d and 0x24 vertices from 0, 146 and 254).
BALL_VERTICES = 290
BALL_STRIPS = ((0, 0x94), (146, 0x6D), (254, 0x24))
RING_VERTICES, RING_QUADS = 96, 32
SHIMMER_VERTICES, SHIMMER_QUADS = 16, 4

# The tables after the sphere's vertices: its texture coordinates, colours, the ring's
# vertices and quads, the shimmer's vertices and quads (NTSC level 0: 0x1E14C0 onwards).
UVS = BALL_VERTICES * 16
COLOURS = UVS + BALL_VERTICES * 8
RING = (COLOURS + BALL_VERTICES * 4 + 15) // 16 * 16  # the colours end 8 bytes short of a quadword
RING_INDICES = RING + RING_VERTICES * 16
SHIMMER = RING_INDICES + RING_QUADS * 8
SHIMMER_INDICES = SHIMMER + SHIMMER_VERTICES * 16
END = SHIMMER_INDICES + SHIMMER_QUADS * 8

# Their effect textures (get_effect_texture): the sphere's and the shimmer's, and the ring's.
BALL_TEXTURE, RING_TEXTURE = 21, 11
# The sparks' trail particle (spawn_mode_controlled_alpha_fade_particle): its particle type
# (part_type62_textures) and the mode that picks its texture.
SPARK_TYPE, SPARK_MODE = 62, 2
# The sphere's radius in its table.
BALL_RADIUS = 0.3


def effect_texture(level: Level, index: int) -> Texture | None:
    """Effect texture INDEX (unpack_point_records' table: palette and texel offsets into the
    effect texture data, width, height), or None when the level has none such."""
    count, at = unpack("<2i", level.index, 0x58)
    base, = unpack("<i", level.index, 0x68)
    if not 0 <= index < count:
        return None
    palette, texels, width, height = unpack("<4i", level.index, at + 16 * index)
    pixels = level.core[base + texels:base + texels + width * height]
    colours = level.core[base + palette:base + palette + 1024]
    if len(pixels) < width * height or len(colours) < 1024:
        return None
    return Texture(width, height, pixels, colours)


def spark_texture(level: Level) -> int | None:
    """The sparks' particle texture: the level's particle type lists (core index word 0x6C: a
    16-byte head, then each type's offset from the table's start to its texture bytes, one per
    mode), which the game copies into the level program as part_typeNN_textures."""
    table, = unpack("<i", level.index, 0x6C)
    if not 0 < table < len(level.index):
        return None
    entry = table + 0x10 + 4 * SPARK_TYPE
    if entry + 4 > len(level.index):
        return None
    offset, = unpack("<i", level.index, entry)
    at = table + offset + SPARK_MODE
    return level.index[at] if offset > 0 and at < len(level.index) else None


def particle_texture(level: Level, index: int) -> Texture | None:
    """Particle texture INDEX (parse_particle_textures' table: palette offset, 0, texel offset,
    side of a square texture, into the particle texture data), or None when there is none such."""
    count, at = unpack("<2i", level.index, 0x50)
    base, = unpack("<i", level.index, 0x64)
    if not 0 <= index < count:
        return None
    palette, _, texels, side = unpack("<4i", level.index, at + 16 * index)
    pixels = level.core[base + texels:base + texels + side * side]
    colours = level.core[base + palette:base + palette + 1024]
    if len(pixels) < side * side or len(colours) < 1024:
        return None
    return Texture(side, side, pixels, colours)


def find_ball(program: bytes, sections: list[dict]) -> int | None:
    """Where the sphere's vertex table is in the level program: 290 vertices of radius 0.3
    (w 1.0), then texture coordinates in 0..1. Its address moves from level to level."""
    for section in sections:
        start, size = section["offset"], section["bytes"]
        for at in range(start, start + size - END + 1, 16):
            if unpack("<f", program, at + 12)[0] != 1.0:
                continue
            if all(_on_ball(program, at + 16 * i) for i in range(0, BALL_VERTICES, 9)) \
                    and all(0.0 <= c <= 1.0 for c in unpack(f"<{BALL_VERTICES * 2}f", program, at + UVS)):
                return at
    return None


def _on_ball(program: bytes, at: int) -> bool:
    x, y, z, w = unpack("<4f", program, at)
    return w == 1.0 and abs((x * x + y * y + z * z) ** 0.5 - BALL_RADIUS) < 1e-3


def nanotech(level: Level) -> dict | None:
    """The nanotech ball's tables (module doc), or None when the level's program has none."""
    raw = level.stored.get("overlay")
    if not raw:
        return None
    program = decoded(raw)
    try:
        sections = overlay_sections(program)["sections"]
    except FormatError:
        return None
    at = find_ball(program, sections)
    if at is None:
        return None

    def vectors(start: int, count: int) -> list:
        return [list(unpack("<3f", program, at + start + 16 * i)) for i in range(count)]

    def quads(start: int, count: int) -> list:
        return [list(unpack("<4h", program, at + start + 8 * i)) for i in range(count)]

    colours = unpack(f"<{BALL_VERTICES}I", program, at + COLOURS)
    return {"ball": {"vertices": vectors(0, BALL_VERTICES),
                     "uvs": [list(unpack("<2f", program, at + UVS + 8 * i)) for i in range(BALL_VERTICES)],
                     "colours": [[c & 0xFF, c >> 8 & 0xFF, c >> 16 & 0xFF, c >> 24] for c in colours],
                     "strips": [list(s) for s in BALL_STRIPS]},
            "ring": {"vertices": vectors(RING, RING_VERTICES), "quads": quads(RING_INDICES, RING_QUADS)},
            "shimmer": {"vertices": vectors(SHIMMER, SHIMMER_VERTICES),
                        "quads": quads(SHIMMER_INDICES, SHIMMER_QUADS)}}


# The electric gate's tables and values in level 05's program (names.tsv, names_ntsc.tsv), by
# the disc's version: the quads' texture coordinates, outer and inner corners (4 quadwords
# each), the colours (outer from, outer to, inner from, inner to) and the effect texture.
GATE_ADDRESSES = {"ntsc": {"uvs": 0x0020B740, "outer": 0x0020B760, "inner": 0x0020B7A0,
                           "colours": 0x00161D3C, "texture": 0x00161D98},
                  "pal": {"uvs": 0x0020BA80, "outer": 0x0020BAA0, "inner": 0x0020BAE0,
                          "colours": 0x00161DFC, "texture": 0x00161E58}}
GATE_LEVEL = 5


def program_reader(level: Level):
    """A function reading N bytes at an address of the level's program, None outside its
    sections; None when the level has no program."""
    raw = level.stored.get("overlay")
    if not raw:
        return None
    program = decoded(raw)
    try:
        sections = overlay_sections(program)["sections"]
    except FormatError:
        return None

    def read(address: int, size: int) -> bytes | None:
        for section in sections:
            if section["address"] <= address and address + size <= section["address"] + section["bytes"]:
                at = section["offset"] + address - section["address"]
                return program[at:at + size]
        return None
    return read


def electric_gate(level: Level) -> dict | None:
    """The electric gate's beams (module doc), or None for a level or disc without them."""
    addresses = GATE_ADDRESSES.get(level.version)
    read = program_reader(level) if addresses is not None and level.id == GATE_LEVEL else None
    if read is None:
        return None
    parts = {name: read(address, {"uvs": 32, "outer": 64, "inner": 64, "colours": 16, "texture": 4}[name])
             for name, address in addresses.items()}
    if any(part is None for part in parts.values()):
        return None

    def corners(data: bytes) -> list:
        return [list(unpack("<3f", data, 16 * i)) for i in range(4)]

    colours = [[c & 0xFF, c >> 8 & 0xFF, c >> 16 & 0xFF, c >> 24] for c in unpack("<4I", parts["colours"], 0)]
    gate = {"uvs": [list(unpack("<2f", parts["uvs"], 8 * i)) for i in range(4)],
            "outer": corners(parts["outer"]), "inner": corners(parts["inner"]),
            "outer_colours": colours[0:2], "inner_colours": colours[2:4],
            "texture": unpack("<i", parts["texture"], 0)[0]}
    # A program that is not the one these addresses are for: its corners are no beam.
    if any(abs(c[0]) > 1e-6 or not 0 < abs(c[1]) < 100 for c in gate["outer"] + gate["inner"]):
        return None
    return gate
