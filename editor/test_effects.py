"""effects.py on hand-built level programs and effect texture tables; no disc data."""

import math
import struct
import unittest
from types import SimpleNamespace

import effects


def ball_tables(at: int) -> bytearray:
    """The nanotech tables at offset AT of a blank program: a sphere of radius 0.3, coordinates
    in 0..1, colours, the ring's and shimmer's vertices and quads (each quad k: 4k..4k+3)."""
    data = bytearray(at + effects.END + 64)
    for i in range(effects.BALL_VERTICES):
        a = i * 0.1
        struct.pack_into("<4f", data, at + 16 * i, 0.3 * math.cos(a), 0.3 * math.sin(a), 0.0, 1.0)
        struct.pack_into("<2f", data, at + effects.UVS + 8 * i, i / 290, 0.5)
        struct.pack_into("<I", data, at + effects.COLOURS + 4 * i, 0x50804040)
    for i in range(effects.RING_QUADS):
        struct.pack_into("<4h", data, at + effects.RING_INDICES + 8 * i, 4 * i, 4 * i + 1, 4 * i + 2, 4 * i + 3)
    for i in range(effects.SHIMMER_QUADS):
        struct.pack_into("<4h", data, at + effects.SHIMMER_INDICES + 8 * i, 4 * i, 4 * i + 1, 4 * i + 3, 4 * i + 2)
    return data


class FindBall(unittest.TestCase):
    def test_the_tables_are_found_where_they_start(self):
        data = ball_tables(0x230)
        self.assertEqual(effects.find_ball(bytes(data), [{"offset": 0, "bytes": len(data)}]), 0x230)

    def test_a_program_without_them_has_none(self):
        data = bytes(effects.END * 2)
        self.assertIsNone(effects.find_ball(data, [{"offset": 0, "bytes": len(data)}]))

    def test_the_ring_starts_on_a_quadword_after_the_colours(self):
        self.assertEqual(effects.RING % 16, 0)
        self.assertEqual(effects.RING - effects.COLOURS, 0x490)


class EffectTexture(unittest.TestCase):
    def level(self, count: int) -> SimpleNamespace:
        index = bytearray(0x100)
        struct.pack_into("<2i", index, 0x58, count, 0x80)
        struct.pack_into("<i", index, 0x68, 0x10)
        struct.pack_into("<4i", index, 0x80 + 16, 0, 1024, 2, 2)    # texture 1: palette, texels, 2x2
        core = bytearray(0x10 + 1024 + 4)
        core[0x10 + 1024:0x10 + 1028] = bytes((0, 1, 2, 3))
        return SimpleNamespace(index=bytes(index), core=bytes(core))

    def test_a_texture_is_read_from_its_palette_and_texel_offsets(self):
        texture = effects.effect_texture(self.level(2), 1)
        self.assertEqual((texture.width, texture.height, texture.pixels), (2, 2, bytes((0, 1, 2, 3))))

    def test_an_index_past_the_table_has_none(self):
        self.assertIsNone(effects.effect_texture(self.level(1), 1))


def gate_program(outer_x: float = 0.0) -> bytes:
    """A level 05 program of the NTSC disc's layout with only the electric gate's values in it."""
    at = effects.GATE_ADDRESSES["ntsc"]
    low, high = bytearray(0x100), bytearray(0x100)
    struct.pack_into("<4I", low, at["colours"] - 0x161D00, 0x300000FF, 0x30000080, 0x200000FF, 0x20000080)
    struct.pack_into("<i", low, at["texture"] - 0x161D00, 19)
    struct.pack_into("<8f", high, at["uvs"] - 0x20B700, 0.5, 0, 0.5, 1, 0.5, 0, 0.5, 1)
    for table, half in (("outer", 0.05), ("inner", 0.1)):
        for i, (y, z) in enumerate(((-5, -half), (-5, half), (5, -half), (5, half))):
            struct.pack_into("<4f", high, at[table] - 0x20B700 + 16 * i, outer_x, y, z, 1.0)
    entry = 0x161D00
    return (struct.pack("<4I", 0x161D00, len(low), 0, entry) + bytes(low)
            + struct.pack("<4I", 0x20B700, len(high), 0, entry) + bytes(high))


class ElectricGate(unittest.TestCase):
    def level(self, program: bytes, version: str = "ntsc", number: int = 5) -> SimpleNamespace:
        return SimpleNamespace(id=number, version=version, stored={"overlay": program})

    def test_the_beams_are_read_by_the_discs_addresses(self):
        gate = effects.electric_gate(self.level(gate_program()))
        self.assertEqual(gate["texture"], 19)
        self.assertEqual(gate["outer_colours"], [[255, 0, 0, 48], [128, 0, 0, 48]])
        self.assertEqual(gate["inner_colours"], [[255, 0, 0, 32], [128, 0, 0, 32]])
        self.assertEqual(gate["uvs"][1], [0.5, 1.0])
        self.assertAlmostEqual(gate["inner"][3][2], 0.1)
        self.assertEqual(gate["outer"][2][1], 5.0)

    def test_another_level_or_disc_has_none(self):
        self.assertIsNone(effects.electric_gate(self.level(gate_program(), number=4)))
        self.assertIsNone(effects.electric_gate(self.level(gate_program(), version="jp")))
        self.assertIsNone(effects.electric_gate(self.level(b"")))

    def test_a_program_of_another_layout_has_none(self):
        self.assertIsNone(effects.electric_gate(self.level(gate_program(outer_x=3.0))))


if __name__ == "__main__":
    unittest.main()
