"""The game's own cutscenes as .cutscene files (OpenRAC Editor's format, docs/CUTSCENE_FORMAT.md in
its repo), to start a new scene from one or to remake it.

Ratchet & Clank keeps a level's in-engine scenes in its level header (+0x184): 15 records of
0x250 bytes, each the speech VAG sector per language, then the sectors of its NTSC and its PAL
chunks (port/assets/disc/scene.h has the layout, from ReRAC). A chunk is WAD-compressed and holds
96 ticks (NTSC): a camera record per 60 Hz tick (eye, a cut flag, the turns about x, y and z, the
tangent of half the field of view), its actors (a moby class, a position per 30 Hz frame and the
joint frames of its own animation) and its subtitles in five languages.

What a .cutscene keeps: the camera key per tick (cuts as cuts, the field of view), each actor's
class and place per frame, the subtitles, the speech as WAV per language. Not kept: the actors'
joint animation (the scene's own frames, which the cutscene player has no call for) and their
turns, which are in those frames. The actors are made from their class and stand in its first
animation.

    python3 editor/game_scenes.py DISC.iso --level 3 --out DIR [--scene K]
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

from disc import SECTOR, Disc
from formats import FormatError, unpack, wad

HEADER_SCENES = 0x184
SCENE_RECORDS = 15
SCENE_RECORD_SIZE = 0x250
SCENE_SLOTS = (SCENE_RECORD_SIZE - 6 * 4) // 8   # chunk sectors per region: 71
NTSC_TICKS_PER_CHUNK = 96
SUBTITLE_MIN_OFFSET = 0x400
# The game's language numbers for the speech (audio.LANGUAGES: 0 and 1 are both English) and the
# subtitles' five.
SPEECH_LANGUAGES = ("en", None, "fr", "de", "es", "it")
SUBTITLE_LANGUAGES = ("en", "fr", "de", "es", "it")
HERO_CLASS = 0
# The game draws at 4:3 and keeps the horizontal half field of view's tangent; the format's field
# of view is vertical.
GAME_ASPECT = 4 / 3
# The game's letters past ASCII (options_menu.cpp's game_text, the other way round); 0xAD is the
# dash its lines use.
GAME_LETTERS = {0x84: "á", 0x85: "à", 0x86: "â", 0x87: "ä", 0x8C: "é", 0x8D: "è", 0x8E: "ê", 0x8F: "ë",
                0x94: "í", 0x95: "ì", 0x96: "î", 0x97: "ï", 0x9C: "ó", 0x9D: "ò", 0x9E: "ô", 0x9F: "ö",
                0xA4: "ú", 0xA5: "ù", 0xA6: "û", 0xA7: "ü", 0x80: "Á", 0x81: "À", 0x82: "Â", 0x83: "Ä",
                0x88: "É", 0x89: "È", 0x8A: "Ê", 0x8B: "Ë", 0x90: "Í", 0x91: "Ì", 0x92: "Î", 0x93: "Ï",
                0x98: "Ó", 0x99: "Ò", 0x9A: "Ô", 0x9B: "Ö", 0xA0: "Ú", 0xA1: "Ù", 0xA2: "Û", 0xA3: "Ü",
                0xA8: "Ñ", 0xA9: "ñ", 0xAA: "Ç", 0xAB: "ç", 0xAC: "ß", 0xAE: "¡", 0xAF: "¿", 0xAD: "—"}


def game_text(raw: bytes) -> str:
    """A line in the game's letters as text (one it has no letter for as "?"), spaces trimmed."""
    return "".join(chr(b) if b < 0x80 else GAME_LETTERS.get(b, "?") for b in raw).strip()


def camera_rotation(angles: tuple[float, float, float]) -> list[float]:
    """The format's [roll, pitch, yaw] from a scene camera's turns about x, y and z: the game's
    camera looks down its -z, so x a quarter turn looks level (pitch is what is left, above 0 down)
    and z turns it from +y (the format's yaw 0 looks along +x). Fitted on every NTSC scene against
    where its actors stand."""
    ax, ay, az = angles
    yaw = (az + math.pi / 2 + math.pi) % (2 * math.pi) - math.pi
    return [round(ay, 5), round(math.pi / 2 - ax, 5), round(yaw, 5)]


