"""game_scenes.py: the game's scenes as .cutscene files, from made-up chunks (no disc needed)."""

import json
import math
import struct
import unittest

import game_scenes as gs


def chunk(index: int, end_tick: int, actors: list[tuple[int, list]], subtitles=(), cut_at=None) -> bytes:
    """A decompressed NTSC chunk as parse_chunk reads it."""
    records = min(end_tick - index * gs.NTSC_TICKS_PER_CHUNK, gs.NTSC_TICKS_PER_CHUNK) + 1
    head = 0x14 + 4 * len(actors)
    body = bytearray()
    actor_at = []
    for klass, positions in actors:
        actor_at.append(head + len(body))
        frames = len(positions)
        record = bytearray(0x2c + 4 * frames)
        track = head + len(body) + len(record)
        struct.pack_into("<iiii", record, 0, klass, 0, 0, track)
        record[0x20] = frames
        record[0x22] = 0xff
        body += record
        for p in positions:
            body += struct.pack("<4f", *p, 0.0)
    camera_at = head + len(body)
    for r in range(records):
        t = index * gs.NTSC_TICKS_PER_CHUNK + r
        body += struct.pack("<3fI4f", t * 0.1, 2.0, 3.0, 1 if t == cut_at else 0, math.pi / 2, 0.0, 0.0, 0.5)
    subtitles_at = 0
    if subtitles:
        while head + len(body) < gs.SUBTITLE_MIN_OFFSET:
            body += b"\0"
        subtitles_at = head + len(body)
        texts = bytearray()
        entries = bytearray()
        table = 16 * (len(subtitles) + 1)
        for start, end, text in subtitles:
            offsets = []
            for language in gs.SUBTITLE_LANGUAGES:
                offsets.append(table + len(texts))
                texts += text.get(language, b"") + b"\0"
            entries += struct.pack("<hh5hh", start, end, *offsets, 0)
        entries += struct.pack("<h", -1) + bytes(14)
        body += entries + texts
    header = struct.pack("<hhihhHHi", end_tick, 0, subtitles_at, 12, -1, len(actors), 0, camera_at)
    return header + b"".join(struct.pack("<I", a) for a in actor_at) + bytes(body)


class GameScenes(unittest.TestCase):
    def test_letters(self):
        self.assertEqual(gs.game_text(b"\x88coutez, c'\x8ctait \x85 vous ! \xae\xa9o\xaf  "), "Écoutez, c'était à vous ! ¡ño¿")
        self.assertEqual(gs.game_text(b"a\xffb"), "a?b")

    def test_camera_rotation(self):
        # A quarter turn about x looks level, along +y with no turn about z: the format's yaw 90.
        roll, pitch, yaw = gs.camera_rotation((math.pi / 2, 0.0, 0.0))
        self.assertAlmostEqual(pitch, 0.0, places=4)
        self.assertAlmostEqual(yaw, math.pi / 2, places=4)
        # Less than a quarter turn looks down; z turns it the other way round.
        _, pitch, yaw = gs.camera_rotation((1.2, 0.0, -math.pi / 2))
        self.assertAlmostEqual(pitch, math.pi / 2 - 1.2, places=4)
        self.assertAlmostEqual(yaw, 0.0, places=4)
        self.assertAlmostEqual(gs.vertical_fov(math.tan(math.radians(32)) ), 2 * math.degrees(math.atan(math.tan(math.radians(32)) * 3 / 4)), places=4)

    def test_a_scene(self):
        end = 150
        hero = [(1.0 + f, 2.0, 3.0) for f in range(48)]
        clank = [(5.0, 5.0, 5.0)] * 48
        lines = [(10, 70, {"en": b"Hello", "fr": b"All\x8c"})]
        first = gs.parse_chunk(chunk(0, end, [(0, hero), (10, clank)], lines, cut_at=40), 0)
        second = gs.parse_chunk(chunk(1, end, [(0, hero[:27]), (10, clank[:27])], lines), 1)
        self.assertEqual(len(first["camera"]), 97)
        self.assertEqual(len(second["camera"]), 150 - 96 + 1)
        self.assertEqual(first["subtitles"], [{"start": 10, "end": 70, "text": {"en": "Hello", "fr": "Allé"}}])
        doc = gs.to_cutscene([first, second], 3, 4, {"en": "audio/game_scene_04_en.wav"})
        self.assertEqual(doc["length"], 150)
        keys = doc["camera"]["keys"]
        self.assertEqual([k["t"] for k in keys], list(range(151)))  # one a tick, the shared record once
        self.assertEqual([k["t"] for k in keys if k.get("cut")], [40])
        self.assertAlmostEqual(keys[100]["eye"][0], 10.0, places=3)
        self.assertAlmostEqual(keys[0]["fov"], gs.vertical_fov(0.5), places=2)
        hero_actor, clank_actor = doc["actors"]
        self.assertEqual(hero_actor["source"], {"figure": "hero"})
        self.assertEqual(doc["hero"]["mode"], "hidden")
        self.assertEqual(clank_actor["source"], {"class": 10})
        self.assertEqual(len(clank_actor["track"]), 1)     # standing still: one key
        self.assertEqual([k["t"] for k in hero_actor["track"]][:3], [0, 2, 4])   # 30 Hz frames
        self.assertEqual(hero_actor["track"][48]["t"], 96)                       # the second chunk
        self.assertEqual(len(doc["subtitles"]), 1)                               # once, not per chunk
        self.assertEqual(doc["subtitles"][0]["end"], 71)
        self.assertEqual(doc["sounds"], [{"t": 12, "file": {"en": "audio/game_scene_04_en.wav"}, "kind": "voice"}])
        # A second Ratchet is a figure too; a camera record that is no place is left out.
        twice = gs.parse_chunk(chunk(0, 90, [(0, hero), (0, hero)], []), 0)
        twice["camera"][5]["eye"] = (float("nan"), 0.0, 0.0)
        twice["camera"][6]["tan_half_fov"] = 0.0
        doc2 = gs.to_cutscene([twice], 3, 1, {})
        self.assertEqual([a["source"] for a in doc2["actors"]], [{"figure": "hero"}] * 2)
        self.assertEqual(len({a["id"] for a in doc2["actors"]}), 2)
        self.assertNotIn(5, [k["t"] for k in doc2["camera"]["keys"]])
        self.assertNotIn("fov", doc2["camera"]["keys"][5])   # tick 6, after the one left out
        json.dumps(doc2, allow_nan=False)

    def test_records(self):
        header = bytearray(0x2434)
        at = gs.HEADER_SCENES + 2 * gs.SCENE_RECORD_SIZE
        struct.pack_into("<6i", header, at, 500, 0, 600, 0, 0, 0)
        struct.pack_into("<3i", header, at + 24, 1000, 1010, 1012)
        records = gs.scene_records(bytes(header))
        self.assertEqual(len(records), 15)
        self.assertEqual(records[2]["ntsc"], [1000, 1010, 1012])
        self.assertEqual(records[2]["speech"][:3], [500, 0, 600])
        self.assertEqual(records[0]["ntsc"], [])


if __name__ == "__main__":
    unittest.main()
