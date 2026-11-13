#!/usr/bin/env python3
"""The level's mobys as the game starts them, worked out by the game's own code.

  moby_states.py PROJECT --data DIR --levels DIR [--level N ...] [--program FILE] [--ticks N]

Runs the port's states program (openrac-rac1-ntsc-states, port/game/common/program/moby_states.h)
on each level of the Godot project PROJECT (one project, or a folder of per-level projects): it
loads the level, runs the mobys' updates for a few ticks and writes each placed moby's state. The
result goes to levels/level_NN/moby_states.json beside level.json, where rc1/moby.gd reads it. A
custom map works the same way: give --levels the folder of its packed levels.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

# The states program the port's build makes (port/cmake/Games.cmake).
PROGRAM = ROOT / "port/build/release/openrac-rac1-ntsc-states"

# A level takes well under a second; a stuck run is ended.
TIMEOUT = 60


def level_folders(project: Path, wanted: list[int] | None) -> dict[int, Path]:
    """The project's level folders (levels/level_NN), by number."""
    found = {}
    for stats in [*project.glob("levels/level_*/level.json"), *project.glob("level_*/levels/level_*/level.json")]:
        number = json.loads(stats.read_text())["level"]
        if wanted is None or number in wanted:
            found[number] = stats.parent
    return dict(sorted(found.items()))


def states(program: Path, data: Path, levels: Path, level: int, ticks: int) -> dict:
    """One level's moby states, from a run of the program."""
    with tempfile.TemporaryDirectory() as temp:
        out = Path(temp) / "states.json"
        run = subprocess.run([str(program), "--data", str(data), "--levels", str(levels), "--level", str(level),
                              "--moby-states", str(out), "--moby-states-ticks", str(ticks)],
                             capture_output=True, text=True, timeout=TIMEOUT)
        if run.returncode != 0 or not out.is_file():
            raise RuntimeError(f"level {level}: the states program failed: {run.stderr.strip()[-400:]}")
        return json.loads(out.read_text())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("project", type=Path)
    parser.add_argument("--data", type=Path, required=True, help="the port's game data")
    parser.add_argument("--levels", type=Path, required=True, help="the port's levels, or a custom map's packed levels")
    parser.add_argument("--level", type=int, action="append", help="a level (repeatable; default: all in PROJECT)")
    parser.add_argument("--program", type=Path, default=Path(os.environ.get("OPENRAC_STATES", PROGRAM)))
    parser.add_argument("--ticks", type=int, default=300, help="ticks the level runs first (default: 300, 5 seconds)")
    args = parser.parse_args()
    if not args.program.is_file():
        sys.exit(f"no states program at {args.program}: build the port's openrac-rac1-ntsc-states target")
    folders = level_folders(args.project, args.level)
    if not folders:
        sys.exit(f"{args.project} has no levels to work out")
    for number, folder in folders.items():
        result = states(args.program, args.data, args.levels, number, args.ticks)
        (folder / "moby_states.json").write_text(json.dumps(result, separators=(",", ":")) + "\n")
        print(f"level {number}: {len(result['mobys'])} mobys")


if __name__ == "__main__":
    main()