def scene_records(header: bytes) -> list[dict]:
    """Each record of a level header: {"speech": [sector or 0] * 6, "ntsc": [sectors], "pal": [...]},
    the regions' chunk sectors up to the first 0 (the last one ends the last chunk)."""
    out = []
    for k in range(SCENE_RECORDS):
        at = HEADER_SCENES + k * SCENE_RECORD_SIZE
        values = struct.unpack_from(f"<6i{SCENE_SLOTS}i{SCENE_SLOTS}i", header, at)
        speech, ntsc, pal = list(values[:6]), values[6:6 + SCENE_SLOTS], values[6 + SCENE_SLOTS:]

        def leading(sectors):
            out_ = []
            for s in sectors:
                if s <= 0:
                    break
                out_.append(s)
            return out_
        out.append({"speech": speech, "ntsc": leading(ntsc), "pal": leading(pal)})
    return out


def parse_chunk(data: bytes, index: int) -> dict:
    """A decompressed NTSC chunk: its end tick, audio start, camera records, actors, subtitles."""
    end_tick, _, subtitles_at, audio_start, _, actor_count, _, camera_at = unpack("<hhihhHHi", data, 0)
    left = end_tick - index * NTSC_TICKS_PER_CHUNK
    if left <= 0:
        raise FormatError(f"scene chunk {index} starts at or after the end tick {end_tick}")
    records = min(left, NTSC_TICKS_PER_CHUNK) + 1
    camera = []
    for i in range(records):
        ex, ey, ez, cut, ax, ay, az, tan_half = unpack("<3fI4f", data, camera_at + i * 0x20)
        camera.append({"eye": (ex, ey, ez), "cut": (cut & 0xff) != 0, "angles": (ax, ay, az), "tan_half_fov": tan_half})
    actors = []
    for i in range(actor_count):
        at, = unpack("<I", data, 0x14 + i * 4)
        klass, _, _, track = unpack("<iiii", data, at)
        frames = data[at + 0x10 + 0x10]
        positions = [unpack("<3f", data, track + f * 16) for f in range(frames)]
        actors.append({"class": klass, "positions": positions})
    subtitles = []
    if subtitles_at >= SUBTITLE_MIN_OFFSET:
        at = subtitles_at
        while True:
            start, = unpack("<h", data, at)
            if start == -1:
                break
            end, *offsets = unpack("<h5h", data, at + 2)
            text = {}
            for language, offset in zip(SUBTITLE_LANGUAGES, offsets):
                if offset < 0:
                    continue
                raw = data[subtitles_at + offset:]
                nul = raw.find(b"\0")
                if nul < 0:
                    raise FormatError("scene subtitle text without a NUL")
                if game_text(raw[:nul]):
                    text[language] = game_text(raw[:nul])
            subtitles.append({"start": start, "end": end, "text": text})
            at += 16
    return {"end_tick": end_tick, "audio_start": audio_start, "camera": camera, "actors": actors,
            "subtitles": subtitles}


