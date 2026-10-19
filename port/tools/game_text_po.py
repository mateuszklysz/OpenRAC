#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
The port's own translations of the game's texts, port/lang/game/<code>.po (port/lang/README.md,
game/common/game_text.h). The port writes the disc's texts beside its settings, each with its id
as msgctxt and its English as msgid; a file built into the port holds no English, only each
text's key, its id and the CRC-32 of its English ("1000/2ec6618e"), and its translation.

  game_text_po.py pack TRANSLATED.po OUT.po     a translation made from the port's game.pot (by
                                                id and English) as a built-in file (by key)
  game_text_po.py unpack GAME.pot BUILT_IN.po OUT.po
                                                a built-in file beside your disc's English again,
                                                to edit it in a PO editor; pack it back after
  game_text_po.py --check                       checks every lang/game/*.po: its header, its
                                                keys, its brackets; the test the port's ctest runs
"""
import argparse
import re
import sys
import zlib
from pathlib import Path

from i18n_template import LITERAL, escape, quoted, unescape

PORT = Path(__file__).resolve().parent.parent
FOLDER = PORT / "lang" / "game"
KEY = re.compile(r"^-?[0-9]+/[0-9a-f]{8}$")
# The bracketed words of the game's texts (assets/disc/game_text.cpp).
NAMES = {"default", "blue", "green", "purple", "orange", "cross", "circle", "triangle", "square",
         "L1", "R1", "L2", "R2", "lstick", "rstick"}
BRACKET = re.compile(r"\[\[|\[([^\[\]]*)\]")


def key(text_id, english):
    """game_text_key (assets/disc/game_text.h): the id and zlib's CRC-32 of the UTF-8 English."""
    return f"{text_id}/{zlib.crc32(english.encode('utf-8')):08x}"


def read_po(path):
    """(header, [entry]) of a .po file, each entry {"context", "id", "text", "where"}."""
    entries, current, into = [], None, None

    def fresh():
        return {"context": "", "id": "", "text": "", "where": ""}

    pending = fresh()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("#:"):
            pending["where"] = (pending["where"] + " " + line[2:].strip()).strip()
            continue
        if not line or line.startswith("#"):
            continue
        word = line.split(" ", 1)[0]
        if word == "msgctxt":
            current, pending = pending, fresh()
            entries.append(current)
            into = "context"
        elif word == "msgid":
            if current is None or into != "context":
                current, pending = pending, fresh()
                entries.append(current)
            into = "id"
        elif word == "msgstr":
            into = "text"
        elif not line.startswith('"'):
            into = None
            continue
        if into is not None and current is not None:
            current[into] += "".join(unescape(p) for p in LITERAL.findall(line))
    header = ""
    if entries and entries[0]["id"] == "" and entries[0]["context"] == "":
        header = entries.pop(0)["text"]
    return header, entries


def write_po(path, comment, header, entries):
    out = [f"# {line}\n" for line in comment.splitlines()]
    out += ['msgid ""\n', 'msgstr ""\n']
    out += [f'"{escape(line)}\\n"\n' for line in header.splitlines() if line]
    for e in entries:
        out.append("\n")
        if e["where"]:
            out.append(f"#: {e['where']}\n")
        if e["context"]:
            out.append(quoted("msgctxt", e["context"]))
        out.append(quoted("msgid", e["id"]))
        out.append(quoted("msgstr", e["text"]))
    path.write_text("".join(out), encoding="utf-8")


BUILT_IN_NOTE = (
    "The game's texts, translated: built into the port. Each msgid is a text's key, its id and\n"
    "the CRC-32 of its English; port/lang/README.md says how to edit this file beside the English.")


def pack(translated, out):
    header, entries = read_po(translated)
    if "Language:" not in header:
        sys.exit(f"{translated}: the header has no Language:")
    keyed = []
    for e in entries:
        if not e["text"] or not re.fullmatch(r"-?[0-9]+", e["context"]):
            continue
        keyed.append({"context": "", "id": key(int(e["context"]), e["id"]), "text": e["text"],
                      "where": e["where"]})
    write_po(out, BUILT_IN_NOTE, header, keyed)
    print(f"{out}: {len(keyed)} of {len(entries)} texts")


def unpack(template, built_in, out):
    _, english = read_po(template)
    header, keyed = read_po(built_in)
    texts = {e["id"]: e["text"] for e in keyed}
    found = 0
    for e in english:
        e["text"] = texts.get(key(int(e["context"]), e["id"]), "")
        found += bool(e["text"])
    write_po(out, "The game's texts beside your disc's English: keep this file on your computer.\n"
             "port/tools/game_text_po.py pack makes it the built-in file again.", header, english)
    print(f"{out}: {found} of {len(english)} texts translated, of {len(keyed)} in {built_in}")


def check():
    problems = []
    if key(1000, "Café [cross]") != "1000/2ec6618e":
        problems.append("the key is not game_text_key's")
    for po in sorted(FOLDER.glob("*.po")):
        name = po.relative_to(PORT)
        header, entries = read_po(po)
        if "Language:" not in header:
            problems.append(f"{name}: the header has no Language:")
        seen = set()
        for e in entries:
            if not KEY.match(e["id"]):
                problems.append(f"{name}: {e['id']!r} is not a key (id/crc)")
            if e["id"] in seen:
                problems.append(f"{name}: {e['id']} twice")
            seen.add(e["id"])
            for m in BRACKET.finditer(e["text"]):
                word = m.group(1)
                if word is not None and word not in NAMES and not re.fullmatch(r"x[0-9A-F]{2}", word):
                    problems.append(f"{name}: {e['id']}: [{word}] is no button or colour")
        print(f"{name}: {len(entries)} texts")
    for p in problems:
        print(p, file=sys.stderr)
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("command", nargs="?", choices=("pack", "unpack"))
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    if args.check:
        return check()
    if args.command == "pack" and len(args.files) == 2:
        pack(*args.files)
    elif args.command == "unpack" and len(args.files) == 3:
        unpack(*args.files)
    else:
        parser.error("pack TRANSLATED.po OUT.po, unpack GAME.pot BUILT_IN.po OUT.po, or --check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