def read_scene(disc: Disc, record: dict) -> list[dict] | None:
    """The NTSC chunks of a scene record, parsed; None when it has none."""
    sectors = record["ntsc"]
    if len(sectors) < 2:
        return None
    chunks = []
    for i in range(len(sectors) - 1):
        raw = disc.sectors(sectors[i], sectors[i + 1] - sectors[i])
        chunks.append(parse_chunk(wad(raw), i))
    end = chunks[0]["end_tick"]
    if any(c["end_tick"] != end for c in chunks) or -(-end // NTSC_TICKS_PER_CHUNK) != len(chunks):
        raise FormatError(f"the scene's {len(chunks)} chunks do not cover its end tick {end}")
    return chunks


def speech_wav(disc: Disc, sector: int) -> bytes | None:
    """A scene's speech VAG as a WAV, or None if it is not one."""
    import audio
    head = disc.read(sector * SECTOR, 0x30)
    if head[:4] != b"VAGp":
        return None
    size = struct.unpack_from(">I", head, 0x0C)[0]
    rate = struct.unpack_from(">I", head, 0x10)[0]
    if not 0 < size < 64 << 20 or not 4000 <= rate <= 96000:
        return None
    return audio.wav_file(1, rate, audio.decode_adpcm(disc.read(sector * SECTOR + 0x30, size)))


def vertical_fov(tan_half_fov: float) -> float:
    """The format's vertical field of view in degrees from the game's horizontal half tangent."""
    return math.degrees(2 * math.atan(tan_half_fov / GAME_ASPECT))


def to_cutscene(chunks: list[dict], level: int, k: int, speech: dict[str, str]) -> dict:
    """A .cutscene of the game's scene K of LEVEL from its parsed chunks; SPEECH: its WAV per
    language, beside it."""
    end = chunks[0]["end_tick"]
    keys = []
    for i, chunk in enumerate(chunks):
        for r, rec in enumerate(chunk["camera"]):
            t = i * NTSC_TICKS_PER_CHUNK + r
            if t > end or (r == len(chunk["camera"]) - 1 and i + 1 < len(chunks)):
                continue    # a chunk's last record is the next one's first
            if not all(math.isfinite(v) for v in (*rec["eye"], *rec["angles"])):
                continue    # a record that is no place (JSON has no NaN): the key before holds
            key = {"t": t, "eye": [round(v, 4) for v in rec["eye"]],
                   "rotation": camera_rotation(rec["angles"]), "ease": "linear"}
            if math.isfinite(rec["tan_half_fov"]) and rec["tan_half_fov"] > 0:
                key["fov"] = round(vertical_fov(rec["tan_half_fov"]), 3)
            if rec["cut"] and t > 0:
                key["cut"] = True
            keys.append(key)
    actors = []
    seen_hero = False
    for a, first in enumerate(chunks[0]["actors"]):
        track = []
        for i, chunk in enumerate(chunks):
            if a >= len(chunk["actors"]):
                continue
            for f, position in enumerate(chunk["actors"][a]["positions"]):
                if not all(math.isfinite(v) for v in position):
                    continue
                track.append({"t": i * NTSC_TICKS_PER_CHUNK + f * 2, "position": [round(v, 4) for v in position]})
        # Every Ratchet a figure (a moby of the hero's class would run the hero's own update).
        hero = first["class"] == HERO_CLASS
        actor_id = ("hero_double" if not seen_hero else f"hero_double_{a}") if hero else f"actor_{a}_{first['class']}"
        seen_hero = seen_hero or hero
        actor = {"id": actor_id,
                 "source": {"figure": "hero"} if hero else {"class": first["class"]},
                 "track": track[:1] if len({tuple(p["position"]) for p in track}) == 1 else track,
                 "end": "remove"}
        actors.append(actor)
    subtitles = []
    for chunk in chunks:
        for line in chunk["subtitles"]:
            item = {"t": line["start"], "end": line["end"] + 1, "text": line["text"]}
            if item not in subtitles:
                subtitles.append(item)
    sounds = []
    if speech:
        sounds.append({"t": max(0, chunks[0]["audio_start"]), "file": speech, "kind": "voice"})
    return {
        "format": "openrac-cutscene", "version": 1, "game": "rac1", "level": level,
        "name": f"game_scene_{k:02}", "length": end,
        "languages": [l for l in SUBTITLE_LANGUAGES if any(l in s["text"] for s in subtitles)] or ["en"],
        "trigger": {"kind": "manual", "once": False},
        "options": {"letterbox": False, "hide_hud": True, "skippable": True},
        "hero": {"mode": "hidden" if seen_hero else "held"},
        "camera": {"keys": keys, "end": "cut"},
        "actors": actors, "subtitles": subtitles, "sounds": sounds, "events": [],
        "source": {"disc_scene": k, "region": "ntsc"},
    }


def export_level(disc: Disc, level: int, out: Path, only: int | None = None) -> list[Path]:
    """Writes the level's scenes to OUT/game_scene_KK.cutscene, their speech to OUT/audio/."""
    toc = disc.read(disc.version.toc_lba * SECTOR, disc.version.toc_size)
    header_info = disc.level_header(toc, level)
    header = disc.read(header_info["header_lba"] * SECTOR, 0x2434)
    written = []
    for k, record in enumerate(scene_records(header)):
        if only is not None and k != only:
            continue
        chunks = read_scene(disc, record)
        if chunks is None:
            continue
        speech = {}
        for number, sector in enumerate(record["speech"]):
            language = SPEECH_LANGUAGES[number]
            if sector <= 0 or language is None or language in speech:
                continue
            wav_bytes = speech_wav(disc, sector)
            if wav_bytes is None:
                continue
            path = f"audio/game_scene_{k:02}_{language}.wav"
            (out / "audio").mkdir(parents=True, exist_ok=True)
            (out / path).write_bytes(wav_bytes)
            speech[language] = path
        doc = to_cutscene(chunks, level, k, speech)
        out.mkdir(parents=True, exist_ok=True)
        file = out / f"game_scene_{k:02}.cutscene"
        file.write_text(json.dumps(doc, indent=1, allow_nan=False) + "\n", encoding="utf-8")
        written.append(file)
    return written


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("disc", type=Path)
    parser.add_argument("--level", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--scene", type=int)
    args = parser.parse_args(argv)
    with Disc(args.disc) as disc:
        for file in export_level(disc, args.level, args.out, args.scene):
            print(file)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
